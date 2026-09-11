#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <stdint.h>
#include "Window.h"
#include "Renderer.h"
#include "Shader.h"
#include "Scene.h"
#include "PerfTimer.h"
#include "AssetManager.h"
#include "Debug.h"
#include "GLTFLoader.h"
#include "RRandom.h"
#include "ObjectCollider.h"
#include "RawInput.h"
#include "skeleton/PlayerCharacter.h"
//For MaybeAttachScreenshot's signature below. json.hpp only (not MCPServer.h): this is pure C++
//with no winsock in it, so it sidesteps the include-order trap MCPServer.h documents.
#include "tinygltf/json.hpp"
using json = nlohmann::json;

/*
    The thing that ties everything together.

    I guess we need to provide a basic implementation,
    but since there will be many different types of example applications and tests...
    It's hard to come up with anything generic right now.
    Most of the things needs some kind of specific order: Frame, input, physics.

    We'd also like an application that isn't so much a game, but only a UI from maybe ImGui or the default windows one.

    The basic order of things:
    Start:
     Creates a thread for frame rendering and one for physics. The idea being that Window Messages and input
     are sent to the thread that makes the window, but we want input to be coupled to physics.

     - Main thread only gathers window input
     - Frame thread actually draws frames.
     - Physics thread handles input to change state of things.

    When physics is paused, you do want to be able to move the camera, so you do need input...
    And since the camera is a physics objects... because everything is an object....?

*/
class Application{
public:
    Application();

    //Main functions an application can implement by overriding.
    virtual void Start(void);   // Creates 2 threads
    virtual void Init(void);    // Called from Frame Thread

    //Handlers for core's own SimCommand types (see core/SimCommand.h) - object_set_transform and
    //asset spawning. Registered on main_scene right after Init(), for the same reason
    //RegisterCoreMCPTools is: the scene has to exist. It lives on Application rather than on
    //Scene because spawning needs the AssetManager, which Scene has no business knowing about -
    //and keeping Scene as pure queue-and-dispatch is what lets an app register handlers that
    //close over its own types.
    void RegisterCoreCommandHandlers();

    //Submit a SimCommand and block until the physics thread has applied it, then return the
    //object it created or acted on (OBJECTID_INVALID on timeout, or if the handler made nothing).
    //This is what an MCP tool handler wants: the tool has to report the resulting state, and
    //the command has not run when SubmitCommand returns.
    //
    //ONLY callable from a thread that does NOT hold renderer->physics_mutex. The MCP threads
    //qualify. THE DEBUG UI DOES NOT: DrawImGuiUI runs with that mutex held, so waiting here for
    //the physics thread - which needs the same mutex to reach DrainCommands - would deadlock
    //instantly. UI code uses SubmitUICommand below instead.
    objectid_t SubmitCommandAndWait(const SimCommand& cmd, int timeout_ms = 2000);

    //What every debug-UI control that changes the simulation calls: submit and return, never
    //wait. See the deadlock note above, and the implementation's own comment.
    void SubmitUICommand(const SimCommand& cmd);

    //Generic, app-independent MCP tools (object_list/object_get/object_set_transform/
    //object_move/screenshot/...) - the MCP counterpart of the Generic Object UI panel. Registered
    //for every app right after Init(), before the MCP server starts accepting requests.
    void RegisterCoreMCPTools();

    //If requested, blocks (Renderer::RequestScreenshot) until the render thread has captured and
    //PNG-encoded the current frame, and attaches it to `result` as an MCP image content block. A
    //no-op passthrough otherwise, so any MCP tool in any app can offer a screenshot with one line:
    //  return MaybeAttachScreenshot(MyTelemetry(),args.value("include_screenshot",false));
    //A failure is reported in a "screenshot_error" field rather than replacing the result, since
    //the caller asked for the telemetry first and the picture second. Lives here rather than per
    //app because nothing about it is app-specific - it was written twice before it was moved.
    //The `screenshot` core MCP tool is this with an empty result.
    json MaybeAttachScreenshot(json result, bool include_screenshot);

    //The simulation clock, as the core sim_pause/sim_step tools report it: tick, paused,
    //pending_steps, timestep. Deliberately the CLOCK and nothing else - an app with telemetry
    //worth returning already has its own tool for it, and a core tool that tried to summarise
    //eleven different simulations would be wrong in ten of them.
    json SimClockJson();

    //Turn an {id|name} tool argument into an id, reading the scene at a tick boundary. The
    //POINTER is deliberately not returned: it is only valid while the lock is held, and every
    //caller here goes on to submit a command and wait, which must happen with the lock released.
    //An id stays meaningful across that gap, and ObjectJsonAtTickBoundary below resolves it again.
    bool ResolveObjectIdArg(const json& args, objectid_t& id_out, std::string& error);

    //Serialise one object by id, at a tick boundary. Reports a clear error rather than crashing if
    //the object stopped existing in between - which is a real outcome once a command has been
    //applied, since object_destroy is one of the commands.
    json ObjectJsonAtTickBoundary(objectid_t id, bool full);

    //Queue num_ticks single-steps and block until the physics thread has actually consumed them,
    //returning how many ticks REALLY ran. That return value is the honest answer and worth
    //checking: a timeout leaves the remainder queued to run later, so a caller that assumed it got
    //what it asked for would be reading the wrong state. Requires the simulation to be paused -
    //StepPhysics is a no-op otherwise, since a running sim is already stepping.
    uint64_t StepPhysicsAndWait(int num_ticks);

    //The point the app's camera orbits and zooms around, if it has one. Every app subclass keeps
    //its own `camera_target` (a copy-pasted field, not shared state), so the core camera MCP
    //tools cannot see it directly - this is the one-line opt-in that lets them read and write it.
    //Returning NULL just means camera_target is absent from those tools' view for this app.
    virtual vec3* GetCameraTargetPtr(){ return NULL; }

    //Physics thread
    virtual void UpdateInput(void);
    virtual void UpdateAnimations(void);

    /*
        The app's two per-pass hooks. Both run on the physics thread with renderer->physics_mutex
        held; what separates them is HOW OFTEN.

        UpdateView()        - every pass of the physics loop, including the passes that simulate
                              nothing because the sim is paused. For work whose job is to LOOK at
                              the simulation rather than to be part of it: moving the camera,
                              picking, editor gizmos, keeping a debug marker on something. It must
                              not change anything a tick will read - it runs a number of times that
                              depends on loop pacing and on how long the game sat paused, so
                              anything it writes into the simulation is unreproducible by
                              construction.

        RunSimulationTick() - exactly once per tick that actually runs, immediately before
                              Scene::UpdatePhysics steps the world. Gameplay goes here. It does not
                              run while paused, and single-stepping runs it exactly once per step,
                              so what it does is paused, stepped and replayed along with the physics.

        These replace the old RunLogic(), which was documented as the per-tick hook but was really
        called on every pass - so gameplay written the obvious way kept playing through a pause, and
        single-stepping advanced it by loop iterations rather than by ticks.

        The test for which one a piece of code belongs in: if running it twice for a single tick
        would change the outcome, it is simulation, and it goes in RunSimulationTick.
    */
    virtual void UpdateView(void);
    virtual void RunSimulationTick(void);

    virtual void UpdatePhysics(void);
    virtual void NextInput(void);

    //Frame thread
    /*
        Called at the top of DrawFrame, on the FRAME THREAD, before the scene renders anything.
        Empty by default.

        The hook exists because an app sometimes has GL work that has to happen before the colour
        pass and cannot be hung off a shader's uniform_callback - that fires while the shader is
        already bound, mid-pass, which is too late to fill a texture the pass will sample. The
        ship app builds its cloud shadow map here. UpdateView is NOT the place, close as it sounds:
        it runs on the physics thread, which may not touch GL at all.
    */
    virtual void PreRender(void){};
    virtual void DrawFrame(void);
    virtual void DrawImGuiUI(void);

    int Exit(void);

    DWORD thread_id_main = -1;
    DWORD thread_id_render = -1;
    DWORD thread_id_physics = -1;

    Window* main_window = NULL;
    Renderer* renderer = NULL;
    Shader* default_shader = NULL;
    std::vector<Scene*> scenes;         // List of different scenes this application owns.
    Scene* main_scene = NULL;           // Currently active scene.
    AssetManager* assetmanager = NULL;
    GLTFLoader gltfloader;              // We can only have a single GLTF loader for now

    //Performance timers
    PerfTimer* tmr_physics = NULL;          // Used for timing how long the physics calculations take
    PerfTimer* tmr_physics_loop = NULL;     // Time an entire physics loop took (sleeping+calculating+overhead)
    PerfTimer* tmr_physics_sleep = NULL;    // Time physics took sleeping in order to achieve desired rate

    PerfTimer* tmr_render_loop = NULL;      // Used for timing how long the entire render loop costs, should yield FPS.

    RRandom* rrand = NULL;

    //Keyboard/mouse acquisition on its own thread. Started in Start(); stops itself on destruction.
    RawInputSource raw_input;

    //Physics Settings
    float physics_tps           = 50.0f; //Target physics ticks per second
    double physics_us_per_tick  = 20000.0f;
    //Speeds up or slows down physics globally by running ticks more or less OFTEN. It must never
    //scale the timestep: a simulation stepped with a varying dt is not reproducible, and the goal
    //is that a recorded run replays to the identical state. Slow motion means fewer ticks per
    //second of real time, each still exactly GetPhysicsTimestep() long.
    float physics_time_factor   = 1.0f;
    void SetPhysicsTPS(float tps){
        physics_tps = tps;
        physics_us_per_tick = 1000000.0f / physics_tps;
    }

    //The one and only simulation timestep, in seconds. Constant for the life of the run - every
    //caller of Scene::UpdatePhysics/UpdateAnimations passes this, nothing computes its own.
    float GetPhysicsTimestep() const { return 1.0f / physics_tps; }


    //Generic Object placement and selection
    Object* selected_object = NULL;
    Object* hovered_object = NULL;
    plane projection_plane;

    int2 GetDisplaySettings();

    Debugger *debug_physics = NULL;
    Debugger *debug_frame = NULL;

    Scene* CreateNewScene(const std::string& name);    // Creates a new scene, with some defaults.

    //One liners that do many things
    Object* CreateNewObjectFromGLTF(const std::string& nodename, Scene* target_scene);
    void GetAllAssetsFromGLTF();
    void GetAssetsFromGLTF(const std::vector<std::string>& names);
    template<typename... Args>
    void GetAssetsFromGLTF(Args... names){
        GetAssetsFromGLTF(std::vector<std::string>{ std::string(names)... });
    }
    void BuildSceneFromJSON();

protected:
    // Two main threads
    static DWORD WINAPI FrameThreadFunction(LPVOID lpParameter);
    static DWORD WINAPI PhysicsThreadFunction(LPVOID lpParameter);

    objectid_t hovered_objid = OBJECTID_INVALID;
    objectid_t dragged_objid = OBJECTID_INVALID;
    void CheckObjectSelection();

    //--- Debug UI ---------------------------------------------------------------------------
    //RenderApplicationUI is still the single call an app's DrawImGuiUI makes; it now hosts a
    //dockspace and the three windows below rather than being one panel of its own. See the
    //section comment in Application.cpp for why it was split up and which controls go through
    //the SimCommand queue.
    void RenderApplicationUI();
    void RenderSceneWindow();
    void RenderInspectorWindow();
    void RenderEngineWindow();

    //One per Inspector tab. Each takes the object rather than reading selected_object, so a tab
    //cannot disagree with the header about what it is showing.
    void RenderInspectorTransformTab(Object* object);
    void RenderInspectorPhysicsTab(Object* object);
    void RenderInspectorRenderTab(Object* object);
    void RenderInspectorAnimationTab(Object* object);
    void RenderInspectorDebugTab(Object* object);
    //Eight checkboxes for an 8-bit collision mask. Returns true (and writes `mask`) on a change.
    bool RenderBitmaskCheckboxes(const char* id, uint32_t& mask);

    //Which of the three windows are up. Toggled from the menu bar's View menu; an app may also
    //set them before/inside Init() if it wants a different default.
    bool f_show_scene_window = true;
    bool f_show_inspector_window = true;
    bool f_show_engine_window = true;

    void UpdateUICameraControls(Camera* camera, int id);
    virtual void RenderDebugMenuBarClass(void);
    void RenderDebugMenuBar();
    void RenderRandTestWindow();
    void RenderShaderUI(Shader* shader);
    void UpdateUIWorldPhysics(PhysicsWorld* physics_world);
private:
    bool SetupConsole();
    static bool WINAPI ConsoleHandler(DWORD console_event);
    void UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked);
};

#endif

#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <stdint.h>
#include <mutex>
#include <atomic>
//app_name is a std::string. Window.h happens to pull this in too, but an include that is only
//there by transit is one refactor away from not being there at all.
#include <string>
#include "Window.h"
#include "Renderer.h"
#include "Shader.h"
//Included rather than forward-declared: an app overriding DrawOverlay needs the Add* calls, so a
//forward declaration would only make every one of them include this itself. Renderer.h has
//already pulled in glad by this point, so it costs nothing.
#include "UIOverlay.h"
#include "Scene.h"
#include "PerfTimer.h"
#include "AssetManager.h"
#include "Debug.h"
#include "GLTFLoader.h"
#include "RRandom.h"
//NO ObjectCollider.h. Nothing in this header names the type - the one use in the whole engine
//is the gizmo Application.cpp spawns for SIMCMD_ADD_COLLIDER_GIZMO, so the include lives there
//instead. It mattered because ObjectCollider holds an rp3d::Collider* and this header is
//reached by every app: dragging reactphysics3d's headers into all fifteen of them, including
//the ones built with USE_PHYSICS=0, for a class none of them mention.
#include "RawInput.h"
#include "skeleton/PlayerCharacter.h"
//For MaybeAttachScreenshot's signature below. json.hpp only (not MCPServer.h): this is pure C++
//with no winsock in it, so it sidesteps the include-order trap MCPServer.h documents.
#include "tinygltf/json.hpp"
using json = nlohmann::json;

/*
    THE ON-SCREEN TOUCH UI, AND THE ONE LINE THAT TURNS IT ON.

    A phone or tablet has nothing else to play with, so the button clusters are the only controls
    there. A desktop has a keyboard, a mouse and a gamepad already, so the same buttons are dead
    weight on top of the game - they cover the corners, they hit-test every pointer press, and
    nobody presses them. So they are ANDROID-ONLY by default.

    This is deliberately ONE #if, in one file, rather than a per-app setting: checking how the
    touch layout looks on Windows is a thing you want to do while working on the layout, and it
    should cost changing this line to `#if 1` and rebuilding - not finding five places.

    It gates BINDING as well as drawing, in the apps as well as here, and that is the part not to
    get wrong: a button that is bound but not drawn is still live at its rect, so an invisible
    DROP in the corner of a desktop window would eat clicks and there would be nothing on screen
    to explain why. Backlog item 67 is the same failure found the other way round.
*/
#if defined(__ANDROID__)
    #define USE_TOUCH_UI 1
#else
    #define USE_TOUCH_UI 1
#endif

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
    //asset spawning. Registered right after Init(), for the same reason RegisterCoreMCPTools is:
    //the scenes have to exist. It lives on Application rather than on Scene because spawning needs
    //the AssetManager, which Scene has no business knowing about - and keeping Scene as pure
    //queue-and-dispatch is what lets an app register handlers that close over its own types.
    //
    //The no-argument form does EVERY scene the app built, which is what makes a second scene work
    //at all - see the definition.
    void RegisterCoreCommandHandlers();
    void RegisterCoreCommandHandlers(Scene* scene);

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

    /*
        Generic, app-independent MCP tools (object_list/object_get/object_set_transform/
        object_move/screenshot/shader_reload/...) - the MCP counterpart of the Generic Object UI
        panel. Registered for every app right after Init(), before the MCP server starts accepting
        requests.

        THESE TWO AND MaybeAttachScreenshot BELOW ARE DECLARED UNCONDITIONALLY and defined in one
        of a swappable PAIR of translation units: core/ApplicationMCP.cpp with USE_MCP=1, and
        core/ApplicationMCP_none.cpp - where all three do nothing - with USE_MCP=0. So this header
        is the same for every app whatever the flag says, which is what keeps the shared core
        objects shareable. See the note at the top of core/ApplicationMCP.cpp.
    */
    void RegisterCoreMCPTools();

    //Starts both MCP transports, once the app's own Init() has registered its tools. A wrapper
    //rather than a direct MCPServer call so that Application.cpp never names that class - with
    //USE_MCP=0 it is not compiled at all.
    void StartMCPServer();

    /*
        Recompile every registered Shader whose vertex or fragment file name contains
        `name_filter`, and block until the render thread has done it. Returns one entry per
        shader touched, each with its compile log - see the `shader_reload` MCP tool.

        THE WAIT IS THE WHOLE DESIGN. Reloading touches GL, which only the render thread may do,
        so this raises a flag that ServiceShaderReload picks up at the top of the next frame. The
        caller then has to wait, or it would be reporting that a reload was ASKED FOR rather than
        what the compiler said about it - and the compiler's answer is the entire value of the
        call. Same shape as StepPhysicsAndWait, for the same reason.

        ONLY callable from a thread that is not the render thread. The MCP threads qualify. A call
        from DrawFrame or an ImGui panel would wait for a frame that cannot start until it returns.
    */
    json ReloadShadersAndWait(const std::string& name_filter, int timeout_ms = 2000);

    //Serviced at the top of DrawFrame, on the render thread, and a cheap no-op unless a reload has
    //been asked for. See ReloadShadersAndWait.
    void ServiceShaderReload();

    //If requested, blocks (Renderer::RequestScreenshot) until the render thread has captured and
    //PNG-encoded the current frame, and attaches it to `result` as an MCP image content block. A
    //no-op passthrough otherwise, so any MCP tool in any app can offer a screenshot with one line:
    //  return MaybeAttachScreenshot(MyTelemetry(),args.value("include_screenshot",false));
    //A failure is reported in a "screenshot_error" field rather than replacing the result, since
    //the caller asked for the telemetry first and the picture second. Lives here rather than per
    //app because nothing about it is app-specific - it was written twice before it was moved.
    //The `screenshot` core MCP tool is this with an empty result.
    //include_ui defaults to true: the debug UI is ImGui and exists nowhere else, so a capture
    //without it silently drops every readout, inspector field and button from the one caller that
    //cannot look at the monitor. Pass false for the clean 3D scene.
    json MaybeAttachScreenshot(json result, bool include_screenshot, bool include_ui = true);

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
    //Runs only on a pass that ticks, before UpdateAnimations, so that an edge a scripted hold
    //raises is observable by RunSimulationTick instead of being cleared unseen. See
    //InputController::ApplyTickInput and backlog item 84.
    virtual void UpdateTickInput(void);
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
    /*
        End of a physics pass: clears the per-pass input transition flags.

        `f_ticked` is whether this pass actually ran a tick, and passing it is what makes an
        edge-triggered action survive a paused pass - backlog item 88. An edge nobody has read
        yet is kept until a ticking pass has had its chance at it; see InputController::Tick,
        which carries the rule and the reasoning.
    */
    virtual void NextInput(bool f_ticked);

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

    /*
        Screen-space 2D - rounded rects and text - for an app's own HUD and, later, the on-screen
        buttons. RENDER THREAD, called between the scene and the ImGui panels, with `overlay`
        already Begin()'d at the window's size: an override just calls overlay->AddRect/AddText
        and returns. DrawFrame submits the batch.

        DRAWN UNDER ImGui, deliberately. This is the app's UI and ImGui's windows are the debug
        panels on top of it, which is the right way round while both exist - and in a shipping
        build (item 82, USE_IMGUI=0) the question disappears with the panels. The exception to
        watch for is the touch buttons: item 67 records that a button cluster drawn UNDER an
        app's own HUD is live and invisible, which is nastier than being drawn over, so when
        those move here they may want to be last rather than first.
    */
    virtual void DrawOverlay(void){};

    /*
        Draws InputController's on-screen button rects through `overlay`. Core-owned, because the
        rects are, and drawn AFTER DrawOverlay so a cluster cannot end up beneath an app's own HUD
        - item 67 records that as the nastier failure, since the buttons hit-test their own rect
        list and would still be live while invisible.

        Override to draw them as artwork instead, or clear f_draw_touch_buttons for an app whose
        layout is swipe-only and draws nothing. Either way input is unaffected: the rect list and
        SubmitPointer do not know or care whether anything drew.
    */
    virtual void DrawTouchButtons(void);
    bool f_draw_touch_buttons = true;

    /*
        Positions the on-screen buttons for a surface of `w` x `h` pixels. Called on the render
        thread before the first frame and again whenever the size changes - so an app does its
        layout arithmetic here and nowhere else, and never has to find out when the size is final.

        THAT LAST PART IS WHY THIS EXISTS RATHER THAN THE LAYOUT LIVING IN SetupInput. The size is
        NOT final during Init: ApplicationTetris::Init calls SetupInput and then resizes its own
        window, so a layout computed in SetupInput is built against 1280x800 for a window that
        becomes 1200x900, and the right-hand buttons land off the edge. That was real, not
        hypothetical. A user dragging the window, or an Android orientation change, is the same
        bug arriving later.

        Bind the buttons in SetupInput (AddTouchButton, which allocates the keycodes once) and
        move them here (SetTouchButtonRect). See those two for why identity and geometry are
        separated.
    */
    virtual void LayoutTouchButtons(int w, int h){ (void)w; (void)h; };

    int Exit(void);

    DWORD thread_id_main = -1;
    DWORD thread_id_render = -1;
    DWORD thread_id_physics = -1;

    Window* main_window = NULL;
    Renderer* renderer = NULL;
    //The 2D overlay, created on the render thread after the app's Init() - see UIOverlay.h. Never
    //NULL once the frame thread is running, but IsReady() is false if the font or shader is
    //missing, in which case every Add* is a no-op rather than a crash.
    UIOverlay* overlay = NULL;
    //Surface size the touch-button layout was last computed for, so DrawFrame can notice a
    //resize. -1 forces LayoutTouchButtons to run before the first frame.
    int touch_layout_w = -1;
    int touch_layout_h = -1;
    Shader* default_shader = NULL;
    std::vector<Scene*> scenes;         // List of different scenes this application owns.
    Scene* main_scene = NULL;           // Currently active scene. Physics thread swaps it; see
                                        // ApplyPendingSceneSwitch before writing it directly.
    //A switch asked for from another thread, waiting to be applied. Atomic because the asking and
    //the applying are on different threads and nothing else synchronises them.
    std::atomic<Scene*> pending_scene{NULL};
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

    /*
        What this application calls itself. Start() hands it to the window, so it is what shows in
        the title bar and on the taskbar instead of the old hardcoded "Normal Window".

        SET IT IN THE APP'S CONSTRUCTOR - Start() creates the window, and a subclass constructor
        has already run by then. Assigning it later changes nothing on its own; that is what
        Window::SetTitle is for.

        Keep it distinct from any ImGui window title the app uses. Two ImGui::Begin() calls with
        the same string are ONE window, so an app named "Tetris" that also does Begin("Tetris")
        for its HUD would find the two merged into a single scrolling panel.
    */
    std::string app_name = "Application";

    //The one and only simulation timestep, in seconds. Constant for the life of the run - every
    //caller of Scene::UpdatePhysics/UpdateAnimations passes this, nothing computes its own.
    float GetPhysicsTimestep() const { return 1.0f / physics_tps; }

    /*
        THE DEVICE'S OWN MEDIA VOLUME, not a gain applied to our mixer.

        SoundSystem's per-voice gain and an app's mute flag both sit DOWNSTREAM of this: if the
        output stream is at 0 the game is silent however loud it asks to be. Worth having as a
        separate control because the two failures are indistinguishable to the player - a muted
        game on a device at volume 0 and a game at full gain on a device at volume 0 sound
        exactly alike, and only one of them is a setting they can find.

        GetSystemVolume returns the current step and GetMaxSystemVolume the top of the scale,
        which is PER-DEVICE and must never be assumed - so a readout shows both, or it does not
        say how loud the number is. SetSystemVolume clamps to [0,max].

        All three return -1 / do nothing when there is no control to reach, which is what the
        win32 build does today. Callers must treat -1 as "no volume control on this platform"
        and HIDE their UI rather than drawing one that does nothing: see ApplicationTetris,
        which never allocates its V-/V+ buttons when max comes back negative.
    */
    int  GetSystemVolume() const;
    int  GetMaxSystemVolume() const;
    void SetSystemVolume(int volume);


    //Generic Object placement and selection
    Object* selected_object = NULL;
    Object* hovered_object = NULL;
    plane projection_plane;

    int2 GetDisplaySettings();

    Debugger *debug_physics = NULL;
    Debugger *debug_frame = NULL;

    //--- shader_reload, across the thread boundary --------------------------------------------
    //Written by whichever thread asked for the reload and by the render thread that services it,
    //so everything here is under the one mutex. f_pending is the handshake: raised by the asker,
    //cleared by the render thread once the result is in place.
    std::mutex shader_reload_mutex;
    std::string shader_reload_filter;
    bool f_shader_reload_pending = false;
    json shader_reload_result;

    Scene* CreateNewScene(const std::string& name);    // Creates a new scene, with some defaults.

    //Switching which scene is simulated and drawn. See the comments on the definitions: the
    //request is safe from any thread, the swap itself only happens on the physics thread.
    void RequestActiveScene(Scene* scene);
    void ApplyPendingSceneSwitch();                    // physics thread only, physics_mutex held
    Scene* GetActiveScene(){ return main_scene; }

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

    /*
        Does the debug UI want the mouse, or the keyboard?

        ImGui::GetIO().WantCaptureMouse behind a name every build has. Game logic asks before
        acting on a click so that dragging a slider does not also swing the camera - apps/ship
        and apps/tank both do. With USE_IMGUI=0 the answer is always false and the click belongs
        to the game, which is what lets those call sites stay unguarded.

        Defined, like everything else declared around here, in whichever of
        core/ApplicationDebugUI.cpp / ApplicationDebugUI_none.cpp this build compiled.
    */
    bool UIWantsMouse();
    bool UIWantsKeyboard();

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
#ifdef USE_PHYSICS
    void RenderInspectorPhysicsTab(Object* object);
#endif
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
    //Takes a PhysicsWorld* by name, so it exists only where that type does. The USE_IMGUI=0 twin
    //in ApplicationDebugUI_none.cpp carries the same guard.
#ifdef USE_PHYSICS
    void UpdateUIWorldPhysics(PhysicsWorld* physics_world);
#endif
private:
    bool SetupConsole();
    static bool WINAPI ConsoleHandler(DWORD console_event);
    void UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked);
};

#endif

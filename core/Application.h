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

    //Generic, app-independent MCP tools (object_list/object_get/object_set_transform/
    //object_move) - the MCP counterpart of the Generic Object UI panel. Registered for
    //every app right after Init(), before the MCP server starts accepting requests.
    void RegisterCoreMCPTools();

    //The point the app's camera orbits and zooms around, if it has one. Every app subclass keeps
    //its own `camera_target` (a copy-pasted field, not shared state), so the core camera MCP
    //tools cannot see it directly - this is the one-line opt-in that lets them read and write it.
    //Returning NULL just means camera_target is absent from those tools' view for this app.
    virtual vec3* GetCameraTargetPtr(){ return NULL; }

    //Physics thread
    virtual void UpdateInput(void);
    virtual void UpdateAnimations(void);
    virtual void RunLogic(void);
    virtual void UpdatePhysics(void);
    virtual void NextInput(void);

    //Frame thread
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
    //UI
    void UpdateUICameraControls(Camera* camera, int id);
    virtual void RenderDebugMenuBarClass(void);
    void RenderDebugMenuBar();
    void RenderRandTestWindow();
    void RenderShaderUI(Shader* shader);
    void RenderApplicationUI();
    void RenderSelectedObjectUI(Object* objec, int ui_camera_id);

    void UpdateUIWorldPhysics(PhysicsWorld* physics_world);
    void UpdateUIPhysics(Physics* world_physics);
private:
    bool SetupConsole();
    static bool WINAPI ConsoleHandler(DWORD console_event);
    //UI
    void UpdateUISceneObjectTree(Scene* scene);
    void UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked);
};

#endif

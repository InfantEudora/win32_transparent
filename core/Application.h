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

    //Physics Settings
    float physics_tps           = 50.0f; //Target physics ticks per second
    double physics_us_per_tick  = 20000.0f;
    float physics_time_factor   = 1.0f; //Used to speed up or slow down physics globally
    void SetPhysicsTPS(float tps){
        physics_tps = tps;
        physics_us_per_tick = 1000000.0f / physics_tps;
    }


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

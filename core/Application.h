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

/*
    The thing that ties everything together.

    I guess we need to provide a basic implementation,
    but since there will be many different types of example applications and tests...
    It's hard to come up with anything generic right now.
    Most of the things needs some kind of specific order: Frame, input, physics.

    We'd also like an application that isn't so much a game, but only a UI from maybe ImGui or the default windows one.

*/
class Application{
public:
    Application();

    virtual void Run(void);
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

    PerfTimer* tmr_physics = NULL;

    //Generic Object placement and selection
    Object* selected_object = NULL;
    Object* hovered_object = NULL;
    plane projection_plane;

    virtual void RunLogic();

    int2 GetDisplaySettings();

    Debugger *debug_physics = NULL;
    Debugger *debug_frame = NULL;

    Scene* CreateNewScene(const std::string& name);    // Creates a new scene, with some defaults.

protected:
    static DWORD WINAPI PhysicsThreadFunction(LPVOID lpParameter);
    void CheckObjectSelection();
    //UI
    void UpdateUICameraControls(Camera* camera, int id);
    void RenderDebugMenuBar();
    void RenderGenericObjectUI();
    void UpdateUIWorldPhysics(PhysicsWorld* physics_world);
    void UpdateUIPhysics(Physics* world_physics);

private:
    bool SetupConsole();
    static bool WINAPI ConsoleHandler(DWORD console_event);
    static DWORD WINAPI FrameThreadFunction(LPVOID lpParameter);
    //UI
    void UpdateUISceneObjectTree();
    void UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked);
};

#endif

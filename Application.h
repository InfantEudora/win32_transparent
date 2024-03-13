#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <windows.h>
#include <stdint.h>
#include "Window.h"
#include "Renderer.h"
#include "Shader.h"
#include "Scene.h"

/*
    The thing that ties everything together.

    I guess we need to provide a basic implementation,
    but since there will be many different types of example applications and tests...
    It's hard to come up with anything generic right now.
    Most of the things needs some kind of specific order: Frame, input, physics.
*/
class Application{
public:
    Application();

    void Run(void);
    int Exit(void);

    DWORD thread_id_main = -1;
    DWORD thread_id_render = -1;
    DWORD thread_id_physics = -1;

    Window* main_window = NULL;
    Renderer* renderer = NULL;
    Shader* default_shader = NULL;
    Scene* main_scene = NULL;

private:
    bool SetupConsole();
    static bool WINAPI ConsoleHandler(DWORD console_event);
    static DWORD WINAPI FrameThreadFunction(LPVOID lpParameter);
    static DWORD WINAPI PhysicsThreadFunction(LPVOID lpParameter);
};

#endif

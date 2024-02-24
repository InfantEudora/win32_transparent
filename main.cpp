
#include <tchar.h>

#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Debug.h"
#include "Window.h"
#include "Shader.h"
#include "Renderer.h"
#include "glad.h"

#pragma comment(lib, "opengl32.lib")

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

BOOL WINAPI ConsoleHandler(DWORD console_event){
    switch(console_event){
        case CTRL_C_EVENT:
            debug->Ok("Shutting down by CTRL+C\n");
            ExitProcess(1);
        break;
    }
    return true;
}

void SetVSync(bool enable){
    if (wglSwapIntervalEXT){
        wglSwapIntervalEXT(enable);
        if (enable){
            debug->Ok("VSync: Enabled\n");
        }else{
            debug->Ok("VSync: Disabled\n");
        }
    }
}

//Function for a thread
DWORD WINAPI ThreadFunction(LPVOID lpParameter){
    DWORD thread_id = GetCurrentThreadId();
    debug->Info("Output from Thread ID: %lu\n",thread_id);

    Window* wind = Window::CreateNewLayeredWindow(256,256,&Window::wcs.at(0));
    if (!wind){
        debug->Fatal("Unable to create normal window\n");
    }
    if (!wind->Init()){
        return false;
    }

    Shader* shader = new Shader("default.vert","default.frag");
    Renderer* renderer = new Renderer(256,256);
    renderer->Init();

    wind->Show(SW_SHOWDEFAULT);
    MSG msg = {0};
    while (wind->f_should_quit == false){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }else{
            renderer->DrawFrame(shader);
            wind->DrawFrame();
        }
    }
    debug->Info("Thread terminated\n");
    return 0;
}

Window* wind_frame = NULL;
Shader* shader_frame = NULL;
Renderer* renderer_frame = NULL;

//Function for rendering the frame to a window
DWORD WINAPI FrameFunction(LPVOID lpParameter){
    DWORD thread_id = GetCurrentThreadId();
    debug->Info("Output from FrameFunction Thread ID: %lu\n",thread_id);

    Window* wind = wind_frame;
    Renderer* renderer = renderer_frame;
    Shader* shader = shader_frame;

    //We make the window's context current to this thread
    if (!wglMakeCurrent(wind->hDC, wind->hRC)){
        debug->Info("FrameFunction Thread unable to get Context\n");
        return false;
    }

    while (wind->f_should_quit == false){
        renderer->DrawFrame(shader);
        wind->DrawFrame();
    }
    debug->Info("Thread terminated\n");
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Used to do things from console, like CTRL+C
    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler,TRUE)==FALSE){
        debug->Err("Unable to install a console handler!\n");
    }

    int num_threads = 0;

    DWORD main_id = GetCurrentThreadId();
    debug->Info("WinMain Thread ID: %lu\n",main_id);

    Window::RegisterWindowClasses();

    Window* wind = Window::CreateNewLayeredWindow(512,512,&Window::wcs.at(0));
    if (!wind){
        debug->Fatal("Unable to create window\n");
    }
    if (!wind->Init()){
        debug->Fatal("Failed to init window\n");
    }

    //Now we can make shaders
    Shader* shader = new Shader("default.vert","default.frag");
    Renderer* renderer = new Renderer(512,512);
    renderer->Init();

    for (int i =0;i<num_threads;i++){
        HANDLE hThread = NULL;
        DWORD thread_id;
        // Create a new thread which will get it's own render context
        hThread = CreateThread(
            NULL,    // Thread attributes
            0,       // Stack size (0 = use default)
            ThreadFunction, // Thread start address
            NULL,    // Parameter to pass to the thread
            0,       // Creation flags
            &thread_id);   // Thread id

        if (hThread == NULL){
            debug->Fatal("Unable to create thread\n");
        }
    }

    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    wind->Show(SW_SHOWDEFAULT);
    SetVSync(true);

    wind_frame = wind;
    shader_frame = shader;
    renderer_frame = renderer;

    //We release the window's context from this thread
    wglMakeCurrent(wind->hDC, NULL);

    if (1){
        HANDLE hThread = NULL;
        DWORD thread_id;
        // Create a new thread which will get this one's render context
        hThread = CreateThread(
            NULL,    // Thread attributes
            0,       // Stack size (0 = use default)
            FrameFunction, // Thread start address
            NULL,    // Parameter to pass to the thread
            0,       // Creation flags
            &thread_id);   // Thread id

        if (hThread == NULL){
            debug->Fatal("Unable to FrameFunction thread\n");
        }
    }


    MSG msg = {0};
    while (wind->f_should_quit == false){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }else{
            Sleep(1);
        }
    }

    return 0;
}
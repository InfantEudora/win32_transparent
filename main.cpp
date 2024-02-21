
#include <tchar.h>

#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Debug.h"
#include "Window.h"
#include "glad.h"

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

    Window* wind = Window::CreateNewLayeredWindow(128,128,&Window::wcs.at(0));
    if (!wind){
        debug->Fatal("Unable to create normal window\n");
    }
    if (!wind->Init()){
        return false;
    }

    wind->Show(SW_SHOWDEFAULT);
    MSG msg = {0};
    while (wind->f_should_quit == false){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }else{
            wind->DrawFrame();
        }
    }
    debug->Info("Thread terminated\n");
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Used to do things from console, like CTRL+C
    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler,TRUE)==FALSE){
        debug->Err("Unable to install a console handler!\n");
    }

    DWORD main_id = GetCurrentThreadId();
    debug->Info("WinMain Thread ID: %lu\n",main_id);

    Window::RegisterWindowClasses();

    for (int i =0;i<1;i++){
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

    Window* wind = Window::CreateNewLayeredWindow(256,256,&Window::wcs.at(0));

    if (!wind){
        debug->Fatal("Unable to create window\n");
    }

    if (wind->Init()){
        wind->Show(SW_SHOWDEFAULT);
        SetVSync(true);
        MSG msg = {0};
        while (wind->f_should_quit == false){
            if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
                if (msg.message == WM_QUIT)
                    break;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }else{
                wind->DrawFrame();
            }
        }
    }

    //Cleanup();
    //UnregisterClass(wind->wc.lpszClassName, hInstance);

    return 0;
}
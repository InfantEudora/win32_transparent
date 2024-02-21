
#include <tchar.h>

#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Debug.h"
#include "Window.h"
#include "glad.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

//Window everything will be drawn in.
Window* wind = NULL;
Window* wind_norm = NULL;

BOOL WINAPI ConsoleHandler(DWORD console_event){
    switch(console_event){
        case CTRL_C_EVENT:
            debug->Ok("Shutting down by CTRL+C\n");
            if (wind){
                wind->Close();
            }
        break;
    }
    return true;
}


/* Clean it up your self.
void Cleanup()
{
    if (g_textureId)
    {
        glDeleteTextures(1, &g_textureId);
        g_textureId = 0;
    }

    if (g_hPBuffer)
    {
        wglDeleteContext(g_hPBufferRC);
        wglReleasePbufferDCARB(g_hPBuffer, g_hPBufferDC);
        wglDestroyPbufferARB(g_hPBuffer);
        g_hPBufferRC = 0;
        g_hPBufferDC = 0;
        g_hPBuffer = 0;
    }

    if (wind->hDC){
        if (g_hRC){
            wglMakeCurrent(wind->hDC, 0);
            wglDeleteContext(g_hRC);
            g_hRC = 0;
        }

        ReleaseDC(wind->hWnd, wind->hDC);
        wind->hDC = 0;
    }

    ImageDestroy(&g_image);
}*/



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

    wind_norm = Window::CreateNewLayeredWindow(256,256,&Window::wcs.at(0));
    if (!wind_norm){
        debug->Fatal("Unable to create normal window\n");
    }
    if (!wind_norm->Init()){
        return false;
    }

    wind_norm->Show(SW_SHOWDEFAULT);
    MSG msg = {0};
    while (wind_norm->f_should_quit == false){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }else{
            wind_norm->DrawFrame();

            //Sleep(1000);
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





    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    wind = Window::CreateNewLayeredWindow(256,256,&Window::wcs.at(0));




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
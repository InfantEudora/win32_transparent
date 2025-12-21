#include <winsock2.h>
#include <ws2tcpip.h>

#include <tchar.h>

#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Debug.h"

//Would be the name of the application
#include APP_HEADER

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    Application* main_app = new APP_CLASS();
    main_app->Start();
    return main_app->Exit();
}

#include <tchar.h>

#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Application.h"
#include "Debug.h"
#include "Window.h"
#include "Shader.h"
#include "Renderer.h"
#include "Scene.h"
#include "Asset.h"
#include "glad.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    Application* main_app = new Application();
    main_app->Run();
    return main_app->Exit();
}
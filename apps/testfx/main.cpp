#include <winsock2.h>
#include <ws2tcpip.h>

#include <tchar.h>

#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Debug.h"
#include "File.h"

//Required for NVidia Optimus to use the discrete GPU on laptops with integrated graphics. Put this in the main.cpp of your application, and it will be picked up by the driver.
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }

//The app's class, by name - see the note in apps/tank/main.cpp for why each app has its own main
//instead of one shared one reaching the class through -DAPP_HEADER/-DAPP_CLASS.
#include "ApplicationTestFX.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        This app's own root first, then the shared one, the same order the ship uses.

        The order is doing real work here rather than being tidiness: every effect this bench
        compiles lives in the "shaders/" category, right alongside the engine's own
        shaders/default.vert and shaders/default.frag. Its own root coming first is what lets
        "shaders/fullscreen.vert" resolve here while "shaders/default.frag" - which the scene's
        test geometry still needs - falls through to shared_assets.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.
    */
    AddAssetSearchRootFromExe("../assets");                 //apps/testfx/assets
    AddAssetSearchRootFromExe("../../../shared_assets");    //the engine's shared shaders and fonts

    Application* main_app = new ApplicationTestFX();
    main_app->Start();
    return main_app->Exit();
}

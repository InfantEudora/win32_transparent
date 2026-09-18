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
#include "ApplicationBomber.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        This app's own root first, then the shared one, which is the order every app uses and
        which is doing real work here rather than being tidiness: bomber's own
        shaders/bomber_explosion.frag has to resolve from this folder while
        shaders/default.vert - which it reuses as its vertex stage - and shaders/noise3d.comp -
        which fills its 3D noise, and which the ship app uses too - fall through to
        shared_assets.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.

        NOT DECLARED AT ALL IN A BAKED BUILD (BAKE_ASSETS=1, which defines ASSETS_BAKED - see this
        app's makefile and the BAKED ASSETS block in engine.mk). LoadFile asks the baked table
        before it touches the search path, so with everything baked a root can only ever be
        consulted for a name that is going to fail anyway; leaving it in would make the log claim
        a search path the process never uses, and would hide an incomplete bake behind a disk that
        happens to be there. The ordinary build is unaffected.
    */
#ifndef ASSETS_BAKED
    AddAssetSearchRootFromExe("../assets");                 //apps/bomber/assets
    AddAssetSearchRootFromExe("../../../shared_assets");    //default shaders, noise3d.comp, fonts
#endif

    Application* main_app = new ApplicationBomber();
    main_app->Start();
    return main_app->Exit();
}

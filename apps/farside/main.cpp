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
#include "ApplicationFarside.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        This app's own root first, then the ship app's, then the shared one. Relative to the
        EXECUTABLE, not the working directory - see GetExecutableDirectory in core/File.h.

        THE SHIP APP'S ROOT IS BORROWED, for meshes/ships.glb (the player ship, the hulls and the
        asteroids). The rule is that an asset two apps use moves to shared_assets, but a baked
        build packs EVERY file under shared_assets (ASSET_ROOTS in engine.mk), so 12 MB of glb there
        would grow every app's shipped exe. While Farside is a bench that never ships, reading it
        from where it lives is the cheaper honest choice; when Farside gets its own models this
        line goes.
    */
    AddAssetSearchRootFromExe("../assets");                 //apps/farside/assets
    AddAssetSearchRootFromExe("../../ship/assets");         //meshes/ships.glb, see above
    AddAssetSearchRootFromExe("../../../shared_assets");    //the engine's shaders and fonts

    Application* main_app = new ApplicationFarside();
    main_app->Start();
    return main_app->Exit();
}

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
#include "ApplicationTetris.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        ONE ROOT, AND NO apps/tetris/assets AT ALL - Tetris is the app that owns nothing.

        Every file it loads is shared: the default shaders and the field shaders from core, the
        font, and the glyph mesh and four sound effects it has in common with Breakout. So there
        is no second root to declare, and declaring one for a folder that does not exist would
        only put a line in the log that reads like a mistake.

        If Tetris ever gains an asset of its own, this is a one-liner - add
        AddAssetSearchRootFromExe("../assets") ABOVE the shared root, so the app's own copy wins.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.
    */
    AddAssetSearchRootFromExe("../../../shared_assets");

    Application* main_app = new ApplicationTetris();
    main_app->Start();
    return main_app->Exit();
}

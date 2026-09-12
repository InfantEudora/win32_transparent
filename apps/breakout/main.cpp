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
#include "ApplicationBreakout.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        Breakout owns exactly one asset - the shield shader. Its glyph mesh and its four sound
        effects are shared with Tetris and live in shared_assets, which is the whole reason that
        folder exists: not everything one app loads is that app's.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.
    */
    AddAssetSearchRootFromExe("../assets");                 //apps/breakout/assets
    AddAssetSearchRootFromExe("../../../shared_assets");    //glyphs, sounds, default shaders, fonts

    Application* main_app = new ApplicationBreakout();
    main_app->Start();
    return main_app->Exit();
}

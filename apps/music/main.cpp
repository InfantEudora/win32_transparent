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
#include "ApplicationMusic.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    debug->Info("nShowCmd = %i\n",nShowCmd);

    /*
        This app's own root first - the score and the exported samples - then the shared one for
        the engine's shaders and fonts. Relative to the EXECUTABLE, not the working directory; see
        GetExecutableDirectory in core/File.h. Not declared in a baked build, where LoadFile answers
        every name from the table - see apps/ui/main.cpp for why that matters.
    */
#ifndef ASSETS_BAKED
    AddAssetSearchRootFromExe("../assets");                 //apps/music/assets
    AddAssetSearchRootFromExe("../../../shared_assets");
#endif

    Application* main_app = new ApplicationMusic();
    main_app->Start();
    return main_app->Exit();
}

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

//The app's class, by name - see the note in apps/tank/main.cpp for why each app has its own main.
#include "ApplicationChasm.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    debug->Info("nShowCmd = %i\n",nShowCmd);

    /*
        Only the shared root for now: everything chasm draws is generated, and it has no assets of
        its own until the palette texture (step 4). Its own root goes FIRST when it does, as in
        every other app, so it can override a shared file. Not declared in a baked build - see the
        same block in apps/bomber/main.cpp.
    */
#ifndef ASSETS_BAKED
    AddAssetSearchRootFromExe("../../../shared_assets");
#endif

    Application* main_app = new ApplicationChasm();
    main_app->Start();
    return main_app->Exit();
}

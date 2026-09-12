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
#include "ApplicationOCPP.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        OCPP owns the browser UI it serves - assets/www. That page is loaded by core/HTTPServer
        rather than by this app directly, which for a while made it look shared; it lived in
        shared_assets while Tileset also ran an HTTPServer. Tileset no longer does, so OCPP is the
        only user and the page belongs to it. Who LOADS an asset is core's business; who OWNS it
        is decided by how many apps need it.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.
    */
    AddAssetSearchRootFromExe("../assets");                 //apps/ocpp/assets - the served page
    AddAssetSearchRootFromExe("../../../shared_assets");    //default shaders and fonts

    Application* main_app = new ApplicationOCPP();
    main_app->Start();
    return main_app->Exit();
}

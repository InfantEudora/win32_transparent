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
#include "ApplicationUI.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        ONE ROOT - this app owns no assets of its own, like Tetris. See apps/tetris/main.cpp for
        the reasoning, and for how to add an app root later if that changes.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.

        NOT DECLARED AT ALL IN A BAKED BUILD, and that is the point of the #ifndef rather than
        tidiness. LoadFile asks the baked table before it touches the search path, so with
        BAKE_ASSETS=1 every one of these names is already answered and a root could only ever
        be consulted for a name that is going to fail anyway. Leaving it in would make the log
        claim a search path the process never uses, and would make "does the bake actually
        cover everything?" unanswerable - a missing asset would quietly be found on disk and
        the gap would surface on the machine that has no disk copy. ASSETS_BAKED comes from
        engine.mk; see the BAKED ASSETS block there.
    */
#ifndef ASSETS_BAKED
    AddAssetSearchRootFromExe("../../../shared_assets");
#endif

    Application* main_app = new ApplicationUI();
    main_app->Start();
    return main_app->Exit();
}

#include <winsock2.h>
#include <ws2tcpip.h>

#include <tchar.h>

#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Debug.h"
#include "File.h"

//Required for NVidia Optimus to use the discrete GPU on laptops with integrated graphics. Put this
//in the main.cpp of your application, and it will be picked up by the driver.
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }

//The app's class, by name - see the note in apps/tank/main.cpp for why each app has its own main
//instead of one shared one reaching the class through -DAPP_HEADER/-DAPP_CLASS.
#include "ApplicationArcher.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        This app's own root first, then the shared one, which is the order every app uses.

        Nothing resolves out of apps/archer/assets yet - this prototype is built entirely from
        generated primitives, so every name it asks for (shaders/default.vert, shaders/deferred.frag
        and the rest of the pipeline's) falls through to shared_assets. The first root is declared
        anyway rather than added later, because it is what the archer's own meshes, bow and level
        props will arrive through, and an app that declares its roots only once it has assets is an
        app where the first asset added does not load and nobody knows why.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.

        NOT DECLARED AT ALL IN A BAKED BUILD (BAKE_ASSETS=1, which defines ASSETS_BAKED). LoadFile
        asks the baked table before it touches the search path, so with everything baked a root can
        only ever be consulted for a name that is going to fail anyway.
    */
#ifndef ASSETS_BAKED
    AddAssetSearchRootFromExe("../assets");                 //apps/archer/assets
    AddAssetSearchRootFromExe("../../../shared_assets");    //the default shaders and the font
#endif

    Application* main_app = new ApplicationArcher();
    main_app->Start();
    return main_app->Exit();
}

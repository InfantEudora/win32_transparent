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

/*
    The app's class, by name. The root main.cpp used to reach it through -DAPP_HEADER and
    -DAPP_CLASS so that one main.cpp could serve twelve apps; each app having its own main is what
    makes those defines - and the .current_app sentinel that existed to stop main.o going stale
    when APP changed - unnecessary.
*/
#include "ApplicationTank.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        WHERE THIS APP'S ASSETS ARE, declared here rather than baked in by the makefile.

        Order matters and is the point: the app's own root is searched first, so dropping a file
        named like a shared one into apps/tank/assets overrides it for this app alone.

        Relative TO THE EXECUTABLE, not to the working directory. tank.exe lives in
        apps/tank/build/, and resolving against the working directory would mean it only found its
        assets when launched from one particular folder - so running it from a debugger, a
        shortcut, or a script elsewhere would fail with nothing obviously wrong. See
        GetExecutableDirectory in core/File.h.
    */
    AddAssetSearchRootFromExe("../assets");                 //apps/tank/assets
    AddAssetSearchRootFromExe("../../../shared_assets");    //the engine's shared shaders and fonts

    Application* main_app = new ApplicationTank();
    main_app->Start();
    return main_app->Exit();
}

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>

#include <tchar.h>
#include <crtdbg.h>
#endif

#include <cstdio>
#include <vector>

#include "Debug.h"
#include "File.h"

#if defined(_WIN32)
//Required for NVidia Optimus to use the discrete GPU on laptops with integrated graphics. Put this in the main.cpp of your application, and it will be picked up by the driver.
//Windows only: on Linux the choice of GPU is the launcher's (prime-run, DRI_PRIME=1), not something an exe can export.
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }
#endif

//The app's class, by name - see the note in apps/tank/main.cpp for why each app has its own main.
#include "ApplicationChasm.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

//The program, whichever entry point the platform calls (below). show_command is Windows' nShowCmd, -1 elsewhere.
static int ChasmMain(int show_command){
    //Everything logged, to a file beside the exe as well (demo_plan.md, step 4): a tester's double-clicked exe has
    //nowhere for stderr to go, and a bug report carries the log.
    Debugger::OpenLogFile(GetExecutableDirectory() + "/chasm.log");
    if (show_command >= 0){
        debug->Info("nShowCmd = %i\n",show_command);
    }

    /*
        This app's own root first - the palette lives there - then the shared one for the engine's
        shaders, as in every other app, so a file here overrides a shared one. Not declared in a
        baked build - see the same block in apps/bomber/main.cpp.
    */
#ifndef ASSETS_BAKED
    AddAssetSearchRootFromExe("../assets");                 //apps/chasm/assets
    AddAssetSearchRootFromExe("../../../shared_assets");
#endif

    Application* main_app = new ApplicationChasm();
    main_app->Start();
    return main_app->Exit();
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    return ChasmMain(nShowCmd);
}
#else
//The arguments are not passed on: the engine reads the command line itself (Application.cpp), as on Windows.
int main(int argc, char** argv){
    (void)argc;
    (void)argv;
    return ChasmMain(-1);
}
#endif

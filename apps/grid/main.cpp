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
#include "ApplicationGrid.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Some info we were called with
    debug->Info("nShowCmd = %i\n",nShowCmd);
    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    /*
        This app's own root first, then the shared one - see apps/tank/main.cpp. Grid owns by far
        the most: ten .obj meshes with their .mtl materials, five .glb, the skybox cube and the
        textures those materials name.

        The textures are worth a note. They are not loaded by anything here - a .mtl names them
        and core/OBJLoader loads them on the mesh's behalf, as "textures/brickwall.jpg" straight
        out of the material file. That only works because a .mtl already writes its maps in the
        same <category>/<file> form this engine names assets in, so the search path finds them in
        this app's root with no translation at all.

        Relative to the EXECUTABLE, not the working directory - see GetExecutableDirectory in
        core/File.h.
    */
    AddAssetSearchRootFromExe("../assets");                 //apps/grid/assets
    AddAssetSearchRootFromExe("../../../shared_assets");    //default and skybox shaders, fonts

    Application* main_app = new ApplicationGrid();
    main_app->Start();
    return main_app->Exit();
}

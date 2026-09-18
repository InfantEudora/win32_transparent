#include <windows.h>

//NVidia Optimus: picked up by the driver to choose the discrete GPU on a laptop. Every main.cpp
//in this tree carries it - see apps/bomber/main.cpp.
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }

#include "File.h"

#include "SpritePackerApplication.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    // IMPORTANT: function-local static, not a file-scope global -- same
    // static-init-order reasoning as android_main()'s `static HelloApplication
    // instance;` (see core/Application.h's class comment and
    // docs/static-init-order-crash.md).
    /*
        ADDED HERE, not in the port: this engine resolves every asset name against roots the app
        declares, and the tool needs shaders/default.* out of shared_assets. The port's
        Application found its own; ours does not - see core/File.h.

        ONE ROOT, because this tool has no assets of its own. The images it packs are chosen at
        run time through the folder picker and read by absolute path, so they never go through
        the search path at all.
    */
    AddAssetSearchRootFromExe("../../../shared_assets");    //default shaders and fonts

    static SpritePackerApplication application;
    //Start() creates the threads and calls the app's Init(); Exit() is this engine's spelling
    //of the port's Shutdown().
    application.Start();
    return application.Exit();
}

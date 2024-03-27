#include "ApplicationUI.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationUI", DEBUG_ALL);

ApplicationUI::ApplicationUI():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationUI::Run(void){
    int2 dimensions = GetDisplaySettings();

    //Create a main window
    main_window = Window::CreateNewLayeredWindow(512,512,&Window::wcs.at(0));
    if (!main_window){
        debug->Fatal("Unable to create window\n");
    }
    if (!main_window->Init()){
        debug->Fatal("Failed to init window\n");
    }

    main_window->Show(SW_SHOWDEFAULT);

    //Setup renderer
    Renderer::SetVSync(true);

    if (!main_window->InitImGui()){
        debug->Fatal("Failed to setup ImGui on Window\n");
    }

    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    renderer->Init();

    //Catch all input and window related messages in this thread:
    MSG msg = {0};
    while (main_window->f_should_quit == false){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        main_window->inputcontroller->UpdateKeyState();

        RunLogic();

        renderer->DrawFrame(NULL,NULL,NULL);

        //Tell ImGui to start a new frame
        main_window->ImGuiNewFrame();

        UpdateUI();

        main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        main_window->DrawFrame();
    }
}

//Called before update physics
void ApplicationUI::RunLogic(){

}

void ApplicationUI::UpdateUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("This application only renders a window.");
    ImGui::End();
}
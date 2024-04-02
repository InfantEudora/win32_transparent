#include "ApplicationCANUI.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationCANUI", DEBUG_ALL);

ApplicationCANUI::ApplicationCANUI():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationCANUI::Run(void){
    int2 dimensions = GetDisplaySettings();

    //Create a main window
    main_window = Window::CreateNewLayeredWindow(800,600,&Window::wcs.at(0));
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

    //Init can interface
    pcan_reader = new PCANReader();
    pcan_reader->name = "USB1";

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
void ApplicationCANUI::RunLogic(){

}

void ApplicationCANUI::UpdateUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("A CAN UI Application");
    if (ImGui::Button("Connect CAN-USB interface\n")){
        pcan_reader->Connect();
    }

    ImVec4 red = ImVec4(1,0,0,1);
    ImVec4 green = ImVec4(0,1,0,1);
    ImVec4 yellow = ImVec4(1,1,0,1);

    ImGui::SameLine();
    if (!pcan_reader->connected){
        ImGui::TextColored(red,"Disconnected");
    }else{
        ImGui::TextColored(green,"Connected");
    }
    ImGui::End();

    //InfyPower interface
    ImGui::Begin("InfyPower Control Interface");
    ImGui::End();

}
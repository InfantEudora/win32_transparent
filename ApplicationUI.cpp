#include "ApplicationUI.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationUI", DEBUG_ALL);

ApplicationUI::ApplicationUI():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationUI::Init(void){
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_MSAA)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }

    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    renderer->Init();

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_window->Resize(1024,768);

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics();

}

//Called before update physics
void ApplicationUI::RunLogic(){

}

void ApplicationUI::DrawImGuiUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("This application only renders a window.");
    ImGui::End();
}
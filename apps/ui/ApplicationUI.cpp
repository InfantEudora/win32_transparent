#include "ApplicationUI.h"
#ifdef USE_IMGUI
//core/Window.h no longer pulls ImGui into every translation unit - see the note at the top of it.
//Guarded because a build with USE_IMGUI=0 has no library behind this header, and every panel
//function below that would call it is compiled out too.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationUI", DEBUG_ALL);

ApplicationUI::ApplicationUI():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationUI::Init(void){
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_window->Resize(1024,768);

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics(GetPhysicsTimestep());
}

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationUI::DrawImGuiUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("This application only renders a window.");
    ImGui::End();
}
#endif //USE_IMGUI
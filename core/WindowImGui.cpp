/*
    Window's ImGui backend - context creation, the Win32 and OpenGL3 platform backends, the
    per-frame begin/end, and the WndProc forward.

    One of a swappable PAIR with core/WindowImGui_none.cpp; engine.mk compiles this one when
    USE_IMGUI=1 and the other when it is 0. Same reasoning as core/ApplicationDebugUI.cpp, and
    the same reason it is not an #ifdef in Window.cpp: core objects are shared between every app
    and make compares them by timestamp, not by flags.

    Window.h is UNCHANGED either way, including its imgui backend #includes. Those are
    declarations and cost nothing at link time; keeping them makes the header identical for
    every app, which is the property that matters.
*/
#include <winsock2.h>
#include "glad.h"

#include "Window.h"
#include "File.h"
#include "Debug.h"

//ImGui itself, which core/Window.h deliberately no longer drags into every translation unit in
//the tree - see the note at the top of it. This file is one of the three places that actually
//calls the library, so this is where the include belongs.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static Debugger* debug = new Debugger("WindowImGui",DEBUG_ALL);

bool Window::InitImGui(){
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    //Docking. The submodule is on imgui's docking branch, but the flag was never set, so nothing
    //could actually be docked - Application::RenderApplicationUI builds a default left-hand
    //layout on top of this. Enabling it makes EVERY ImGui window in every app dockable, which is
    //the intent: an app'''s own panels can be dragged into the same layout as the core ones.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    //Load a font
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    sprintf(config.Name,"Consola TTF");

    /*
        AddFontFromMemoryTTF takes ownership of what it is given by default and frees it with the
        atlas - which would be the file layer's buffer, freed out from under a cache that is still
        handing that pointer out (see File.h). So we keep ownership, which is also the arrangement
        ImGui prefers since 1.92: it no longer copies for this flag, and it requires the data to
        outlive the atlas. Being lent something that lives as long as the process is exactly that.
    */
    config.FontDataOwnedByAtlas = false;

    size_t size = 0;
    uint8_t* data = LoadFile("fonts/consola.ttf",&size);
    ImFont* font = NULL;
    if (data){
        font = io.Fonts->AddFontFromMemoryTTF(data,size, 13, &config);
    }
    const char* glsl_version = "#version 430";

    // Setup Platform/Renderer backends
    if (!ImGui_ImplWin32_Init(hWnd)){
        debug->Err("Failed to do ImGui_ImplWin32_Init\n");
        return false;
    };
    if (!ImGui_ImplOpenGL3_Init(glsl_version)){
        debug->Err("Failed to do ImGui_ImplOpenGL3_Init\n");
        return false;
    }
    return true;
}

void Window::ImGuiNewFrame(){
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Window::ImGuiRenderDrawData(){
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/*
    The WndProc forward, as a wrapper so Window.cpp's message loop does not name ImGui.

    Returns nonzero when ImGui consumed the message. With USE_IMGUI=0 the twin returns 0 - nothing
    is consuming input, which is exactly right: there are no panels to click on.
*/
int ImGuiForwardWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    return (int)ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam);
}

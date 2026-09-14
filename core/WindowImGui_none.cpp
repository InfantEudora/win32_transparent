/*
    Window's ImGui backend, switched off. USE_IMGUI=0 compiles this instead of
    core/WindowImGui.cpp - see the note at the top of that file for why the two are separate
    translation units rather than one with an #ifdef.

    Nothing here creates an ImGui context, so nothing in the process has one. That is the point:
    with this twin linked, libimgui.a is not linked at all and its 618 KB are not in the binary.
*/
#include <winsock2.h>
#include "Window.h"

bool Window::InitImGui(){
    //TRUE, not false. The caller treats false as fatal ("Failed to setup ImGui on Window"), and
    //there is nothing to fail here - a build with no debug UI came up exactly as intended.
    return true;
}

void Window::ImGuiNewFrame(){
}

void Window::ImGuiRenderDrawData(){
}

int ImGuiForwardWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    (void)hWnd;
    (void)msg;
    (void)wParam;
    (void)lParam;
    //Zero means "not consumed", so every message carries on to the switch below the call site.
    //With no panels on screen there is nothing that could legitimately swallow input.
    return 0;
}

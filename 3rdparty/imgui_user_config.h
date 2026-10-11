/*
    ImGui's compile-time configuration for this engine. imgui.h includes it first, because
    -DIMGUI_USER_CONFIG names it - in BOTH 3rdparty/makefile (libs/libimgui.a) and engine.mk
    (everything else that includes imgui.h).

    Both, for the reason 3rdparty/miniaudio_config.h gives: imgui.h is compiled on each side of
    the library boundary, its inline functions with it, and a setting seen by one side only is a
    silent disagreement rather than a build error.

    Not 3rdparty/imgui/imconfig.h, which stays stock: it is inside the submodule, and an edit
    there is one `git submodule update` from vanishing without a word - backlog item 86 is that
    trap, already sprung once.
*/
#pragma once

/*
    IM_ASSERT. Stock ImGui uses assert(), which prints the expression, file and line and calls
    abort(). For an assert that fires once a week that is not enough to act on: the one that
    prompted this, `i >= 0 && i < Size`, is ImVector::operator[], which every vector in ImGui
    goes through. The handler (core/WindowImGui.cpp) still stops the program, but first writes
    which thread failed and every thread's stack - see core/CrashReport.h.
*/
void ImGuiAssertFailed(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) ((_EXPR) ? (void)0 : ImGuiAssertFailed(#_EXPR, __FILE__, __LINE__))

#ifndef _WINDOW_H_
#define _WINDOW_H_

//Windows header
#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <vector>

#include <GL/gl.h>
#include <GL/glu.h>

#include "InputController.h"

/*
    NO ImGui HEADERS HERE, deliberately, and this is the interesting part of the file.

    Until 2026-09-14 this header included imgui.h and both backend headers, so every translation
    unit that touched a Window - which is to say very nearly all of core and all fourteen apps -
    had the whole of ImGui's API in scope whether it wanted it or not, including in builds with
    USE_IMGUI=0 where the library is not even linked.

    They are gone rather than wrapped in an #ifdef, and the difference matters. This header is
    read by CORE translation units, compiled with CORE_CFLAGS, and by APP ones, compiled with
    CFLAGS - and only the second carries -DUSE_IMGUI. An #ifdef here would make the same header
    mean two different things in the same build, which is the sort of thing that is fine until
    the day it silently is not. Deleting the includes cannot: Window has no ImGui-typed MEMBER,
    only the three methods below, so nothing about this class changes either way.

    Anything that actually calls ImGui includes it itself now - core/WindowImGui.cpp,
    core/ApplicationDebugUI.cpp, and each app's own panel code behind its own #ifdef USE_IMGUI.
    Which is also simply where an include belongs.
*/

/*
    The WndProc forward, so Window.cpp's message loop does not name ImGui. Nonzero means the debug
    UI consumed the message. Defined in core/WindowImGui.cpp, or as a no-op returning 0 in its twin.

    A FREE function rather than a Window member, because the window procedure that calls it is one
    - Win32 hands WndProc to the OS, so it cannot be a non-static member.
*/
int ImGuiForwardWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Generic wrapper around a DIB with a 32-bit color depth.
typedef struct{
    int width;
    int height;
    int pitch;
    HDC hdc;
    HBITMAP hBitmap;
    BITMAPINFO info;
    BYTE *pPixels;
} Image;

/*
    A Class for managing WIN32 windows.

    We need at least one window to be able to query OpenGL capabilities on.
    After that, we can spawn as many windows as we like... theoretically.
*/
class Window;

LRESULT CALLBACK windproc(HWND hwnd, UINT wm, WPARAM wp, LPARAM lp);

class Window{
public:
    static Window* GetWindowByHandle(HWND hWnd);

    HWND        hWnd = 0;   //Handle to a window
    HDC         hDC;        //Handle to a Device Context
    HGLRC       hRC;
    WNDCLASSEXA*  wc = NULL;         //The class this window has

    InputController* inputcontroller = NULL;

    int left = 0;
    int top = 0;
    int width = 800;
    int height = 600;
    int width_windowed = 800;
    int height_windowed = 600;

    bool f_is_layered = false;
    bool f_resized = false;
    bool f_fullscreen = false;
    bool f_istogglingfullscreen = false;
    bool f_has_focus = false;
    std::string title = "Window Title";

    bool f_should_quit = false;
    bool f_control_down = false;

    //Bitmap image for layered window
    Image g_image;
    BYTE* pixels = NULL;//[width * height * 4] = {0};

    Window();
    ~Window();

    static void RegisterWindowClasses();
    bool Init();
    bool InitOpenGL(); //Needs to be called on a window, only once.
    bool InitImGui();

    static Window* CreateNewLayeredWindow(int width, int height, WNDCLASSEXA* wc);
    //`title` is what shows in the title bar and on the taskbar. Passed in at creation rather
    //than SetTitle'd afterwards so the window is never briefly called something else - see
    //Application::Start, which hands it the app's own name.
    static Window* CreateNewWindow(int width, int height, WNDCLASSEXA* wc, const char* title);

    void ImGuiNewFrame();
    void ImGuiRenderDrawData();

    void SwapWindowBuffers();
    void CopyBufferToImage();
    void CopyBufferToBackBuffer();
    void RedrawLayeredWindow();

    void Show(int nShowCmd);
    void Resize(int width, int height);
    void Move(int x, int y);
    void SetTitle(std::string);
    void RegisterDropFiles();
    void Close(void);
    static HWND _FindWindow(std::string title);
    static std::vector<WNDCLASSEXA>wcs;      //Different types of window classes

    //Callback when a file is dropped on the window
    void SetOnFileDropped(std::function<void(std::string)> callback);
    std::function<void(std::string)> onFileDropped; //Declared public because it must be accessed from windproc
private:
    static std::vector<Window*> windows; //An array of all created windows

};

#endif
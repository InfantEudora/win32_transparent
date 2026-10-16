#ifndef _WINDOW_H_
#define _WINDOW_H_

#if defined(_WIN32)
//Windows header
#include <windows.h>
#include <mmsystem.h>
#endif
#include <atomic>
#include <string>
#include <vector>
//SetOnFileDropped/onFileDropped below. This header got <functional> for free until 2026-09-18
//by way of Object.h -> Physics.h -> reactphysics3d, and putting physics behind USE_PHYSICS is
//what exposed it: the no-physics build stopped at "'std::function' has not been declared" in a
//file that has nothing to do with physics. Same rule as the ImGui note below - a header
//includes what it uses.
#include <functional>

#if defined(_WIN32)
//GLU went: nothing in the tree calls it, and it is one more package a Linux build would need.
#include <GL/gl.h>
#endif

#include "InputController.h"
#include "type_int2.h"

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
#if defined(_WIN32)
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
#endif

/*
    A Class for managing WIN32 windows.

    We need at least one window to be able to query OpenGL capabilities on.
    After that, we can spawn as many windows as we like... theoretically.

    THE CLASS IS THE PLATFORM SEAM. Window.cpp is its Win32 implementation, and everything outside it
    reaches the window system only through the methods below - which is what lets the rest of core
    build where another window system sits behind this class (Wayland, X11). The Win32 handles and
    the layered-window machinery are Win32's alone and say so with #if.
*/
class Window;

#if defined(_WIN32)
LRESULT CALLBACK windproc(HWND hwnd, UINT wm, WPARAM wp, LPARAM lp);
#elif defined(__linux__) && !defined(__ANDROID__)
//The X11 backend's handles, defined in Window_x11.cpp alone: Xlib's own window type is called Window.
struct WindowX11;
#endif

class Window{
public:
#if defined(_WIN32)
    static Window* GetWindowByHandle(HWND hWnd);

    HWND        hWnd = 0;   //Handle to a window
    HDC         hDC;        //Handle to a Device Context
    HGLRC       hRC;
    WNDCLASSEXA*  wc = NULL;         //The class this window has
#elif defined(__linux__) && !defined(__ANDROID__)
    WindowX11*  x11 = NULL;          //Display, window, GLX context - see Window_x11.cpp
#endif

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

    /*
        WHETHER ESCAPE CLOSES THE WINDOW. True by default, because that is right for the tools and
        test apps that make up most of this tree - an app with no menu and no way out otherwise.

        AN APP WITH A MENU CLEARS IT, and then maps VK_ESCAPE like any other key. Escape already
        reaches the InputController either way: Raw Input forwards every virtual key through
        SubmitSystemKey without looking at it (see RawInput.cpp), so the key was never the problem.
        The problem was that the window closed out from under whatever the app wanted to do with
        it, and this is the only thing this flag stops.

        A DEFAULT RATHER THAN A POLICY, which is the point - an app that does nothing keeps the
        behaviour it has today, and one that wants Escape for its own menu says so in one line
        instead of the engine having to guess.

        Ctrl+C on the CONSOLE is unaffected and stays: that one is the operator asking the process
        to stop, not the player pressing a key, and those should not share a control.
    */
    bool f_escape_closes_window = true;

    /*
        WHETHER THE WINDOW'S CLOSE ASKS THE APP FIRST. False by default: the X, Alt+F4 and anything else
        that sends WM_CLOSE close the window, as they always have.

        AN APP THAT ASKS "ARE YOU SURE?" SETS IT (apps/chasm/docs/demo_plan.md). Then WM_CLOSE closes
        nothing: it raises f_close_asked, the app finds it on its next pass, asks its question, and on a
        yes quits by RequestQuitAll - the way out that never needed WM_CLOSE. Read with exchange(false),
        so one close is one question.

        Ctrl+C on the console still stops the process, as above: the operator, not the player.
    */
    bool f_close_asks_app = false;
    std::atomic<bool> f_close_asked{false};

#if defined(_WIN32)
    //Bitmap image for layered window
    Image g_image;
    BYTE* pixels = NULL;//[width * height * 4] = {0};
#endif

    Window();
    ~Window();

#if defined(_WIN32)
    static void RegisterWindowClasses();
#endif
    bool Init();
    bool InitOpenGL(); //Needs to be called on a window, only once.
    bool InitImGui();

#if defined(_WIN32)
    static Window* CreateNewLayeredWindow(int width, int height, WNDCLASSEXA* wc);
#endif
    //`title` is what shows in the title bar and on the taskbar. Passed in at creation rather
    //than SetTitle'd afterwards so the window is never briefly called something else - see
    //Application::Start, which hands it the app's own name.
    /*
        The window class is an INDEX into wcs, not a pointer, and that is what lets this signature
        be the same on both platforms. Every caller in either tree passed &Window::wcs.at(0) - the
        pointer bought nothing, and a WNDCLASSEXA* in the signature is what stopped Android from
        ever sharing this entry point. Android ignores the index: it has one surface, handed to it
        by the compositor, and no notion of a window class.
    */
    static Window* CreateNewWindow(int width, int height, int wc_index, const char* title);

    void ImGuiNewFrame();
    void ImGuiRenderDrawData();

    void SwapWindowBuffers();
#if defined(_WIN32)
    void CopyBufferToImage();
#endif
    void CopyBufferToBackBuffer();
#if defined(_WIN32)
    void RedrawLayeredWindow();
#endif

    //The GL context, handed between threads: Start() releases it on the main thread, the render
    //thread makes it current at its top and releases it again before it ends (FrameThreadFunction).
    bool MakeContextCurrent();
    void ReleaseContext();

    //The main thread's whole job once the render thread is running: window messages and input,
    //until f_should_quit.
    void PumpUntilQuit();

    //The monitor's current mode, in pixels.
    static int2 GetDisplaySize();

    //Shows the window the first time. Minimised means minimised AND not activated - see --minimized
    //in Application::Start for why both halves matter.
    void Show(bool f_minimized);
    void Resize(int width, int height);
    //Above every other window, or back among them - the debug UI's Window menu.
    void SetAlwaysOnTop(bool f_on);
    /*
        Borderless full screen (a topmost popup, maximised) or back to the window it was. What
        Alt+Enter toggles, and what Application applies at start when an app asks for it
        (f_start_fullscreen). Asking for the state it is already in does nothing.
    */
    void SetFullscreen(bool f_on);
    //Iconic - on the taskbar rather than on screen. The render loop paces itself by it: a
    //minimised window gets no vsync, so SwapBuffers stops holding the loop back.
    bool IsMinimized();
    void Move(int x, int y);
    void SetTitle(std::string);
    void RegisterDropFiles();
    void Close(void);

    /*
        Ask every window to shut down, from a thread that is not the one pumping messages.

        THE FLAG ONLY, which is the difference from Close() above: Close() sends WM_CLOSE, and
        SendMessage from another thread blocks until the pump thread has run the handler - so a
        caller that is not the pump, and cannot assume the pump is even responsive, would be
        waiting on the very thing it is trying to stop. Every loop in the engine already polls
        f_should_quit, so setting it is the whole job; the pump notices within its wait timeout
        and unwinds normally. Application::ConsoleHandler is the caller this exists for.
    */
    static void RequestQuitAll();
#if defined(_WIN32)
    static HWND _FindWindow(std::string title);
    static std::vector<WNDCLASSEXA>wcs;      //Different types of window classes
#endif

    //Callback when a file is dropped on the window
    void SetOnFileDropped(std::function<void(std::string)> callback);
    std::function<void(std::string)> onFileDropped; //Declared public because it must be accessed from windproc
private:
    static std::vector<Window*> windows; //An array of all created windows

};

#endif
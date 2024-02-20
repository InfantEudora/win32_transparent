#ifndef _WINDOW_H_
#define _WINDOW_H_

//Windows header
#include <windows.h>
#include <string>
#include <vector>

/*
    A Class for managing WIN32 windows.

    We need at least one dummy window to be able to query OpenGL capabilities on.
    After that, we can spawn as many windows as we like... theoretically.
    Right now only one main window is supported.
*/
class Window;

class Window{
    public:
    static Window* dummywindow;
    static Window* GetWindowByHandle(HWND hWnd);

    HINSTANCE   hInst;
    HWND        hWnd = 0; //Handle to a window
    HDC         hDC; //Handle to a Device Context
    HGLRC       hRC;

    int left = 0;
    int top = 0;
    int width = 800;
    int height = 600;
    int width_windowed = 800;
    int height_windowed = 600;
    bool f_resized = false;
    bool f_fullscreen = false;
    bool f_istogglingfullscreen = false;
    bool f_has_focus = false;
    std::string title = "Window Title";


    Window(){};
    ~Window(){};

    void RegisterWindowClasses();
    void InitOpenGL(HINSTANCE hInst);
    //void InitGL(HWND& hWnd, HINSTANCE hInst);
    void SetVSync(bool enable);
    void Show(int cmd);
    void Resize(int width, int height);
    void Move(int x, int y);
    void SetTitle(std::string);
    static HWND _FindWindow(std::string title);

    //void Register_DropFiles();
    //std::queue<std::string> dropped_files;
private:
    static bool f_dummy_created;
    static std::vector<Window*> windows; //An array of all created windows
    void CreateDummyWindow();
};

#endif
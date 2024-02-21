#ifndef _WINDOW_H_
#define _WINDOW_H_

//Windows header
#include <windows.h>
#include <string>
#include <vector>

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
    WNDCLASSEX*  wc = NULL;         //The class this window has

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

    bool f_should_quit = false;
    bool f_control_down = false;


    Window();
    ~Window();

    static void RegisterWindowClasses();
    void InitOpenGL();
    static Window* CreateNewLayeredWindow(int width, int height, WNDCLASSEX* wc);
    static Window* CreateNewWindow(int width, int height, WNDCLASSEX* wc);

    void SetVSync(bool enable);
    void Show(int nShowCmd);
    void Resize(int width, int height);
    void Move(int x, int y);
    void SetTitle(std::string);
    void Close(void);
    static HWND _FindWindow(std::string title);
    static std::vector<WNDCLASSEX>wcs;      //Different types of window classes

    //void Register_DropFiles();
    //std::queue<std::string> dropped_files;
private:
    static bool f_dummy_created;
    static std::vector<Window*> windows; //An array of all created windows

    void CreateDummyWindow();
};

#endif
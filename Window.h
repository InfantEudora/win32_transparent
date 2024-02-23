#ifndef _WINDOW_H_
#define _WINDOW_H_

//Windows header
#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <vector>

#include <GL/gl.h>
#include <GL/glu.h>

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
    WNDCLASSEX*  wc = NULL;         //The class this window has

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

    static Window* CreateNewLayeredWindow(int width, int height, WNDCLASSEX* wc);
    static Window* CreateNewWindow(int width, int height, WNDCLASSEX* wc);

    void DrawFrame();
    void CopyBufferToImage();
    void CopyBufferToBackBuffer();
    void RedrawLayeredWindow();

    void Show(int nShowCmd);
    void Resize(int width, int height);
    void Move(int x, int y);
    void SetTitle(std::string);
    void Close(void);
    static HWND _FindWindow(std::string title);
    static std::vector<WNDCLASSEX>wcs;      //Different types of window classes

private:
    static std::vector<Window*> windows; //An array of all created windows
};

#endif
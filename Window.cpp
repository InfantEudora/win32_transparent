#include "Window.h"
#include "Debug.h"
#include "glad.h"

static Debugger* debug = new Debugger("Window",DEBUG_ALL);

std::vector<Window*>Window::windows; //A list of windows to match handles to
std::vector<WNDCLASSEXA>Window::wcs;      //Different types of window classes


void ImageDestroy(Image *pImage){
    if (!pImage)
        return;

    pImage->width = 0;
    pImage->height = 0;
    pImage->pitch = 0;

    if (pImage->hBitmap){
        DeleteObject(pImage->hBitmap);
        pImage->hBitmap = NULL;
    }

    if (pImage->hdc){
        DeleteDC(pImage->hdc);
        pImage->hdc = NULL;
    }

    memset(&pImage->info, 0, sizeof(pImage->info));
    pImage->pPixels = NULL;
}

bool ImageCreate(Image *pImage, int width, int height){
    if (!pImage)
        return false;

    // All Windows DIBs are aligned to 4-byte (DWORD) memory boundaries. This
    // means that each scan line is padded with extra bytes to ensure that the
    // next scan line starts on a 4-byte memory boundary. The 'pitch' member
    // of the Image structure contains width of each scan line (in bytes).

    pImage->width = width;
    pImage->height = height;
    pImage->pitch = ((width * 32 + 31) & ~31) >> 3;
    pImage->pPixels = NULL;
    pImage->hdc = CreateCompatibleDC(NULL);

    if (!pImage->hdc)
        return false;

    memset(&pImage->info, 0, sizeof(pImage->info));

    pImage->info.bmiHeader.biSize = sizeof(pImage->info.bmiHeader);
    pImage->info.bmiHeader.biBitCount = 32;
    pImage->info.bmiHeader.biWidth = width;
    pImage->info.bmiHeader.biHeight = -height;
    pImage->info.bmiHeader.biCompression = BI_RGB;
    pImage->info.bmiHeader.biPlanes = 1;

    pImage->hBitmap = CreateDIBSection(pImage->hdc, &pImage->info,
        DIB_RGB_COLORS, (void**)&pImage->pPixels, NULL, 0);

    if (!pImage->hBitmap){
        ImageDestroy(pImage);
        return false;
    }

    GdiFlush();
    return true;
}

//Used to match window to handle
Window* Window::GetWindowByHandle(HWND hWnd){
    for (Window* wnd:windows){
        if (wnd->hWnd == hWnd){
            return wnd;
        }
    }
    return NULL;
}

Window::Window(){
    windows.push_back(this);
}

Window::~Window(){

}

void Window::Close(){
    f_should_quit = true;
    SendMessage(hWnd, WM_CLOSE, 0, 0);
}

void Window::Show(int nShowCmd){
    ShowWindow(hWnd, nShowCmd);
    //UpdateWindow(hWnd);
}

//This registers the window classes for this application
void Window::RegisterWindowClasses(){
    WNDCLASSEXA wc = {0};

    wc.cbSize = sizeof(wc);
    wc.style = 0; //CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windproc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = NULL;
    wc.hIcon = NULL; // LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = NULL; //LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "MainWindowClass";
    wc.hIconSm = 0;

    if (!RegisterClassExA(&wc)){
        debug->Fatal("Failed to register GLLayeredWindowClass\n");
    }
    wcs.push_back(wc);
}

bool Window::Init(){
    if (!InitOpenGL()){
        return false;
    }

    if (!ImageCreate(&g_image, width, height)){
        return false;
    }

    return true;
}

bool Window::InitOpenGL(){
    PIXELFORMATDESCRIPTOR pfd = {0};

    // Don't bother with anything fancy here. This is just a dummy rendering
    // context so just ask for the bare minimum.
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    if (!(hDC = GetDC(hWnd)))
        return false;

    int pf = ChoosePixelFormat(hDC, &pfd);

    if (!SetPixelFormat(hDC, pf, &pfd))
        return false;

    if (!(hRC = wglCreateContext(hDC)))
        return false;

    if (!wglMakeCurrent(hDC, hRC))
        return false;

    if (!InitGLExtensions())
        return false;

    wglMakeCurrent(hDC, NULL);
    wglDeleteContext(hRC);

    //Now we ask for a specific OpenGL Context
    int attributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    hRC = wglCreateContextAttribsARB( hDC, 0, attributes );
    debug->Info("wglCreateContextAttribsARB: hRC = %lu\n",hRC);

    if (!wglMakeCurrent(hDC, hRC))
        debug->Err("wglMakeCurrent failed (%d)\n", GetLastError());

    return true;
}

void Window::DrawFrame(){
    //Renderer->DrawFrame();

    if (f_is_layered){
        SwapBuffers(hDC);
        CopyBufferToImage();

        // Finally we update our layered window with our scene.
        RedrawLayeredWindow();
    }else{
        CopyBufferToBackBuffer();
        SwapBuffers(hDC);
    }
}

void Window::RedrawLayeredWindow(){
    // The call to UpdateLayeredWindow() is what makes a non-rectangular
    // window possible. To enable per pixel alpha blending we pass in the
    // argument ULW_ALPHA, and provide a BLENDFUNCTION structure filled in
    // to do per pixel alpha blending.

    HDC hdc = GetDC(hWnd);

    if (hdc){
        HGDIOBJ hPrevObj = 0;
        POINT ptDest = {0, 0};
        POINT ptSrc = {0, 0};
        SIZE client = {g_image.width, g_image.height};
        BLENDFUNCTION blendFunc = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

        hPrevObj = SelectObject(g_image.hdc, g_image.hBitmap);
        ClientToScreen(hWnd, &ptDest);

        UpdateLayeredWindow(hWnd, hdc, &ptDest, &client,
            g_image.hdc, &ptSrc, 0, &blendFunc, ULW_ALPHA);

        SelectObject(g_image.hdc, hPrevObj);
        ReleaseDC(hWnd, hdc);
    }
}

void Window::CopyBufferToImage(){
    // Copy the contents of the framebuffer
    // to our bitmap image in local system memory. Notice that we also need
    // to invert the pbuffer's pixel data since OpenGL by default orients the
    // bitmap image bottom up. Our Windows DIB wrapper expects images to be
    // top down in orientation.
    //The correct buffer should have already been selected.

    if (pixels == NULL){
        pixels = new BYTE[width * height * 4];
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    for (int i = 0; i < height; ++i){
        memcpy(&g_image.pPixels[g_image.pitch * i],
            &pixels[((height - 1) - i) * (width * 4)],
            width * 4);
    }
}

void Window::CopyBufferToBackBuffer(){
    //Now we blit the resolve buffer into the back buffer
    //The correct buffer should already have been selected for reading.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_BACK);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

Window* Window::CreateNewLayeredWindow(int width, int height, WNDCLASSEXA* wc){
    Window* wnd = new Window();
    wnd->hWnd = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOPMOST, wc->lpszClassName,
                "GL Layered Window", WS_POPUP, 0, 0, width,
                height, 0, 0, NULL, 0);

    if(wnd->hWnd == NULL){
        debug->Err("Could not create requested window\n");
    }else{
        debug->Ok("New layered window hWnd=%lu\n",wnd->hWnd);
    }
    wnd->f_is_layered = true;
    wnd->width = width;
    wnd->height = height;
    return wnd;
}

Window* Window::CreateNewWindow(int width, int height, WNDCLASSEXA* wc){
    Window* wnd = new Window();

    int left = 200;
    int top = 200;

    //Get the coordinates of the renderable content in a window
    RECT rect;
    LPRECT lpRect = &rect;
    lpRect->left = left;
    lpRect->right = width + left;
    lpRect->top = top;
    lpRect->bottom = height + top;
    AdjustWindowRectEx(lpRect,WS_OVERLAPPEDWINDOW,false,WS_EX_LEFT);

    wnd->hWnd = CreateWindowExA(WS_EX_LEFT,
        wc->lpszClassName,
        "Normal Window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        lpRect->right-lpRect->left,
        lpRect->bottom-lpRect->top,
        NULL,
        NULL,
        NULL,
        NULL);

    if(wnd->hWnd == NULL){
        debug->Err("Could not create requested window\n");
    }else{
        debug->Ok("New window hWnd=%lu\n",wnd->hWnd);
    }
    wnd->width = width;
    wnd->height = height;
    return wnd;
}

LRESULT CALLBACK windproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    Window* wnd = Window::GetWindowByHandle(hWnd);
    if (!wnd){
        //Cannot find window before WM_CREATE is issued
        //Normally, you get: WM_NCCREATE, WM_NCCALCSIZE, WM_CREATE, WM_SIZE, WM_MOVE
        //debug->Err("Unable to find window by handle. msg = %lu (0x%0X) hWnd = %lu (0x%0X) \n",msg,msg,hWnd,hWnd);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    switch(msg){
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE){
                SendMessage(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }else if (wParam == VK_CONTROL){
                wnd->f_control_down = true;
                return 0;
            }else if (wParam == 'C'){
                if (wnd->f_control_down){
                    wnd->f_should_quit = true;
                    debug->Info("CTRL+C on window\n");
                    DestroyWindow(wnd->hWnd);
                }
                return 0;
            }else{
                //debug->Info("WM_KEYDOWN: %lu (0x%0X)\n",wParam,wParam);
                return 0;
            }
            break;

        case WM_NCHITTEST:
            //This returns the mouse is over the Titlebar of the window, which allows it to be dragged.
            return HTCAPTION;
        //case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_MOUSEMOVE:
        case WM_RBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_MOUSEWHEEL:

        case WM_LBUTTONUP:
        case WM_LBUTTONDOWN:

        case WM_MBUTTONUP:
        case WM_MBUTTONDOWN:

            break;
        case WM_DESTROY:
            debug->Warn("Window destroyed\n");
            wnd->f_should_quit = true;
            break;
        case WM_CREATE:
            {
            DWORD thread_id = GetCurrentThreadId();
            debug->Ok("WM_CREATE on main window thread id %lu\n",thread_id);
            }
            break;
        case WM_ACTIVATE:
            if(LOWORD(wParam) == WA_INACTIVE ){
                wnd->f_has_focus = true;
            }else{
                wnd->f_has_focus = false;
            }
            break;
        case WM_MOVE:
        {
            int x = (int)(short) LOWORD(lParam);
            int y = (int)(short) HIWORD(lParam);
            //debug->Info("WM_MOVE to %i,%i\n",x,y);
            //return 0;
        }
        break;
        case WM_MOVING:
        {
            //debug->Info("WM_MOVING\n");
           //return 0;
        }
        break;
        case WM_SETFOCUS:
            wnd->f_has_focus = true;
        break ;
        case WM_KILLFOCUS:
            wnd->f_has_focus = false;
        break;
        case WM_PAINT:
            debug->Info("WM_PAINT received\n");
        break;


        default:
            //ebug->Info("Window Message %lu\n",msg);
            break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}


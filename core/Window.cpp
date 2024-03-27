#include "Window.h"
#include "Debug.h"
#include "glad.h"

static Debugger* debug = new Debugger("Window",DEBUG_INFO);

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
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SUPPORT_GDI;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 32;
    pfd.cStencilBits = 8;
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

     //Read back the format
    pf = GetPixelFormat(hDC);                          // pixel format descriptor index
    DescribePixelFormat(hDC,pf,sizeof(pfd),&pfd);    // format from index
    debug->Info("GetPixelFormat\n",hWnd);
    debug->Info(" ColorBuffer : %i bits\n",pfd.cColorBits);
    debug->Info(" ZBuffer     : %i bits\n",pfd.cDepthBits);
    debug->Info(" Stencil     : %i bits\n",pfd.cStencilBits);
    debug->Info(" dwFlags     : 0x%08lX bits\n",pfd.dwFlags);

    //Now we ask for a specific OpenGL Context
    int attributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 5,
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    hRC = wglCreateContextAttribsARB( hDC, 0, attributes );
    debug->Info("wglCreateContextAttribsARB: hRC = %lu\n",hRC);

    if (!wglMakeCurrent(hDC, hRC)){
        debug->Err("wglMakeCurrent failed (%d)\n", GetLastError());
    }

    const char* gl_version = (const char*)glGetString(GL_VERSION);
    debug->Info("OpenGL Version: %s\n", gl_version);
    return true;
}

bool Window::InitImGui(){
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    //Load a font
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.GlyphExtraSpacing.x = 0.0f;
    //TODO: This can be loaded with our own file loading mechanism.
    ImFont* font = io.Fonts->AddFontFromFileTTF("fonts/consola.ttf", 13, &config);
    const char* glsl_version = "#version 430";

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplOpenGL3_Init(glsl_version);
    return true;
}

void Window::ImGuiNewFrame(){
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Window::ImGuiDrawFrame(){
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

//Simply copies buffer to backbuffer
void Window::DrawFrame(){
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

//hDC 0 = Desktop
HBITMAP GetScreenBMP(HDC hdc, int x, int y, int width, int height) {
    // Get screen dimensions
    //int nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    //int nScreenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Create compatible DC, create a compatible bitmap and copy the screen using BitBlt()
    HDC hCaptureDC  = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ hOld = SelectObject(hCaptureDC, hBitmap);
    BOOL bOK = BitBlt(hCaptureDC,0,0,width, height, hdc,x,y,SRCCOPY|CAPTUREBLT);

    SelectObject(hCaptureDC, hOld); // always select the previously selected object once done
    DeleteDC(hCaptureDC);
    return hBitmap;
}

BYTE* GetScreenCap(int width,int height){
    HDC hdc = GetDC(0);

    HBITMAP hBitmap = GetScreenBMP(hdc,500,200,width,height);

    BITMAPINFO MyBMInfo = {0};
    MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

    // Get the BITMAPINFO structure from the bitmap
    if(0 == GetDIBits(hdc, hBitmap, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) {
        debug->Err("GetDIBits\n");
    }

    // create the bitmap buffer
    BYTE* lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];

    // Better do this here - the original bitmap might have BI_BITFILEDS, which makes it
    // necessary to read the color table - you might not want this.
    MyBMInfo.bmiHeader.biCompression = BI_RGB;

    // get the actual bitmap buffer
    if(0 == GetDIBits(hdc, hBitmap, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) {
        debug->Err("GetDIBits 2\n");
    }

    DeleteObject(hBitmap);
    ReleaseDC(NULL, hdc);
    return lpPixels;
    //delete[] lpPixels;
}

void Window::CopyBufferToImage(){
    // Copy the contents of the framebuffer
    // to our bitmap image in local system memory. Notice that we also need
    // to invert the pbuffer's pixel data since OpenGL by default orients the
    // bitmap image bottom up. Our Windows DIB wrapper expects images to be
    // top down in orientation.
    //The correct buffer should have already been selected.

    //BYTE* cap = GetScreenCap(width,height);

    if (pixels == NULL){
        pixels = new BYTE[width * height * 4];
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    //Copy OpenGL buffer to bitmap
    for (int i = 0; i < height; ++i){
        memcpy(&g_image.pPixels[g_image.pitch * i],
            &pixels[((height - 1) - i) * (width * 4)],
            width * 4);
    }

    //Copy screenshot to bitmap
    /*
    for (int i = 0; i < height; ++i){
        memcpy(&g_image.pPixels[g_image.pitch * i], &cap[((height - 1) - i) * (width * 4)], width * 4);
    }
    delete[] cap;
    */
}

void Window::CopyBufferToBackBuffer(){
    //Now we blit the resolve buffer into the back buffer
    //The correct buffer should already have been selected for reading.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
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
        debug->Ok("New layered window %i x %i hWnd=%lu\n",width,height,wnd->hWnd);
    }
    wnd->f_is_layered = true;
    wnd->width = width;
    wnd->height = height;
    wnd->inputcontroller = new InputController();
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
    wnd->inputcontroller = new InputController();
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

    //Forward message to ImGui
    int res = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

    switch(msg){
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE){
                SendMessage(hWnd, WM_CLOSE, 0, 0);
                break;
            }else if (wParam == VK_CONTROL){
                wnd->f_control_down = true;
                break;
            }else if (wParam == 'C'){
                if (wnd->f_control_down){
                    wnd->f_should_quit = true;
                    debug->Info("CTRL+C on window\n");
                    DestroyWindow(wnd->hWnd);
                }
                break;
            }else{
                //debug->Info("WM_KEYDOWN: %lu (0x%0X)\n",wParam,wParam);
                break;
            }
            break;

        //case WM_NCHITTEST:
            //This returns the mouse is over the Titlebar of the window, which allows it to be dragged.
            //return HTCAPTION;

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
            debug->Trace("WM_MOVE to %i,%i\n",x,y);
            wnd->top = y;
            wnd->left = x;
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

    if (wnd->inputcontroller){
        wnd->inputcontroller->HandleMessage(msg,wParam,lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}


#include "Window.h"
#include "Debug.h"
#include "glad.h"

static Debugger* debug = new Debugger("Window",DEBUG_ALL);

std::vector<Window*>Window::windows; //A list of windows to match handles to
std::vector<WNDCLASSEX>Window::wcs;      //Different types of window classes

#define TEXTURE_WIDTH   128
#define TEXTURE_HEIGHT  128

// Vertex data for a unit cube centered about the origin.
// Each face contains 2 triangles.
Vertex g_cube[36] =
{
    // Positive Z.
    { 0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f},
    { 0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f},
    {-0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f},
    {-0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f},
    { 0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f},
    {-0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 1.0f},

    // Negative Z.
    {-0.5f,  0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   0.0f, 0.0f},
    {-0.5f, -0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   1.0f, 0.0f},
    { 0.5f,  0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   1.0f, 0.0f},
    {-0.5f, -0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   1.0f, 1.0f},

    // Positive Y.
    {-0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 0.0f},
    {-0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f},

    // Negative Y.
    {-0.5f, -0.5f, -0.5f,   0.0f, -1.0f, 0.0f,   0.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   0.0f, -1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f, -0.5f,   0.0f, -1.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f, -0.5f, -0.5f,   0.0f, -1.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   0.0f, -1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f,  0.5f,   0.0f, -1.0f, 0.0f,   1.0f, 1.0f},

    // Positive X.
    { 0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f},
    { 0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f},

    // Negative X.
    {-0.5f,  0.5f,  0.5f,   -1.0f, 0.0f, 0.0f,   0.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   -1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    {-0.5f,  0.5f, -0.5f,   -1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f,  0.5f, -0.5f,   -1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   -1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    {-0.5f, -0.5f, -0.5f,   -1.0f, 0.0f, 0.0f,   1.0f, 1.0f}
};


void ImageDestroy(Image *pImage)
{
    if (!pImage)
        return;

    pImage->width = 0;
    pImage->height = 0;
    pImage->pitch = 0;

    if (pImage->hBitmap)
    {
        DeleteObject(pImage->hBitmap);
        pImage->hBitmap = NULL;
    }

    if (pImage->hdc)
    {
        DeleteDC(pImage->hdc);
        pImage->hdc = NULL;
    }

    memset(&pImage->info, 0, sizeof(pImage->info));
    pImage->pPixels = NULL;
}

//
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

void ImagePreMultAlpha(Image *pImage){
    // The per pixel alpha blending API for layered windows deals with
    // pre-multiplied alpha values in the RGB channels. For further details see
    // the MSDN documentation for the BLENDFUNCTION structure. It basically
    // means we have to multiply each red, green, and blue channel in our image
    // with the alpha value divided by 255.
    //
    // Notes:
    // 1. ImagePreMultAlpha() needs to be called before every call to
    //    UpdateLayeredWindow() (in the RedrawLayeredWindow() function).
    //
    // 2. Must divide by 255.0 instead of 255 to prevent alpha values in range
    //    [1, 254] from causing the pixel to become black. This will cause a
    //    conversion from 'float' to 'BYTE' possible loss of data warning which
    //    can be safely ignored.

    if (!pImage)
        return;

    BYTE *pPixel = NULL;

    if (pImage->width * 4 == pImage->pitch)
    {
        // This is a special case. When the image width is already a multiple
        // of 4 the image does not require any padding bytes at the end of each
        // scan line. Consequently we do not need to address each scan line
        // separately. This is much faster than the below case where the image
        // width is not a multiple of 4.

        int totalBytes = pImage->width * pImage->height * 4;

        for (int i = 0; i < totalBytes; i += 4)
        {
            pPixel = &pImage->pPixels[i];
			pPixel[0] = (BYTE)(pPixel[0] * (float)pPixel[3] / 255.0f);
			pPixel[1] = (BYTE)(pPixel[1] * (float)pPixel[3] / 255.0f);
			pPixel[2] = (BYTE)(pPixel[2] * (float)pPixel[3] / 255.0f);
        }
    }
    else
    {
        // Width of the image is not a multiple of 4. So padding bytes have
        // been included in the DIB's pixel data. Need to address each scan
        // line separately. This is much slower than the above case where the
        // width of the image is already a multiple of 4.

        for (int y = 0; y < pImage->height; ++y)
        {
            for (int x = 0; x < pImage->width; ++x)
            {
                pPixel = &pImage->pPixels[(y * pImage->pitch) + (x * 4)];
				pPixel[0] = (BYTE)(pPixel[0] * (float)pPixel[3] / 255.0f);
				pPixel[1] = (BYTE)(pPixel[1] * (float)pPixel[3] / 255.0f);
				pPixel[2] = (BYTE)(pPixel[2] * (float)pPixel[3] / 255.0f);
            }
        }
    }
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
    WNDCLASSEX wc = {0};

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

    if (!RegisterClassEx(&wc)){
        debug->Fatal("Failed to register GLLayeredWindowClass\n");
    }
    wcs.push_back(wc);
}

bool Window::Init(){
    if (!InitOpenGL()){
        return false;
    }

    if (!InitFBO()){
        return false;
    }

    if (!InitVBO()){
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_TEXTURE_2D);

    // Create and bind the texture to the pbuffer's rendering context.
    InitCheckerPatternTexture();
    glBindTexture(GL_TEXTURE_2D, g_textureId);

    if (!ImageCreate(&g_image, width, height)){
        return false;
    }

    return true;
}


void Window::InitCheckerPatternTexture(){
    // Procedurally create a black and white checker pattern texture.
    // Adapted from the "OpenGL Programming Guide" (aka the red book).

    BYTE *pPixels = 0;
    BYTE c = 0;
    UINT pitch = TEXTURE_WIDTH * 4;
    std::vector<BYTE> checkerImage(pitch * TEXTURE_HEIGHT);

    for (UINT i = 0; i < TEXTURE_HEIGHT; ++i){
        for (UINT j = 0; j < TEXTURE_WIDTH; ++j){
            c = (BYTE)((((i & 0x10) == 0) ^ ((j & 0x10) == 0)) * 255);
            pPixels = &checkerImage[i * pitch + (j * 4)];
            pPixels[0] = pPixels[1] = pPixels[2] = c;
            if (c == 0){
                pPixels[3] = 255;
            }else{
                pPixels[3] = 200;
            }
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &g_textureId);

    glTextureParameteri(g_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(g_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(g_textureId, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTextureParameteri(g_textureId, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    glTextureStorage2D(g_textureId, 1, GL_RGBA8, TEXTURE_WIDTH, TEXTURE_WIDTH);
    glTextureSubImage2D(g_textureId, 0, 0, 0, TEXTURE_WIDTH, TEXTURE_WIDTH, GL_RGBA, GL_UNSIGNED_BYTE, &checkerImage[0]);
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

bool Window::InitFBO(){
    glGenFramebuffers(1, &msaa_fbo_id);
    glGenRenderbuffers(1, &color_rbo_id);
    glGenRenderbuffers(1, &depth_rbo_id);

    glGenFramebuffers(1, &resolve_fbo_id);
    glGenRenderbuffers(1, &resolve_rbo_id);

    if (msaa_fbo_id == -1){
        return false;
    }

    int aa_samples = 16;

    //Setup buffers:
    //Mutisampled color 16bit float
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rbo_id);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, aa_samples, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rbo_id);
    CheckFrameBuffer();

    //32-bit depth
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_id);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, aa_samples, GL_DEPTH_COMPONENT32F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_id);
    CheckFrameBuffer();

    //The resolve buffer
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);
    glBindRenderbuffer(GL_RENDERBUFFER, resolve_rbo_id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolve_rbo_id);
    CheckFrameBuffer();
    return true;
}

bool Window::InitVBO(){
    glCreateBuffers(1, (GLuint*)&cube_vbo);
    glNamedBufferStorage(cube_vbo, sizeof(g_cube), (float*)g_cube, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, (GLuint*)&cube_vao);
    glVertexArrayVertexBuffer(cube_vao, 0, cube_vbo, 0, sizeof(Vertex));
    return true;
}

//Blit all multisampled buffer back to main/resolve buffers
void Window::ResolveAA(){
    //Blit from multisampled buffer to main backbuffer = GL_COLOR_ATTACHMENT0
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_id);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void Window::DrawFrame(Shader* shader){
    //Select the mutisampled frambuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //Modify the cube
    rotation += 0.01f;
    fmat3 mr;
    vec3 v = vec3(0,1,0);
    mr.set_rotation(v,rotation);
    shader->Setmat3("obj_rotate",mr);

    int num_vertices = 36;

    glEnableVertexArrayAttrib(cube_vao,ATTRIB_VERTEX);
    glEnableVertexArrayAttrib(cube_vao,ATTRIB_NORMAL);
    glEnableVertexArrayAttrib(cube_vao,ATTRIB_UVCOORD);

    glVertexArrayAttribFormat(cube_vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
    glVertexArrayAttribFormat(cube_vao, ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
    glVertexArrayAttribFormat(cube_vao, ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float));

    glVertexArrayAttribBinding(cube_vao, ATTRIB_VERTEX, 0);
    glVertexArrayAttribBinding(cube_vao, ATTRIB_NORMAL, 0);
    glVertexArrayAttribBinding(cube_vao, ATTRIB_UVCOORD, 0);

    glBindVertexArray(cube_vao);

    glDrawArraysInstanced(GL_TRIANGLES, 0, num_vertices,1); // Starting from vertex 0; 3 vertices total -> 1 triangle

    ResolveAA();
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);

    if (f_is_layered){
        SwapBuffers(hDC);

        CopyBufferToImage();

        // Then we pre-multiply each pixel with its alpha component. This is how
        // the layered windows API expects images containing alpha information.
        ImagePreMultAlpha(&g_image);

        // Finally we update our layered window with our scene.
        RedrawLayeredWindow();
    }else{
        //Now we blit the resolve buffer into the back buffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo_id);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glDrawBuffer(GL_BACK);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

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
    // Copy the contents of the framebuffer - which in our case is our pbuffer -
    // to our bitmap image in local system memory. Notice that we also need
    // to invert the pbuffer's pixel data since OpenGL by default orients the
    // bitmap image bottom up. Our Windows DIB wrapper expects images to be
    // top down in orientation.

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

//Returns true if the framebuffer checks OK.
bool Window::CheckFrameBuffer(){
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE){
        debug->Ok("GL_FRAMEBUFFER_COMPLETE\n");
        return true;
    }
    if (status == GL_FRAMEBUFFER_UNDEFINED)
        debug->Err("GL_FRAMEBUFFER_UNDEFINED\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER\n");
    else if (status == GL_FRAMEBUFFER_UNSUPPORTED)
        debug->Err("GL_FRAMEBUFFER_UNSUPPORTED\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS\n");
    else
        debug->Err("glCheckFramebufferStatus UNKNOWN\n");
    return false;
}


Window* Window::CreateNewLayeredWindow(int width, int height, WNDCLASSEX* wc){
    Window* wnd = new Window();
    wnd->hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOPMOST, wc->lpszClassName,
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

Window* Window::CreateNewWindow(int width, int height, WNDCLASSEX* wc){
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

    wnd->hWnd = CreateWindowEx(WS_EX_LEFT,
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


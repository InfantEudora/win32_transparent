#include <GL/gl.h>
#include <GL/glu.h>
#include <tchar.h>
#include <mmsystem.h>
#include <cstdio>
#include <vector>
#include <crtdbg.h>

#include "Debug.h"
#include "Window.h"
#include "glad.h"

static Debugger* debug = new Debugger("Main",DEBUG_ALL);


//Window everything will be drawn in.
Window* wind = NULL;
Window* wind_norm = NULL;

//-----------------------------------------------------------------------------
// Globals.
//-----------------------------------------------------------------------------

#define IMAGE_WIDTH     256
#define IMAGE_HEIGHT    256

#define TEXTURE_WIDTH   128
#define TEXTURE_HEIGHT  128

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

struct Vertex{
    float pos[3];
    float normal[3];
    float texcoord[2];
};

Image g_image;


HGLRC g_hRC;
HDC   g_hPBufferDC;
HGLRC g_hPBufferRC;
HPBUFFERARB g_hPBuffer;
INT  g_frames;
UINT g_textureId;

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

BOOL WINAPI ConsoleHandler(DWORD console_event){
    switch(console_event){
        case CTRL_C_EVENT:
            debug->Ok("Shutting down by CTRL+C\n");
            if (wind){
                wind->Close();
            }
        break;
    }
    return true;
}

//-----------------------------------------------------------------------------
// Functions.
//-----------------------------------------------------------------------------

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
bool ImageCreate(Image *pImage, int width, int height)
{
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

void ImagePreMultAlpha(Image *pImage)
{
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

void RedrawLayeredWindow()
{
    // The call to UpdateLayeredWindow() is what makes a non-rectangular
    // window possible. To enable per pixel alpha blending we pass in the
    // argument ULW_ALPHA, and provide a BLENDFUNCTION structure filled in
    // to do per pixel alpha blending.

    HDC hdc = GetDC(wind->hWnd);

    if (hdc){
        HGDIOBJ hPrevObj = 0;
        POINT ptDest = {0, 0};
        POINT ptSrc = {0, 0};
        SIZE client = {g_image.width, g_image.height};
        BLENDFUNCTION blendFunc = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

        hPrevObj = SelectObject(g_image.hdc, g_image.hBitmap);
        ClientToScreen(wind->hWnd, &ptDest);

        UpdateLayeredWindow(wind->hWnd, hdc, &ptDest, &client,
            g_image.hdc, &ptSrc, 0, &blendFunc, ULW_ALPHA);

        SelectObject(g_image.hdc, hPrevObj);
        ReleaseDC(wind->hWnd, hdc);
    }
}

void Cleanup()
{
    if (g_textureId)
    {
        glDeleteTextures(1, &g_textureId);
        g_textureId = 0;
    }

    if (g_hPBuffer)
    {
        wglDeleteContext(g_hPBufferRC);
        wglReleasePbufferDCARB(g_hPBuffer, g_hPBufferDC);
        wglDestroyPbufferARB(g_hPBuffer);
        g_hPBufferRC = 0;
        g_hPBufferDC = 0;
        g_hPBuffer = 0;
    }

    if (wind->hDC){
        if (g_hRC){
            wglMakeCurrent(wind->hDC, 0);
            wglDeleteContext(g_hRC);
            g_hRC = 0;
        }

        ReleaseDC(wind->hWnd, wind->hDC);
        wind->hDC = 0;
    }

    ImageDestroy(&g_image);
}

void InitCheckerPatternTexture()
{
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

    glGenTextures(1, &g_textureId);
    glBindTexture(GL_TEXTURE_2D, g_textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);


    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURE_WIDTH, TEXTURE_HEIGHT,
        0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, &checkerImage[0]);
}

//We'll have one multisampled framebuffer with a single color and depth buffer.
//And a resolve buffer, where the mutisampling is resolved to.

GLuint msaa_fbo_id = -1; //Main FBO consisting of:
GLuint color_rbo_id = -1; // Main color
GLuint depth_rbo_id = -1; // Main depth

GLuint resolve_fbo_id = -1;  //Corresponding frame buffer
GLuint resolve_rbo_id = -1;  //Corresponding frame buffer

//Returns true if the framebuffer checks OK.
bool CheckFrameBuffer(){
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

bool InitFBO(){
    glGenFramebuffers(1, &msaa_fbo_id);
    glGenRenderbuffers(1, &color_rbo_id);
    glGenRenderbuffers(1, &depth_rbo_id);

    glGenFramebuffers(1, &resolve_fbo_id);
    glGenRenderbuffers(1, &resolve_rbo_id);

    if (msaa_fbo_id == -1){
        return false;
    }

    int aa_samples = 16;
    int width = 256;
    int height = 256;


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

//Blit all multisampled buffer back to main/resolve buffers
void ResolveAA(int width, int height){
    //Blit from multisampled buffer to main backbuffer = GL_COLOR_ATTACHMENT0
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_id);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

bool InitGL(){
    // Even though we aren't going to be rendering the scene to the window
    // we still need to create a dummy rendering context in order to load the
    // pbuffer extensions and to create our pbuffer.

    PIXELFORMATDESCRIPTOR pfd = {0};

    // Don't bother with anything fancy here. This is just a dummy rendering
    // context so just ask for the bare minimum.
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    if (!(wind->hDC = GetDC(wind->hWnd)))
        return false;

    int pf = ChoosePixelFormat(wind->hDC, &pfd);

    if (!SetPixelFormat(wind->hDC, pf, &pfd))
        return false;

    if (!(g_hRC = wglCreateContext(wind->hDC)))
        return false;

    if (!wglMakeCurrent(wind->hDC, g_hRC))
        return false;

    if (!InitGLExtensions())
        return false;

    if (!InitFBO()){
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

    return true;
}

bool Init(){
    if (!InitGL()){
        Cleanup();
        return false;
    }

    if (!ImageCreate(&g_image, IMAGE_WIDTH, IMAGE_HEIGHT)){
        Cleanup();
        return false;
    }

    return true;
}

void CopyBufferToImage(){
    // Copy the contents of the framebuffer - which in our case is our pbuffer -
    // to our bitmap image in local system memory. Notice that we also need
    // to invert the pbuffer's pixel data since OpenGL by default orients the
    // bitmap image bottom up. Our Windows DIB wrapper expects images to be
    // top down in orientation.

    static BYTE pixels[IMAGE_WIDTH * IMAGE_HEIGHT * 4] = {0};

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    for (int i = 0; i < IMAGE_HEIGHT; ++i)
    {
        memcpy(&g_image.pPixels[g_image.pitch * i],
            &pixels[((IMAGE_HEIGHT - 1) - i) * (IMAGE_WIDTH * 4)],
            IMAGE_WIDTH * 4);
    }
}

float rotation = 0.0f;

void DrawFrame(){
    // Once the pbuffer is created and its rendering context is made current
    // all normal OpenGL draw calls will affect the pbuffer's framebuffer NOT
    // the normal rendering context for the window.

    //Select the mutisampled frambuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glViewport(0, 0, IMAGE_WIDTH, IMAGE_HEIGHT);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (float)IMAGE_WIDTH / (float)IMAGE_HEIGHT, 1.0f, 100.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    // This seems to produce a nice smooth rotation for our cube.
    //glRotatef(timeGetTime() / 50.0f, 0.0f, 1.0f, 0.0f);
    rotation += 0.1f;
    glRotatef(rotation, 0.0f, 1.0f, 0.0f);

    //
    // Draw the unit cube.
    //

    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);

    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), g_cube[0].texcoord);
    glNormalPointer(GL_FLOAT, sizeof(Vertex), g_cube[0].normal);
    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), g_cube[0].pos);

    glDrawArrays(GL_TRIANGLES, 0, sizeof(g_cube) / sizeof(g_cube[0]));

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    ResolveAA(256,256);
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);

    // At this stage the pbuffer will contain our scene. Now we make a system
    // memory copy of the pbuffer.
    CopyBufferToImage();

    // Then we pre-multiply each pixel with its alpha component. This is how
    // the layered windows API expects images containing alpha information.
    ImagePreMultAlpha(&g_image);

    // Finally we update our layered window with our scene.
    RedrawLayeredWindow();

    //Called from the current context
    glFinish();

    SwapBuffers(wind->hDC);

    // Since we're doing off-screen rendering the frame rate will be
    // independent of the video display's refresh rate.
    g_frames++;
}


//Uses a WGL extension

void SetVSync(bool enable){
    if (wglSwapIntervalEXT){
        wglSwapIntervalEXT(enable);
        if (enable){
            debug->Ok("VSync: Enabled\n");
        }else{
            debug->Ok("VSync: Disabled\n");
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
    //Used to do things from console, like CTRL+C
    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler,TRUE)==FALSE){
        debug->Err("Unable to install a console handler!\n");
    }



    MSG msg = {0};

    debug->Info("WinMain hInstance = %lu\n",hInstance);
    debug->Info("GetModuleHandle = %lu\n",GetModuleHandle(NULL));

    Window::RegisterWindowClasses();
    wind = Window::CreateNewLayeredWindow(IMAGE_WIDTH,IMAGE_HEIGHT,&Window::wcs.at(0));
    wind_norm = Window::CreateNewWindow(IMAGE_WIDTH,IMAGE_HEIGHT,&Window::wcs.at(0));

    if (!wind){
        debug->Fatal("Unable to create window\n");
    }
    if (!wind_norm){
        debug->Fatal("Unable to create normal window\n");
    }

    if (Init()){
        wind->Show(nShowCmd);
        wind_norm->Show(nShowCmd);

        SetVSync(true);

        while (wind->f_should_quit == false){
            if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
                if (msg.message == WM_QUIT)
                    break;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }else{
                DrawFrame();
            }
        }
    }

    Cleanup();
    //UnregisterClass(wind->wc.lpszClassName, hInstance);

    return 0;
}
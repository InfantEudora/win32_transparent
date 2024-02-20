#include "glad.h"
#include "Debug.h"

static Debugger* debug = new Debugger("Glad",DEBUG_ALL);

PFNWGLCHOOSEPIXELFORMATARBPROC                    wglChoosePixelFormatARB;
PFNWGLGETPIXELFORMATATTRIBFVARBPROC               wglGetPixelFormatAttribfvARB;
PFNWGLGETPIXELFORMATATTRIBIVARBPROC               wglGetPixelFormatAttribivARB;

PFNWGLDESTROYPBUFFERARBPROC                       wglDestroyPbufferARB;
PFNWGLQUERYPBUFFERARBPROC                         wglQueryPbufferARB;
PFNWGLGETPBUFFERDCARBPROC                         wglGetPbufferDCARB;
PFNWGLCREATEPBUFFERARBPROC                        wglCreatePbufferARB;
PFNWGLRELEASEPBUFFERDCARBPROC                     wglReleasePbufferDCARB;

bool InitGLExtensions(void){
    #define GPA(x) wglGetProcAddress(x)

    // WGL_ARB_pbuffer.
    wglDestroyPbufferARB   = (PFNWGLDESTROYPBUFFERARBPROC)GPA("wglDestroyPbufferARB");
    wglQueryPbufferARB     = (PFNWGLQUERYPBUFFERARBPROC)GPA("wglQueryPbufferARB");
    wglGetPbufferDCARB     = (PFNWGLGETPBUFFERDCARBPROC)GPA("wglGetPbufferDCARB");
    wglCreatePbufferARB    = (PFNWGLCREATEPBUFFERARBPROC)GPA("wglCreatePbufferARB");
    wglReleasePbufferDCARB = (PFNWGLRELEASEPBUFFERDCARBPROC)GPA("wglReleasePbufferDCARB");

    // WGL_ARB_pixel_format.
    wglChoosePixelFormatARB      = (PFNWGLCHOOSEPIXELFORMATARBPROC)GPA("wglChoosePixelFormatARB");
    wglGetPixelFormatAttribfvARB = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)GPA("wglGetPixelFormatAttribfvARB");
    wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)GPA("wglGetPixelFormatAttribivARB");

    #undef GPA

    if (!wglDestroyPbufferARB || !wglQueryPbufferARB || !wglGetPbufferDCARB || !wglCreatePbufferARB || !wglReleasePbufferDCARB){
        debug->Fatal("Required extension WGL_ARB_pbuffer not supported");
    }

    if (!wglChoosePixelFormatARB || !wglGetPixelFormatAttribfvARB || !wglGetPixelFormatAttribivARB){
        debug->Fatal("Required extension WGL_ARB_pixel_format not supported");
    }
    return true;
}
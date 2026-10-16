#include "glad.h"
#include "Debug.h"

#if defined(__ANDROID__)

#include <EGL/egl.h>

/*
    The Android half of this file. Everything below the #else is the WGL loader; this is the same
    job for GLES, and it is small because GLES entry points are exported from libGLESv3 and want
    declaring rather than loading. Only EXTENSIONS have to be resolved, and eglGetProcAddress is
    how - see the note above these names in glad.h for why they carry a trailing underscore.
*/
PFNGLGENQUERIESEXTPROC          glGenQueriesEXT_          = NULL;
PFNGLDELETEQUERIESEXTPROC       glDeleteQueriesEXT_       = NULL;
PFNGLBEGINQUERYEXTPROC          glBeginQueryEXT_          = NULL;
PFNGLENDQUERYEXTPROC            glEndQueryEXT_            = NULL;
PFNGLGETQUERYOBJECTUIVEXTPROC   glGetQueryObjectuivEXT_   = NULL;
PFNGLGETQUERYOBJECTUI64VEXTPROC glGetQueryObjectui64vEXT_ = NULL;

PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC  glRenderbufferStorageMultisampleEXT_  = NULL;
PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT_ = NULL;

//Both of these are idempotent and cheap, so callers re-arm rather than tracking a 'loaded' flag:
//a context recreation invalidates nothing here (the pointers stay valid for the process) but
//calling again costs six lookups and removes the chance of reading a NULL after a reinit.
bool GLLoadTimerQueryEXT(){
    glGenQueriesEXT_          = (PFNGLGENQUERIESEXTPROC)         eglGetProcAddress("glGenQueriesEXT");
    glDeleteQueriesEXT_       = (PFNGLDELETEQUERIESEXTPROC)      eglGetProcAddress("glDeleteQueriesEXT");
    glBeginQueryEXT_          = (PFNGLBEGINQUERYEXTPROC)         eglGetProcAddress("glBeginQueryEXT");
    glEndQueryEXT_            = (PFNGLENDQUERYEXTPROC)           eglGetProcAddress("glEndQueryEXT");
    glGetQueryObjectuivEXT_   = (PFNGLGETQUERYOBJECTUIVEXTPROC)  eglGetProcAddress("glGetQueryObjectuivEXT");
    glGetQueryObjectui64vEXT_ = (PFNGLGETQUERYOBJECTUI64VEXTPROC)eglGetProcAddress("glGetQueryObjectui64vEXT");
    return glGenQueriesEXT_ && glDeleteQueriesEXT_ && glBeginQueryEXT_ && glEndQueryEXT_
        && glGetQueryObjectuivEXT_ && glGetQueryObjectui64vEXT_;
}

bool GLLoadMultisampleEXT(){
    glRenderbufferStorageMultisampleEXT_  = (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC) eglGetProcAddress("glRenderbufferStorageMultisampleEXT");
    glFramebufferTexture2DMultisampleEXT_ = (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
    return glRenderbufferStorageMultisampleEXT_ && glFramebufferTexture2DMultisampleEXT_;
}

#else
/*
    So, this is not actually glad, but only the needed thigs are pulled from one ginormous glad file.
    You could replace this with glad, it would just be bigger.
*/

static Debugger* debug = new Debugger("Glad",DEBUG_ALL);

#if defined(_WIN32)
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = NULL;
PFNWGLGETPIXELFORMATATTRIBFVARBPROC wglGetPixelFormatAttribfvARB = NULL;
PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB = NULL;

PFNWGLENUMGPUSNVPROC wglEnumGpusNV = NULL;
PFNWGLENUMGPUDEVICESNVPROC wglEnumGpuDevicesNV = NULL;

PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;
#else
static GLProcLoader gl_proc_loader = NULL;
static GLSwapIntervalSetter gl_swap_interval = NULL;

void GLSetPlatform(GLProcLoader loader, GLSwapIntervalSetter swap_interval){
    gl_proc_loader = loader;
    gl_swap_interval = swap_interval;
}
#endif

//Where every GL function below comes from - the one line in this loader that is per platform.
static void* LoadGLProc(const char* name){
#if defined(_WIN32)
    return (void*)wglGetProcAddress(name);
#else
    return gl_proc_loader ? gl_proc_loader(name) : NULL;
#endif
}

bool GLSetSwapInterval(int interval){
#if defined(_WIN32)
    if (!wglSwapIntervalEXT){
        return false;
    }
    wglSwapIntervalEXT(interval);
    return true;
#else
    return gl_swap_interval && gl_swap_interval(interval);
#endif
}

PFNGLCREATEFRAMEBUFFERSPROC glCreateFramebuffers;
PFNGLCREATERENDERBUFFERSPROC glCreateRenderbuffers;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = NULL;
PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer = NULL;
PFNGLCLEARNAMEDFRAMEBUFFERFVPROC glClearNamedFramebufferfv = NULL;
PFNGLCLEARNAMEDFRAMEBUFFERIVPROC glClearNamedFramebufferiv = NULL;
PFNGLNAMEDRENDERBUFFERSTORAGEPROC glNamedRenderbufferStorage = NULL;
PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC glNamedRenderbufferStorageMultisample = NULL;
PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC glNamedFramebufferRenderbuffer = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC glNamedFramebufferDrawBuffers = NULL;
PFNGLNAMEDFRAMEBUFFERTEXTUREPROC glNamedFramebufferTexture = NULL;
PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC glCheckNamedFramebufferStatus = NULL;
PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate = NULL;
PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate = NULL;

PFNGLSHADERSOURCEPROC glShaderSource = NULL;
PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = NULL;
PFNGLCREATESHADERPROC glCreateShader = NULL;
PFNGLCOMPILESHADERPROC glCompileShader = NULL;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;

PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
PFNGLATTACHSHADERPROC glAttachShader = NULL;
PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
PFNGLDELETESHADERPROC glDeleteShader = NULL;
PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
PFNGLDETACHSHADERPROC glDetachShader = NULL;
PFNGLUSEPROGRAMPROC glUseProgram = NULL;
PFNGLDISPATCHCOMPUTEPROC glDispatchCompute = NULL;

PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced = NULL;
PFNGLCREATEBUFFERSPROC glCreateBuffers = NULL;
PFNGLNAMEDBUFFERSTORAGEPROC glNamedBufferStorage = NULL;
PFNGLCREATEVERTEXARRAYSPROC glCreateVertexArrays = NULL;
PFNGLVERTEXARRAYVERTEXBUFFERPROC glVertexArrayVertexBuffer = NULL;
PFNGLENABLEVERTEXARRAYATTRIBPROC glEnableVertexArrayAttrib = NULL;
PFNGLVERTEXARRAYATTRIBFORMATPROC glVertexArrayAttribFormat = NULL;
PFNGLVERTEXARRAYATTRIBIFORMATPROC glVertexArrayAttribIFormat = NULL;
PFNGLVERTEXARRAYATTRIBBINDINGPROC glVertexArrayAttribBinding = NULL;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = NULL;

PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = NULL;

PFNGLBINDBUFFERBASEPROC glBindBufferBase = NULL;
PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = NULL;
PFNGLMAPNAMEDBUFFERRANGEPROC glMapNamedBufferRange = NULL;
PFNGLUNMAPNAMEDBUFFERPROC glUnmapNamedBuffer = NULL;
PFNGLFENCESYNCPROC glFenceSync = NULL;
PFNGLDELETESYNCPROC glDeleteSync = NULL;
PFNGLGETSYNCIVPROC glGetSynciv = NULL;
PFNGLNAMEDBUFFERDATAPROC glNamedBufferData = NULL;
PFNGLNAMEDBUFFERSUBDATAPROC glNamedBufferSubData = NULL;
PFNGLINVALIDATEBUFFERDATAPROC glInvalidateBufferData = NULL;
PFNGLGETNAMEDBUFFERSUBDATAPROC glGetNamedBufferSubData = NULL;

PFNGLCREATETEXTURESPROC glCreateTextures = NULL;
PFNGLTEXTUREPARAMETERIPROC glTextureParameteri = NULL;
PFNGLTEXTURESTORAGE2DPROC glTextureStorage2D = NULL;
PFNGLTEXTURESUBIMAGE2DPROC glTextureSubImage2D = NULL;
PFNGLTEXTURESTORAGE3DPROC glTextureStorage3D = NULL;
PFNGLTEXTURESUBIMAGE3DPROC glTextureSubImage3D = NULL;
PFNGLBINDTEXTUREUNITPROC glBindTextureUnit = NULL;
PFNGLBINDIMAGETEXTUREPROC glBindImageTexture = NULL;
PFNGLGENERATETEXTUREMIPMAPPROC glGenerateTextureMipmap = NULL;
PFNGLMEMORYBARRIERPROC glMemoryBarrier = NULL;

PFNGLGETTEXTUREHANDLEARBPROC glGetTextureHandleARB = NULL;
PFNGLMAKETEXTUREHANDLERESIDENTARBPROC glMakeTextureHandleResidentARB = NULL;
PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC glMakeTextureHandleNonResidentARB = NULL;

PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLGETUNIFORMFVPROC glGetUniformfv = NULL;
PFNGLGETUNIFORMIVPROC glGetUniformiv = NULL;
PFNGLUNIFORM1IPROC glUniform1i = NULL;
PFNGLUNIFORM1FVPROC glUniform1fv = NULL;
PFNGLUNIFORM3FPROC glUniform3f = NULL;
PFNGLUNIFORM3FVPROC glUniform3fv = NULL;
PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv = NULL;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;

PFNGLPROGRAMUNIFORM1IPROC glProgramUniform1i = NULL;
PFNGLPROGRAMUNIFORM1IVPROC glProgramUniform1iv = NULL;
PFNGLPROGRAMUNIFORM1FPROC glProgramUniform1f = NULL;
PFNGLPROGRAMUNIFORM1FVPROC glProgramUniform1fv = NULL;
PFNGLPROGRAMUNIFORM2FVPROC glProgramUniform2fv = NULL;
PFNGLPROGRAMUNIFORM3FVPROC glProgramUniform3fv = NULL;
PFNGLPROGRAMUNIFORM4FVPROC glProgramUniform4fv = NULL;
PFNGLPROGRAMUNIFORMMATRIX3FVPROC glProgramUniformMatrix3fv = NULL;
PFNGLPROGRAMUNIFORMMATRIX4FVPROC glProgramUniformMatrix4fv = NULL;

PFNGLGETINTEGERI_VPROC glGetIntegeri_v = NULL;
PFNGLGETSTRINGIPROC glGetStringi = NULL;

PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = NULL;
PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup = NULL;
PFNGLPOPDEBUGGROUPPROC glPopDebugGroup = NULL;

//Timer queries - see the block comment on these in glad.h.
PFNGLGENQUERIESPROC glGenQueries = NULL;
PFNGLDELETEQUERIESPROC glDeleteQueries = NULL;
PFNGLBEGINQUERYPROC glBeginQuery = NULL;
PFNGLENDQUERYPROC glEndQuery = NULL;
PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv = NULL;
PFNGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v = NULL;


#if defined(_WIN32)
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;

//Callable maybe after extensions are loaded
void GLSelectGPU(){

    int idx;
    DISPLAY_DEVICEA dd;
    HDC dc;
    PIXELFORMATDESCRIPTOR pfd;

    dd.cb = sizeof(dd);
    idx = 0;
    while (1) {
        if (!EnumDisplayDevicesA(NULL, idx, &dd, 0))
            break;
        debug->Info("Display %i DeviceID   : %s\n",idx,dd.DeviceID);
        debug->Info("Display %i DeviceName : %s\n",idx,dd.DeviceName);
        debug->Info("Display %i DeviceString : %s\n",idx,dd.DeviceString);
        idx += 1;
    }
    /*
    dc = CreateDCA(dd.DeviceName, NULL, NULL, NULL);
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    // those flags are not important, they just need to be valid (and nondemanding, just in case).
    // later you will use whatever flags you wish when you are creating your actual gl context
    pfd.dwFlags = PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER|PFD_DEPTH_DONTCARE;
    ChoosePixelFormat(dc, &pfd);
    DeleteDC(dc);*/




    HGPUNV hGPU = 0;
    for (UINT gpu_id = 0;gpu_id < 4;gpu_id++){
        if (!wglEnumGpusNV){
            debug->Warn("GPU selection with wglEnumGpusNV unsupported\n");
            return;
        }else{
            debug->Info("wglEnumGpusNV supported\n");
        }
        debug->Info("Looking for GPU %i\n",gpu_id);
        if (!wglEnumGpusNV(gpu_id, &hGPU)){
            debug->Info("No GPU at index %i\n",gpu_id);
            break;
        }

        if (wglEnumGpuDevicesNV){
            GPU_DEVICE gpuDevice;
            gpuDevice.cb = sizeof(gpuDevice);
            const bool found = wglEnumGpuDevicesNV(hGPU, 0, &gpuDevice);
            debug->Info("GPU: %s\n",gpuDevice.DeviceString);
        }else{
            debug->Warn("wglEnumGpuDevicesNV unsupported\n");
        }
    }
}
#endif


bool extensions_loaded = false;
bool InitGLExtensions(void){
    if (extensions_loaded){
        return true;
    }

#if defined(_WIN32)
    // WGL_ARB_pixel_format.
    wglChoosePixelFormatARB      = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    wglGetPixelFormatAttribfvARB = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)wglGetProcAddress("wglGetPixelFormatAttribfvARB");
    wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)wglGetProcAddress("wglGetPixelFormatAttribivARB");

    // WGL GPU selection
    wglEnumGpusNV = (PFNWGLENUMGPUSNVPROC)wglGetProcAddress("wglEnumGpusNV");

    //Swap
    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
#endif

    //Buffers
    glCreateFramebuffers = (PFNGLCREATEFRAMEBUFFERSPROC)LoadGLProc("glCreateFramebuffers");
    glCreateRenderbuffers = (PFNGLCREATERENDERBUFFERSPROC)LoadGLProc("glCreateRenderbuffers");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)LoadGLProc("glCheckFramebufferStatus");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)LoadGLProc("glBindFramebuffer");
    glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)LoadGLProc("glBindRenderbuffer");
    glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)LoadGLProc("glBlitFramebuffer");
    glClearNamedFramebufferfv = (PFNGLCLEARNAMEDFRAMEBUFFERFVPROC)LoadGLProc("glClearNamedFramebufferfv");
    glClearNamedFramebufferiv = (PFNGLCLEARNAMEDFRAMEBUFFERIVPROC)LoadGLProc("glClearNamedFramebufferiv");
    glNamedRenderbufferStorage = (PFNGLNAMEDRENDERBUFFERSTORAGEPROC)LoadGLProc("glNamedRenderbufferStorage");
    glNamedRenderbufferStorageMultisample = (PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC)LoadGLProc("glNamedRenderbufferStorageMultisample");
    glNamedFramebufferRenderbuffer = (PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC)LoadGLProc("glNamedFramebufferRenderbuffer");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)LoadGLProc("glFramebufferTexture2D");
    glNamedFramebufferDrawBuffers = (PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC)LoadGLProc("glNamedFramebufferDrawBuffers");
    glNamedFramebufferTexture = (PFNGLNAMEDFRAMEBUFFERTEXTUREPROC)LoadGLProc("glNamedFramebufferTexture");
    glCheckNamedFramebufferStatus = (PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC)LoadGLProc("glCheckNamedFramebufferStatus");
    glBlendEquationSeparate = (PFNGLBLENDEQUATIONSEPARATEPROC)LoadGLProc("glBlendEquationSeparate");
    glBlendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)LoadGLProc("glBlendFuncSeparate");

    glShaderSource = (PFNGLSHADERSOURCEPROC)LoadGLProc("glShaderSource");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)LoadGLProc("glGetShaderiv");
    glCreateShader = (PFNGLCREATESHADERPROC)LoadGLProc("glCreateShader");
    glCompileShader = (PFNGLCOMPILESHADERPROC)LoadGLProc("glCompileShader");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)LoadGLProc("glGetShaderInfoLog");

    glCreateProgram = (PFNGLCREATEPROGRAMPROC)LoadGLProc("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)LoadGLProc("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)LoadGLProc("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)LoadGLProc("glGetProgramiv");
    glGetActiveUniform = (PFNGLGETACTIVEUNIFORMPROC)LoadGLProc("glGetActiveUniform");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)LoadGLProc("glGetProgramInfoLog");
    glDeleteShader = (PFNGLDELETESHADERPROC)LoadGLProc("glDeleteShader");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)LoadGLProc("glDeleteProgram");
    glDetachShader = (PFNGLDETACHSHADERPROC)LoadGLProc("glDetachShader");
    glUseProgram = (PFNGLUSEPROGRAMPROC)LoadGLProc("glUseProgram");
    glDispatchCompute = (PFNGLDISPATCHCOMPUTEPROC)LoadGLProc("glDispatchCompute");

    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)LoadGLProc("glVertexAttribPointer");
    glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)LoadGLProc("glDrawArraysInstanced");
    glCreateBuffers = (PFNGLCREATEBUFFERSPROC)LoadGLProc("glCreateBuffers");
    glNamedBufferStorage = (PFNGLNAMEDBUFFERSTORAGEPROC)LoadGLProc("glNamedBufferStorage");
    glCreateVertexArrays = (PFNGLCREATEVERTEXARRAYSPROC)LoadGLProc("glCreateVertexArrays");
    glVertexArrayVertexBuffer = (PFNGLVERTEXARRAYVERTEXBUFFERPROC)LoadGLProc("glVertexArrayVertexBuffer");
    glEnableVertexArrayAttrib = (PFNGLENABLEVERTEXARRAYATTRIBPROC)LoadGLProc("glEnableVertexArrayAttrib");
    glVertexArrayAttribFormat = (PFNGLVERTEXARRAYATTRIBFORMATPROC)LoadGLProc("glVertexArrayAttribFormat");
    glVertexArrayAttribIFormat = (PFNGLVERTEXARRAYATTRIBIFORMATPROC)LoadGLProc("glVertexArrayAttribIFormat");
    glVertexArrayAttribBinding = (PFNGLVERTEXARRAYATTRIBBINDINGPROC)LoadGLProc("glVertexArrayAttribBinding");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)LoadGLProc("glBindVertexArray");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)LoadGLProc("glGenVertexArrays");


    glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)LoadGLProc("glBindBufferBase");
    glBindBuffer = (PFNGLBINDBUFFERPROC)LoadGLProc("glBindBuffer");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)LoadGLProc("glDeleteBuffers");
    glMapNamedBufferRange = (PFNGLMAPNAMEDBUFFERRANGEPROC)LoadGLProc("glMapNamedBufferRange");
    glUnmapNamedBuffer = (PFNGLUNMAPNAMEDBUFFERPROC)LoadGLProc("glUnmapNamedBuffer");
    glFenceSync = (PFNGLFENCESYNCPROC)LoadGLProc("glFenceSync");
    glDeleteSync = (PFNGLDELETESYNCPROC)LoadGLProc("glDeleteSync");
    glGetSynciv = (PFNGLGETSYNCIVPROC)LoadGLProc("glGetSynciv");
    glNamedBufferData = (PFNGLNAMEDBUFFERDATAPROC)LoadGLProc("glNamedBufferData");
    glNamedBufferSubData = (PFNGLNAMEDBUFFERSUBDATAPROC)LoadGLProc("glNamedBufferSubData");
    glInvalidateBufferData = (PFNGLINVALIDATEBUFFERDATAPROC)LoadGLProc("glInvalidateBufferData");
    glGetNamedBufferSubData = (PFNGLGETNAMEDBUFFERSUBDATAPROC)LoadGLProc("glGetNamedBufferSubData");

    glCreateTextures = (PFNGLCREATETEXTURESPROC)LoadGLProc("glCreateTextures");
    glTextureParameteri = (PFNGLTEXTUREPARAMETERIPROC)LoadGLProc("glTextureParameteri");
    glTextureStorage2D = (PFNGLTEXTURESTORAGE2DPROC)LoadGLProc("glTextureStorage2D");
    glTextureSubImage2D = (PFNGLTEXTURESUBIMAGE2DPROC)LoadGLProc("glTextureSubImage2D");
    glTextureStorage3D = (PFNGLTEXTURESTORAGE3DPROC)LoadGLProc("glTextureStorage3D");
    glTextureSubImage3D = (PFNGLTEXTURESUBIMAGE3DPROC)LoadGLProc("glTextureSubImage3D");
    glBindTextureUnit = (PFNGLBINDTEXTUREUNITPROC)LoadGLProc("glBindTextureUnit");
    glBindImageTexture = (PFNGLBINDIMAGETEXTUREPROC)LoadGLProc("glBindImageTexture");
    glGenerateTextureMipmap = (PFNGLGENERATETEXTUREMIPMAPPROC)LoadGLProc("glGenerateTextureMipmap");
    glMemoryBarrier = (PFNGLMEMORYBARRIERPROC)LoadGLProc("glMemoryBarrier");

    glGetTextureHandleARB = (PFNGLGETTEXTUREHANDLEARBPROC)LoadGLProc("glGetTextureHandleARB");
    glMakeTextureHandleResidentARB = (PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)LoadGLProc("glMakeTextureHandleResidentARB");
    glMakeTextureHandleNonResidentARB = (PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)LoadGLProc("glMakeTextureHandleNonResidentARB");

    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)LoadGLProc("glGetUniformLocation");
    glGetUniformfv = (PFNGLGETUNIFORMFVPROC)LoadGLProc("glGetUniformfv");
    glGetUniformiv = (PFNGLGETUNIFORMIVPROC)LoadGLProc("glGetUniformiv");
    glUniform1i = (PFNGLUNIFORM1IPROC)LoadGLProc("glUniform1i");
    glUniform1fv = (PFNGLUNIFORM1FVPROC)LoadGLProc("glUniform1fv");
    glUniform3f = (PFNGLUNIFORM3FPROC)LoadGLProc("glUniform3f");
    glUniform3fv = (PFNGLUNIFORM3FVPROC)LoadGLProc("glUniform3fv");
    glUniformMatrix3fv = (PFNGLUNIFORMMATRIX3FVPROC)LoadGLProc("glUniformMatrix3fv");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)LoadGLProc("glUniformMatrix4fv");

    glProgramUniform1i = (PFNGLPROGRAMUNIFORM1IPROC)LoadGLProc("glProgramUniform1i");
    glProgramUniform1iv = (PFNGLPROGRAMUNIFORM1IVPROC)LoadGLProc("glProgramUniform1iv");
    glProgramUniform1f = (PFNGLPROGRAMUNIFORM1FPROC)LoadGLProc("glProgramUniform1f");
    glProgramUniform1fv = (PFNGLPROGRAMUNIFORM1FVPROC)LoadGLProc("glProgramUniform1fv");
    glProgramUniform2fv = (PFNGLPROGRAMUNIFORM2FVPROC)LoadGLProc("glProgramUniform2fv");
    glProgramUniform3fv = (PFNGLPROGRAMUNIFORM3FVPROC)LoadGLProc("glProgramUniform3fv");
    glProgramUniform4fv = (PFNGLPROGRAMUNIFORM4FVPROC)LoadGLProc("glProgramUniform4fv");
    glProgramUniformMatrix3fv = (PFNGLPROGRAMUNIFORMMATRIX3FVPROC)LoadGLProc("glProgramUniformMatrix3fv");
    glProgramUniformMatrix4fv = (PFNGLPROGRAMUNIFORMMATRIX4FVPROC)LoadGLProc("glProgramUniformMatrix4fv");

    glGetIntegeri_v = (PFNGLGETINTEGERI_VPROC)LoadGLProc("glGetIntegeri_v");
    glGetStringi = (PFNGLGETSTRINGIPROC)LoadGLProc("glGetStringi");

    glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)LoadGLProc("glDebugMessageCallback");
    glPushDebugGroup = (PFNGLPUSHDEBUGGROUPPROC)LoadGLProc("glPushDebugGroup");
    glPopDebugGroup = (PFNGLPOPDEBUGGROUPPROC)LoadGLProc("glPopDebugGroup");

    glGenQueries = (PFNGLGENQUERIESPROC)LoadGLProc("glGenQueries");
    glDeleteQueries = (PFNGLDELETEQUERIESPROC)LoadGLProc("glDeleteQueries");
    glBeginQuery = (PFNGLBEGINQUERYPROC)LoadGLProc("glBeginQuery");
    glEndQuery = (PFNGLENDQUERYPROC)LoadGLProc("glEndQuery");
    glGetQueryObjectuiv = (PFNGLGETQUERYOBJECTUIVPROC)LoadGLProc("glGetQueryObjectuiv");
    glGetQueryObjectui64v = (PFNGLGETQUERYOBJECTUI64VPROC)LoadGLProc("glGetQueryObjectui64v");


#if defined(_WIN32)
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    if (!wglChoosePixelFormatARB || !wglGetPixelFormatAttribfvARB || !wglGetPixelFormatAttribivARB){
        debug->Fatal("Required extension WGL_ARB_pixel_format not supported");
    }
#endif

    extensions_loaded = true;
    return true;
}

#endif //__ANDROID__
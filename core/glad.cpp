#include "glad.h"
#include "Debug.h"
/*
    So, this is not actually glad, but only the needed thigs are pulled from one ginormous glad file.
    You could replace this with glad, it would just be bigger.
*/

static Debugger* debug = new Debugger("Glad",DEBUG_ALL);

PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = NULL;
PFNWGLGETPIXELFORMATATTRIBFVARBPROC wglGetPixelFormatAttribfvARB = NULL;
PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB = NULL;

PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;

PFNGLCREATEFRAMEBUFFERSPROC glCreateFramebuffers;
PFNGLCREATERENDERBUFFERSPROC glCreateRenderbuffers;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = NULL;
PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer = NULL;
PFNGLCLEARNAMEDFRAMEBUFFERFVPROC glClearNamedFramebufferfv = NULL;
PFNGLNAMEDRENDERBUFFERSTORAGEPROC glNamedRenderbufferStorage = NULL;
PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC glNamedRenderbufferStorageMultisample = NULL;
PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC glNamedFramebufferRenderbuffer = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC glNamedFramebufferDrawBuffers = NULL;
PFNGLNAMEDFRAMEBUFFERTEXTUREPROC glNamedFramebufferTexture = NULL;

PFNGLSHADERSOURCEPROC glShaderSource = NULL;
PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
PFNGLCREATESHADERPROC glCreateShader = NULL;
PFNGLCOMPILESHADERPROC glCompileShader = NULL;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;

PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
PFNGLATTACHSHADERPROC glAttachShader = NULL;
PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
PFNGLDELETESHADERPROC glDeleteShader = NULL;
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
PFNGLNAMEDBUFFERDATAPROC glNamedBufferData = NULL;
PFNGLNAMEDBUFFERSUBDATAPROC glNamedBufferSubData = NULL;
PFNGLINVALIDATEBUFFERDATAPROC glInvalidateBufferData = NULL;
PFNGLGETNAMEDBUFFERSUBDATAPROC glGetNamedBufferSubData = NULL;

PFNGLCREATETEXTURESPROC glCreateTextures = NULL;
PFNGLTEXTUREPARAMETERIPROC glTextureParameteri = NULL;
PFNGLTEXTURESTORAGE2DPROC glTextureStorage2D = NULL;
PFNGLTEXTURESUBIMAGE2DPROC glTextureSubImage2D = NULL;
PFNGLBINDTEXTUREUNITPROC glBindTextureUnit = NULL;
PFNGLBINDIMAGETEXTUREPROC glBindImageTexture = NULL;
PFNGLGENERATETEXTUREMIPMAPPROC glGenerateTextureMipmap = NULL;
PFNGLMEMORYBARRIERPROC glMemoryBarrier = NULL;

PFNGLGETTEXTUREHANDLEARBPROC glGetTextureHandleARB = NULL;
PFNGLMAKETEXTUREHANDLERESIDENTARBPROC glMakeTextureHandleResidentARB = NULL;
PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC glMakeTextureHandleNonResidentARB = NULL;

PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLUNIFORM1IPROC glUniform1i = NULL;
PFNGLUNIFORM3FPROC glUniform3f = NULL;
PFNGLUNIFORM3FVPROC glUniform3fv = NULL;
PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv = NULL;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;

PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = NULL;

PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;

bool extensions_loaded = false;

bool InitGLExtensions(void){
    if (extensions_loaded){
        return true;
    }

    // WGL_ARB_pixel_format.
    wglChoosePixelFormatARB      = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    wglGetPixelFormatAttribfvARB = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)wglGetProcAddress("wglGetPixelFormatAttribfvARB");
    wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)wglGetProcAddress("wglGetPixelFormatAttribivARB");

    //Swap
    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

    //Buffers
    glCreateFramebuffers = (PFNGLCREATEFRAMEBUFFERSPROC)wglGetProcAddress("glCreateFramebuffers");
    glCreateRenderbuffers = (PFNGLCREATERENDERBUFFERSPROC)wglGetProcAddress("glCreateRenderbuffers");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)wglGetProcAddress("glBindRenderbuffer");
    glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");
    glClearNamedFramebufferfv = (PFNGLCLEARNAMEDFRAMEBUFFERFVPROC)wglGetProcAddress("glClearNamedFramebufferfv");
    glNamedRenderbufferStorage = (PFNGLNAMEDRENDERBUFFERSTORAGEPROC)wglGetProcAddress("glNamedRenderbufferStorage");
    glNamedRenderbufferStorageMultisample = (PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC)wglGetProcAddress("glNamedRenderbufferStorageMultisample");
    glNamedFramebufferRenderbuffer = (PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glNamedFramebufferRenderbuffer");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
    glNamedFramebufferDrawBuffers = (PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC)wglGetProcAddress("glNamedFramebufferDrawBuffers");
    glNamedFramebufferTexture = (PFNGLNAMEDFRAMEBUFFERTEXTUREPROC)wglGetProcAddress("glNamedFramebufferTexture");

    glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");

    glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
    glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
    glDetachShader = (PFNGLDETACHSHADERPROC)wglGetProcAddress("glDetachShader");
    glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    glDispatchCompute = (PFNGLDISPATCHCOMPUTEPROC)wglGetProcAddress("glDispatchCompute");

    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)wglGetProcAddress("glDrawArraysInstanced");
    glCreateBuffers = (PFNGLCREATEBUFFERSPROC)wglGetProcAddress("glCreateBuffers");
    glNamedBufferStorage = (PFNGLNAMEDBUFFERSTORAGEPROC)wglGetProcAddress("glNamedBufferStorage");
    glCreateVertexArrays = (PFNGLCREATEVERTEXARRAYSPROC)wglGetProcAddress("glCreateVertexArrays");
    glVertexArrayVertexBuffer = (PFNGLVERTEXARRAYVERTEXBUFFERPROC)wglGetProcAddress("glVertexArrayVertexBuffer");
    glEnableVertexArrayAttrib = (PFNGLENABLEVERTEXARRAYATTRIBPROC)wglGetProcAddress("glEnableVertexArrayAttrib");
    glVertexArrayAttribFormat = (PFNGLVERTEXARRAYATTRIBFORMATPROC)wglGetProcAddress("glVertexArrayAttribFormat");
    glVertexArrayAttribIFormat = (PFNGLVERTEXARRAYATTRIBIFORMATPROC)wglGetProcAddress("glVertexArrayAttribIFormat");
    glVertexArrayAttribBinding = (PFNGLVERTEXARRAYATTRIBBINDINGPROC)wglGetProcAddress("glVertexArrayAttribBinding");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");


    glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)wglGetProcAddress("glBindBufferBase");
    glNamedBufferData = (PFNGLNAMEDBUFFERDATAPROC)wglGetProcAddress("glNamedBufferData");
    glNamedBufferSubData = (PFNGLNAMEDBUFFERSUBDATAPROC)wglGetProcAddress("glNamedBufferSubData");
    glInvalidateBufferData = (PFNGLINVALIDATEBUFFERDATAPROC)wglGetProcAddress("glInvalidateBufferData");
    glGetNamedBufferSubData = (PFNGLGETNAMEDBUFFERSUBDATAPROC)wglGetProcAddress("glGetNamedBufferSubData");

    glCreateTextures = (PFNGLCREATETEXTURESPROC)wglGetProcAddress("glCreateTextures");
    glTextureParameteri = (PFNGLTEXTUREPARAMETERIPROC)wglGetProcAddress("glTextureParameteri");
    glTextureStorage2D = (PFNGLTEXTURESTORAGE2DPROC)wglGetProcAddress("glTextureStorage2D");
    glTextureSubImage2D = (PFNGLTEXTURESUBIMAGE2DPROC)wglGetProcAddress("glTextureSubImage2D");
    glBindTextureUnit = (PFNGLBINDTEXTUREUNITPROC)wglGetProcAddress("glBindTextureUnit");
    glBindImageTexture = (PFNGLBINDIMAGETEXTUREPROC)wglGetProcAddress("glBindImageTexture");
    glGenerateTextureMipmap = (PFNGLGENERATETEXTUREMIPMAPPROC)wglGetProcAddress("glGenerateTextureMipmap");
    glMemoryBarrier = (PFNGLMEMORYBARRIERPROC)wglGetProcAddress("glMemoryBarrier");

    glGetTextureHandleARB = (PFNGLGETTEXTUREHANDLEARBPROC)wglGetProcAddress("glGetTextureHandleARB");
    glMakeTextureHandleResidentARB = (PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)wglGetProcAddress("glMakeTextureHandleResidentARB");
    glMakeTextureHandleNonResidentARB = (PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)wglGetProcAddress("glMakeTextureHandleNonResidentARB");

    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
    glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
    glUniform3fv = (PFNGLUNIFORM3FVPROC)wglGetProcAddress("glUniform3fv");
    glUniformMatrix3fv = (PFNGLUNIFORMMATRIX3FVPROC)wglGetProcAddress("glUniformMatrix3fv");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");

    glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)wglGetProcAddress("glDebugMessageCallback");

    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    if (!wglChoosePixelFormatARB || !wglGetPixelFormatAttribfvARB || !wglGetPixelFormatAttribivARB){
        debug->Fatal("Required extension WGL_ARB_pixel_format not supported");
    }

    extensions_loaded = true;
    return true;
}
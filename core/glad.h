#ifndef __glad_h_
#define __glad_h_

/*
    THE PLATFORM'S GL HEADER, and the one place in core/ that knows there is more than one.

    Everything below this guard is desktop-only: WGL, the Win32 types it is written in (HDC,
    HGLRC, DWORD, RECT), and a long list of `GLAPI PFNGL...PROC glSomething;` externs that
    glad.cpp resolves through wglGetProcAddress. Android needs none of it - the GLES entry points
    are exported straight out of the NDK's libGLESv2.so, so they want declaring, not loading, and
    <GLES3/gl31.h> declares them.

    THE EXTERNS MUST STAY BEHIND THIS GUARD RATHER THAN BEING DECLARED ON BOTH SIDES, and that is
    the whole reason the switch is here and not in a shared "GL.h" that offers everything to
    everyone. A visible `GLAPI PFNGLNAMEDBUFFERDATAPROC glNamedBufferData;` would let every Direct
    State Access call in this tree COMPILE for Android and then fail at link - or, if something
    ever supplied the symbol, read as a null function pointer at runtime. GLES has no DSA at any
    version up to 3.2 (measured, not assumed - see the note at the top of UIOverlay.cpp), so a
    desktop-only call has to fail here, loudly, at the compile that first sees it.

    WHY THIS FILE AND NOT EACH CONSUMER. Mesh.h, Texture.h, Material.h, Renderer.h and CubeMap.h
    all open with `#include "glad.h"` and windows.h arrived through every one of them, so the
    first error out of an Android build of almost any file in core/ was 'windows.h' file not
    found - from glad.h:4, three includes deep, saying nothing about the real question. Putting
    the switch here fixes all five without touching them, and leaves them shareable with the
    Android port exactly as they stand.
*/
#if defined(__ANDROID__)

#include <GLES3/gl31.h>

#else

#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>

/*
    This named glad, because it might have been... but it isn't
*/

// WGL_ARB_pixel_format.
#define WGL_ACCELERATION_ARB                      0x2003
#define WGL_ACCUM_ALPHA_BITS_ARB                  0x2021
#define WGL_ACCUM_BITS_ARB                        0x201D
#define WGL_ACCUM_BLUE_BITS_ARB                   0x2020
#define WGL_ACCUM_GREEN_BITS_ARB                  0x201F
#define WGL_ACCUM_RED_BITS_ARB                    0x201E
#define WGL_ALPHA_BITS_ARB                        0x201B
#define WGL_ALPHA_SHIFT_ARB                       0x201C
#define WGL_AUX_BUFFERS_ARB                       0x2024
#define WGL_BLUE_BITS_ARB                         0x2019
#define WGL_BLUE_SHIFT_ARB                        0x201A
#define WGL_COLOR_BITS_ARB                        0x2014
#define WGL_DEPTH_BITS_ARB                        0x2022
#define WGL_DOUBLE_BUFFER_ARB                     0x2011
#define WGL_DRAW_TO_BITMAP_ARB                    0x2002
#define WGL_DRAW_TO_WINDOW_ARB                    0x2001
#define WGL_FULL_ACCELERATION_ARB                 0x2027
#define WGL_GENERIC_ACCELERATION_ARB              0x2026
#define WGL_GREEN_BITS_ARB                        0x2017
#define WGL_GREEN_SHIFT_ARB                       0x2018
#define WGL_NEED_PALETTE_ARB                      0x2004
#define WGL_NEED_SYSTEM_PALETTE_ARB               0x2005
#define WGL_NO_ACCELERATION_ARB                   0x2025
#define WGL_NUMBER_OVERLAYS_ARB                   0x2008
#define WGL_NUMBER_PIXEL_FORMATS_ARB              0x2000
#define WGL_NUMBER_UNDERLAYS_ARB                  0x2009
#define WGL_PIXEL_TYPE_ARB                        0x2013
#define WGL_RED_BITS_ARB                          0x2015
#define WGL_RED_SHIFT_ARB                         0x2016
#define WGL_SHARE_ACCUM_ARB                       0x200E
#define WGL_SHARE_DEPTH_ARB                       0x200C
#define WGL_SHARE_STENCIL_ARB                     0x200D
#define WGL_STENCIL_BITS_ARB                      0x2023
#define WGL_STEREO_ARB                            0x2012
#define WGL_SUPPORT_GDI_ARB                       0x200F
#define WGL_SUPPORT_OPENGL_ARB                    0x2010
#define WGL_SWAP_COPY_ARB                         0x2029
#define WGL_SWAP_EXCHANGE_ARB                     0x2028
#define WGL_SWAP_LAYER_BUFFERS_ARB                0x2006
#define WGL_SWAP_METHOD_ARB                       0x2007
#define WGL_SWAP_UNDEFINED_ARB                    0x202A
#define WGL_TRANSPARENT_ALPHA_VALUE_ARB           0x203A
#define WGL_TRANSPARENT_ARB                       0x200A
#define WGL_TRANSPARENT_BLUE_VALUE_ARB            0x2039
#define WGL_TRANSPARENT_GREEN_VALUE_ARB           0x2038
#define WGL_TRANSPARENT_INDEX_VALUE_ARB           0x203B
#define WGL_TRANSPARENT_RED_VALUE_ARB             0x2037
#define WGL_TYPE_COLORINDEX_ARB                   0x202C
#define WGL_TYPE_RGBA_ARB                         0x202B

#define GL_MAX_SAMPLES 0x8D57

// MEMORY BARRIERS
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#define GL_ELEMENT_ARRAY_BARRIER_BIT 0x00000002
#define GL_UNIFORM_BARRIER_BIT 0x00000004
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GL_COMMAND_BARRIER_BIT 0x00000040
#define GL_PIXEL_BUFFER_BARRIER_BIT 0x00000080
#define GL_TEXTURE_UPDATE_BARRIER_BIT 0x00000100
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400
#define GL_TRANSFORM_FEEDBACK_BARRIER_BIT 0x00000800
#define GL_ATOMIC_COUNTER_BARRIER_BIT 0x00001000

// FRAME/RENDER BUFFERS
#define GL_FRAMEBUFFER                                  0x8D40
#define GL_RENDERBUFFER                                 0x8D41

#define GL_FRAMEBUFFER_UNDEFINED                        0x8219
#define GL_FRAMEBUFFER_COMPLETE                         0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT            0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT    0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER           0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER           0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED                      0x8CDD
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE           0x8D56
#define GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS         0x8DA8

#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_COLOR_ATTACHMENT2 0x8CE2
#define GL_COLOR_ATTACHMENT3 0x8CE3
#define GL_DEPTH_ATTACHMENT 0x8D00

#define GL_DRAW_BUFFER0 0x8825

#define GL_BGRA 0x80E1
#define GL_RGB16F 0x881B
#define GL_RGBA16F 0x881A
#define GL_RG16F 0x822F
#define GL_RGBA32F 0x8814
#define GL_RGB32F 0x8815
#define GL_R32I 0x8235
#define GL_R32UI 0x8236
//Single-channel 8-bit. GL 3.0, so not in the <GL/gl.h> this sits on top of, which only knows
//the 1.1 formats. The cloud shadow map stores transmittance in it.
#define GL_R8 0x8229
//The r16f upgrade path for that map, if 8 bits ever bands on a large flat receiver.
#define GL_R16F 0x822D
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_RED_INTEGER 0x8D94

//GL 1.2, so not in the <GL/gl.h> this header sits on top of. Needed by Texture::Create3D.
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_WRAP_R 0x8072

#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9

#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA

//Shaders
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPUTE_SHADER 0x91B9

#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_ACTIVE_UNIFORMS 0x8B86

//The GLSL types glGetActiveUniform reports back. GL_FLOAT, GL_INT and friends come from the
//ancient GL/gl.h above; the composite ones arrived with GL 2.0 and do not. Anything reflecting
//over a program's uniforms needs them to know what widget to draw - Application::RenderShaderUI
//could only tell a float from an int for exactly this reason.
#define GL_FLOAT_VEC2 0x8B50
#define GL_FLOAT_VEC3 0x8B51
#define GL_FLOAT_VEC4 0x8B52
#define GL_INT_VEC2 0x8B53
#define GL_INT_VEC3 0x8B54
#define GL_INT_VEC4 0x8B55
#define GL_BOOL 0x8B56
#define GL_BOOL_VEC2 0x8B57
#define GL_BOOL_VEC3 0x8B58
#define GL_BOOL_VEC4 0x8B59
#define GL_FLOAT_MAT2 0x8B5A
#define GL_FLOAT_MAT3 0x8B5B
#define GL_FLOAT_MAT4 0x8B5C
#define GL_SAMPLER_2D 0x8B5E
#define GL_SAMPLER_3D 0x8B5F
#define GL_SAMPLER_CUBE 0x8B60

//VBOs
#define GL_ARRAY_BUFFER 0x8892
#define GL_DYNAMIC_STORAGE_BIT 0x0100

//WGL context
#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define ERROR_INVALID_VERSION_ARB 0x2095
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

//Textures
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_MIRRORED_REPEAT                0x8370
#define GL_MAX_TEXTURE_IMAGE_UNITS        0x8872
#define GL_TEXTURE_CUBE_MAP               0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP       0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X    0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X    0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y    0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y    0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z    0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z    0x851A


//SSBO
#define GL_SHADER_STORAGE_BUFFER 0x90D2

#define GL_STREAM_DRAW 0x88E0
#define GL_STREAM_READ 0x88E1
#define GL_STREAM_COPY 0x88E2
#define GL_STATIC_DRAW 0x88E4
#define GL_STATIC_READ 0x88E5
#define GL_STATIC_COPY 0x88E6
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_DYNAMIC_READ 0x88E9
#define GL_DYNAMIC_COPY 0x88EA

//COMPUTE
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT 0x91BE
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE 0x91BF

//DEBUGGING
#define GL_DEBUG_OUTPUT 0x92E0

#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH 0x8243
#define GL_DEBUG_CALLBACK_FUNCTION 0x8244
#define GL_DEBUG_CALLBACK_USER_PARAM 0x8245
#define GL_DEBUG_SOURCE_API 0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM 0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY 0x8249
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#define GL_DEBUG_SOURCE_OTHER 0x824B
#define GL_DEBUG_TYPE_ERROR 0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#define GL_DEBUG_TYPE_OTHER 0x8251
#define GL_MAX_DEBUG_MESSAGE_LENGTH 0x9143
#define GL_MAX_DEBUG_LOGGED_MESSAGES 0x9144
#define GL_DEBUG_LOGGED_MESSAGES 0x9145
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#define GL_DEBUG_SEVERITY_LOW 0x9148
#define GL_DEBUG_TYPE_MARKER 0x8268
#define GL_DEBUG_TYPE_PUSH_GROUP 0x8269
#define GL_DEBUG_TYPE_POP_GROUP 0x826A
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#define GL_MAX_DEBUG_GROUP_STACK_DEPTH 0x826C
#define GL_DEBUG_GROUP_STACK_DEPTH 0x826D

//GPU selection
struct _GPU_DEVICE {
    DWORD  cb;
    CHAR   DeviceName[32];
    CHAR   DeviceString[128];
    DWORD  Flags;
    RECT   rcVirtualScreen;
};
DECLARE_HANDLE(HGPUNV);
typedef struct _GPU_DEVICE GPU_DEVICE;
typedef struct _GPU_DEVICE *PGPU_DEVICE;

bool InitGLExtensions(void);
void GLSelectGPU();

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#define GLAPI extern

typedef signed long long int GLsizeiptr;
typedef intptr_t    GLintptr;
typedef uint64_t    GLuint64;
typedef char GLchar;

typedef GLenum (APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRYP PFNGLBINDRENDERBUFFEREXTPROC)(GLenum target, GLuint renderbuffer);
typedef void (APIENTRYP PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);

//Window creation
typedef BOOL (WINAPI * PFNWGLCHOOSEPIXELFORMATARBPROC) (HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
GLAPI PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
typedef BOOL (WINAPI * PFNWGLGETPIXELFORMATATTRIBFVARBPROC) (HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, FLOAT *pfValues);
GLAPI PFNWGLGETPIXELFORMATATTRIBFVARBPROC wglGetPixelFormatAttribfvARB;
typedef BOOL (WINAPI * PFNWGLGETPIXELFORMATATTRIBIVARBPROC) (HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, int *piValues);
GLAPI PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB;

//GPU selection
typedef BOOL (APIENTRYP PFNWGLENUMGPUSNVPROC)(UINT iGpuIndex, HGPUNV *phGpu);
GLAPI PFNWGLENUMGPUSNVPROC wglEnumGpusNV;
typedef BOOL (APIENTRYP PFNWGLENUMGPUDEVICESNVPROC)(HGPUNV hGpu, UINT iDeviceIndex, PGPU_DEVICE lpGpuDevice);
GLAPI PFNWGLENUMGPUDEVICESNVPROC wglEnumGpuDevicesNV;

typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC)(GLint);
GLAPI PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;

//Render/Frame and Texture Buffers
typedef void (APIENTRYP PFNGLCREATEFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
GLAPI PFNGLCREATEFRAMEBUFFERSPROC glCreateFramebuffers;
typedef void (APIENTRYP PFNGLCREATERENDERBUFFERSPROC)(GLsizei n, GLuint *renderbuffers);
GLAPI PFNGLCREATERENDERBUFFERSPROC glCreateRenderbuffers;
GLAPI PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
GLAPI PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
GLAPI PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
typedef void (APIENTRYP PFNGLNAMEDRENDERBUFFERSTORAGEPROC)(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height);
GLAPI PFNGLNAMEDRENDERBUFFERSTORAGEPROC glNamedRenderbufferStorage;
typedef void (APIENTRYP PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC)(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
GLAPI PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC glNamedRenderbufferStorageMultisample;
typedef void (APIENTRYP PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC)(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
GLAPI PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC glNamedFramebufferRenderbuffer;
typedef void (APIENTRYP PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
GLAPI PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer;
typedef void (APIENTRYP PFNGLCLEARNAMEDFRAMEBUFFERFVPROC)(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value);
GLAPI PFNGLCLEARNAMEDFRAMEBUFFERFVPROC glClearNamedFramebufferfv;
typedef void (APIENTRYP PFNGLCLEARNAMEDFRAMEBUFFERIVPROC)(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value);
GLAPI PFNGLCLEARNAMEDFRAMEBUFFERIVPROC glClearNamedFramebufferiv;
typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
GLAPI PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
typedef void (APIENTRYP PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC)(GLuint framebuffer, GLsizei n, const GLenum *bufs);
GLAPI PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC glNamedFramebufferDrawBuffers;
typedef void (APIENTRYP PFNGLNAMEDFRAMEBUFFERTEXTUREPROC)(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level);
GLAPI PFNGLNAMEDFRAMEBUFFERTEXTUREPROC glNamedFramebufferTexture;
//The named form of glCheckFramebufferStatus above. Both are here because they answer for
//DIFFERENT framebuffers: the unnamed one reports on whatever is bound, so it can only be used at
//a point in the frame where that happens to be the one you just built. This one names its target
//and so can check a framebuffer from anywhere - which is what Renderer::RebuildLowResFBO needs,
//running as it does from an app's PreRender with the frame's own framebuffer bound.
typedef GLenum (APIENTRYP PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC)(GLuint framebuffer, GLenum target);
GLAPI PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC glCheckNamedFramebufferStatus;

//Blending. GL 1.1's glBlendFunc is exported by opengl32 and needs no loading; the EQUATION does,
//and MIN/MAX are the reason it is here - they reduce a target rather than compositing into it,
//which is how the occluder field collapses a whole column of geometry into one texel in a single
//pass (Renderer::RenderFieldPass). Note that both ignore the blend factors entirely.
#define GL_FUNC_ADD 0x8006
#define GL_MIN      0x8007
#define GL_MAX      0x8008
typedef void (APIENTRYP PFNGLBLENDEQUATIONSEPARATEPROC)(GLenum modeRGB, GLenum modeAlpha);
GLAPI PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate;
//Different FACTORS for colour and alpha, which glBlendFunc cannot express. Needed the moment
//anything renders into a framebuffer whose alpha is going to be READ BACK rather than presented:
//the ordinary SRC_ALPHA/ONE_MINUS_SRC_ALPHA applied to the alpha channel of a cleared buffer
//squares the coverage, which is invisible over an opaque frame and wrong in an off-screen one.
//See Renderer::CustomShaderPass, which composites its low-res target and therefore reads it.
typedef void (APIENTRYP PFNGLBLENDFUNCSEPARATEPROC)(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
GLAPI PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate;

//Shaders
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const char *const*string, const GLint *length);
GLAPI PFNGLSHADERSOURCEPROC glShaderSource;
typedef void (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
GLAPI PFNGLGETSHADERIVPROC glGetShaderiv;
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
GLAPI PFNGLCREATESHADERPROC glCreateShader;
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
GLAPI PFNGLCOMPILESHADERPROC glCompileShader;
typedef void (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, char *infoLog);
GLAPI PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
typedef void (APIENTRYP PFNGLDISPATCHCOMPUTEPROC)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
GLAPI PFNGLDISPATCHCOMPUTEPROC glDispatchCompute;

//Shader programs
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
GLAPI PFNGLCREATEPROGRAMPROC glCreateProgram;
typedef void (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
GLAPI PFNGLATTACHSHADERPROC glAttachShader;
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
GLAPI PFNGLLINKPROGRAMPROC glLinkProgram;
typedef void (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
GLAPI PFNGLGETPROGRAMIVPROC glGetProgramiv;
typedef void (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, char *infoLog);
GLAPI PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
typedef void (APIENTRYP PFNGLGETACTIVEUNIFORMPROC)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
GLAPI PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform;
typedef void (APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
GLAPI PFNGLDELETESHADERPROC glDeleteShader;
//For Shader::Reload, which links a replacement program and then has to release the old one. A
//reload that skipped this would leak one whole program per edit, which for a shader bench is a
//leak per keystroke-worth of iteration rather than a one-off.
typedef void (APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
GLAPI PFNGLDELETEPROGRAMPROC glDeleteProgram;
typedef void (APIENTRYP PFNGLDETACHSHADERPROC)(GLuint program, GLuint shader);
GLAPI PFNGLDETACHSHADERPROC glDetachShader;
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
GLAPI PFNGLUSEPROGRAMPROC glUseProgram;

//Uniforms
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const char *name);
GLAPI PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
//Reading a uniform back OUT of a program, which is how a reflected UI seeds its widgets: a
//`uniform float fog = 0.4;` carries its default in the GLSL, and the panel has no other way to
//learn it. Named programs like the glProgramUniform* setters, so bind order is irrelevant here too.
typedef void (APIENTRYP PFNGLGETUNIFORMFVPROC)(GLuint program, GLint location, GLfloat *params);
GLAPI PFNGLGETUNIFORMFVPROC glGetUniformfv;
typedef void (APIENTRYP PFNGLGETUNIFORMIVPROC)(GLuint program, GLint location, GLint *params);
GLAPI PFNGLGETUNIFORMIVPROC glGetUniformiv;
typedef void (APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
GLAPI PFNGLUNIFORM1IPROC glUniform1i;
typedef void (APIENTRYP PFNGLUNIFORM1FVPROC)(GLint location, GLsizei count, const GLfloat *value);
GLAPI PFNGLUNIFORM1FVPROC glUniform1fv;
typedef void (APIENTRYP PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
GLAPI PFNGLUNIFORM3FPROC glUniform3f;
typedef void (APIENTRYP PFNGLUNIFORM3FVPROC)(GLint location, GLsizei count, const GLfloat *value);
GLAPI PFNGLUNIFORM3FVPROC glUniform3fv;
typedef void (APIENTRYP PFNGLUNIFORMMATRIX3FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLAPI PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
typedef void (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLAPI PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;

//The same setters, but naming the program instead of writing to whichever one happens to be
//bound. glUniform* silently lands in the currently bound program, so setting a uniform on a
//shader you have not Use()d writes it into a different shader - see Shader::Setint and friends,
//which all take the program they mean. GL 4.1; the context here is 4.5 core (Window.cpp).
typedef void (APIENTRYP PFNGLPROGRAMUNIFORM1IPROC)(GLuint program, GLint location, GLint v0);
GLAPI PFNGLPROGRAMUNIFORM1IPROC glProgramUniform1i;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORM1IVPROC)(GLuint program, GLint location, GLsizei count, const GLint *value);
GLAPI PFNGLPROGRAMUNIFORM1IVPROC glProgramUniform1iv;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORM1FPROC)(GLuint program, GLint location, GLfloat v0);
GLAPI PFNGLPROGRAMUNIFORM1FPROC glProgramUniform1f;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORM1FVPROC)(GLuint program, GLint location, GLsizei count, const GLfloat *value);
GLAPI PFNGLPROGRAMUNIFORM1FVPROC glProgramUniform1fv;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORM2FVPROC)(GLuint program, GLint location, GLsizei count, const GLfloat *value);
GLAPI PFNGLPROGRAMUNIFORM2FVPROC glProgramUniform2fv;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORM3FVPROC)(GLuint program, GLint location, GLsizei count, const GLfloat *value);
GLAPI PFNGLPROGRAMUNIFORM3FVPROC glProgramUniform3fv;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORM4FVPROC)(GLuint program, GLint location, GLsizei count, const GLfloat *value);
GLAPI PFNGLPROGRAMUNIFORM4FVPROC glProgramUniform4fv;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORMMATRIX3FVPROC)(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLAPI PFNGLPROGRAMUNIFORMMATRIX3FVPROC glProgramUniformMatrix3fv;
typedef void (APIENTRYP PFNGLPROGRAMUNIFORMMATRIX4FVPROC)(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLAPI PFNGLPROGRAMUNIFORMMATRIX4FVPROC glProgramUniformMatrix4fv;

//VBO/VAO
typedef void (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
GLAPI PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
typedef void (APIENTRYP PFNGLDRAWARRAYSINSTANCEDPROC)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
GLAPI PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced;
typedef void (APIENTRYP PFNGLCREATEBUFFERSPROC)(GLsizei n, GLuint *buffers);
GLAPI PFNGLCREATEBUFFERSPROC glCreateBuffers;
typedef void (APIENTRYP PFNGLNAMEDBUFFERSTORAGEPROC)(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags);
GLAPI PFNGLNAMEDBUFFERSTORAGEPROC glNamedBufferStorage;
typedef void (APIENTRYP PFNGLCREATEVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
GLAPI PFNGLCREATEVERTEXARRAYSPROC glCreateVertexArrays;
typedef void (APIENTRYP PFNGLVERTEXARRAYVERTEXBUFFERPROC)(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride);
GLAPI PFNGLVERTEXARRAYVERTEXBUFFERPROC glVertexArrayVertexBuffer;
typedef void (APIENTRYP PFNGLENABLEVERTEXARRAYATTRIBPROC)(GLuint vaobj, GLuint index);
GLAPI PFNGLENABLEVERTEXARRAYATTRIBPROC glEnableVertexArrayAttrib;
typedef void (APIENTRYP PFNGLVERTEXARRAYATTRIBFORMATPROC)(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset);
GLAPI PFNGLVERTEXARRAYATTRIBFORMATPROC glVertexArrayAttribFormat;
typedef void (APIENTRYP PFNGLVERTEXARRAYATTRIBIFORMATPROC)(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);
GLAPI PFNGLVERTEXARRAYATTRIBIFORMATPROC glVertexArrayAttribIFormat;
typedef void (APIENTRYP PFNGLVERTEXARRAYATTRIBBINDINGPROC)(GLuint vaobj, GLuint attribindex, GLuint bindingindex);
GLAPI PFNGLVERTEXARRAYATTRIBBINDINGPROC glVertexArrayAttribBinding;
typedef void (APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint array);
GLAPI PFNGLBINDVERTEXARRAYPROC glBindVertexArray;

typedef void (APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
GLAPI PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;


//SSBOs
typedef void (APIENTRYP PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);
GLAPI PFNGLBINDBUFFERBASEPROC glBindBufferBase;
typedef void (APIENTRYP PFNGLNAMEDBUFFERDATAPROC)(GLuint buffer, GLsizeiptr size, const void *data, GLenum usage);
GLAPI PFNGLNAMEDBUFFERDATAPROC glNamedBufferData;
typedef void (APIENTRYP PFNGLNAMEDBUFFERSUBDATAPROC)(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data);
GLAPI PFNGLNAMEDBUFFERSUBDATAPROC glNamedBufferSubData;
typedef void (APIENTRYP PFNGLINVALIDATEBUFFERDATAPROC)(GLuint buffer);
GLAPI PFNGLINVALIDATEBUFFERDATAPROC glInvalidateBufferData;
typedef void (APIENTRYP PFNGLGETNAMEDBUFFERSUBDATAPROC)(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data);
GLAPI PFNGLGETNAMEDBUFFERSUBDATAPROC glGetNamedBufferSubData;

//Textures
typedef void (APIENTRYP PFNGLCREATETEXTURESPROC)(GLenum target, GLsizei n, GLuint *textures);
GLAPI PFNGLCREATETEXTURESPROC glCreateTextures;
typedef void (APIENTRYP PFNGLTEXTUREPARAMETERIPROC)(GLuint texture, GLenum pname, GLint param);
GLAPI PFNGLTEXTUREPARAMETERIPROC glTextureParameteri;
typedef void (APIENTRYP PFNGLTEXTURESTORAGE2DPROC)(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
GLAPI PFNGLTEXTURESTORAGE2DPROC glTextureStorage2D;
typedef void (APIENTRYP PFNGLTEXTURESUBIMAGE2DPROC)(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
GLAPI PFNGLTEXTURESUBIMAGE2DPROC glTextureSubImage2D;
typedef void (APIENTRYP PFNGLTEXTURESTORAGE3DPROC)(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);
GLAPI PFNGLTEXTURESTORAGE3DPROC glTextureStorage3D;
typedef void (APIENTRYP PFNGLTEXTURESUBIMAGE3DPROC)(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels);
GLAPI PFNGLTEXTURESUBIMAGE3DPROC glTextureSubImage3D;
typedef void (APIENTRYP PFNGLBINDTEXTUREUNITPROC)(GLuint unit, GLuint texture);
GLAPI PFNGLBINDTEXTUREUNITPROC glBindTextureUnit;
typedef void (APIENTRYP PFNGLBINDIMAGETEXTUREPROC)(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format);
GLAPI PFNGLBINDIMAGETEXTUREPROC glBindImageTexture;
typedef void (APIENTRYP PFNGLGENERATETEXTUREMIPMAPPROC)(GLuint texture);
GLAPI PFNGLGENERATETEXTUREMIPMAPPROC glGenerateTextureMipmap;
typedef void (APIENTRYP PFNGLMEMORYBARRIERPROC)(GLbitfield barriers);
GLAPI PFNGLMEMORYBARRIERPROC glMemoryBarrier;

//Bindless Textures
typedef GLuint64 (APIENTRYP PFNGLGETTEXTUREHANDLEARBPROC)(GLuint texture);
GLAPI PFNGLGETTEXTUREHANDLEARBPROC glGetTextureHandleARB;
typedef void (APIENTRYP PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)(GLuint64 handle);
GLAPI PFNGLMAKETEXTUREHANDLERESIDENTARBPROC glMakeTextureHandleResidentARB;
typedef void (APIENTRYP PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)(GLuint64 handle);
GLAPI PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC glMakeTextureHandleNonResidentARB;

//Information getters
typedef void (APIENTRYP PFNGLGETINTEGERI_VPROC)(GLenum target, GLuint index, GLint *data);
GLAPI PFNGLGETINTEGERI_VPROC glGetIntegeri_v;

//Debug
typedef void (APIENTRY *GLDEBUGPROC)(GLenum source,GLenum type,GLuint id,GLenum severity,GLsizei length,const char *message,const void *userParam);
typedef void (APIENTRYP PFNGLDEBUGMESSAGECALLBACKPROC)(GLDEBUGPROC callback, const void *userParam);
GLAPI PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;

//Timer queries - what Renderer's per-pass GPU timings are built on. Core GL 3.3, so unlike the
//Android port (which resolves the GL_EXT_disjoint_timer_query forms through eglGetProcAddress,
//because GLES has only those) there is nothing to feature-test here: a context that can run this
//renderer at all has these. GL_TIME_ELAPSED measures GPU execution between Begin and End, which
//is the entire point - a CPU timer around the same calls measures submission, and this codebase
//has twice concluded the hard way that the two are unrelated. See Renderer::GPUPassTimer.
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define GL_TIME_ELAPSED 0x88BF
typedef void (APIENTRYP PFNGLGENQUERIESPROC)(GLsizei n, GLuint *ids);
GLAPI PFNGLGENQUERIESPROC glGenQueries;
typedef void (APIENTRYP PFNGLDELETEQUERIESPROC)(GLsizei n, const GLuint *ids);
GLAPI PFNGLDELETEQUERIESPROC glDeleteQueries;
typedef void (APIENTRYP PFNGLBEGINQUERYPROC)(GLenum target, GLuint id);
GLAPI PFNGLBEGINQUERYPROC glBeginQuery;
typedef void (APIENTRYP PFNGLENDQUERYPROC)(GLenum target);
GLAPI PFNGLENDQUERYPROC glEndQuery;
typedef void (APIENTRYP PFNGLGETQUERYOBJECTUIVPROC)(GLuint id, GLenum pname, GLuint *params);
GLAPI PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv;
typedef void (APIENTRYP PFNGLGETQUERYOBJECTUI64VPROC)(GLuint id, GLenum pname, GLuint64 *params);
GLAPI PFNGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v;

//Debug groups. Free to place, and they cost nothing when nobody is capturing - but they are what
//turns a RenderDoc capture of this renderer from a wall of unnamed draws into the same pass list
//the Performance tab shows. Deliberately pushed from the same scopes as the timers above, so the
//name a timing is filed under and the name in a capture are the same string.
typedef void (APIENTRYP PFNGLPUSHDEBUGGROUPPROC)(GLenum source, GLuint id, GLsizei length, const GLchar *message);
GLAPI PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup;
typedef void (APIENTRYP PFNGLPOPDEBUGGROUPPROC)(void);
GLAPI PFNGLPOPDEBUGGROUPPROC glPopDebugGroup;

//WGL Contexts - When we want RenderDoc to work
typedef HGLRC (APIENTRYP PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);
GLAPI PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;

#endif //!defined(__ANDROID__)

#endif
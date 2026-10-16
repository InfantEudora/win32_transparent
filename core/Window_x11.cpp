/*
    The X11 window backend: Xlib for the window and its events, GLX for the GL context. What Window.cpp
    is to Win32 - see "THE CLASS IS THE PLATFORM SEAM" in Window.h - for Linux, where it covers every
    desktop: X11 sessions directly, and Wayland ones (GNOME's and KDE's by default) through XWayland,
    which all of them run. docs/linux_port_plan.md, step 2.

    LINKED, NOT LOADED: libX11 and libGL are on every Linux machine with a desktop, as user32 and
    opengl32 are on Windows. Opening them at run time instead would buy a friendlier message on a
    machine with no display at all, at a pointer per function. GL past 1.3 still comes through
    glXGetProcAddressARB, handed to glad in InitOpenGL.

    The input half - keys, buttons, motion, focus as the engine's events - is InputController's, in
    InputController_linux.cpp, the way HandleMessage is on Win32. The pump below hands it every event.
*/
#if defined(__linux__) && !defined(__ANDROID__)
#include "Window.h"
#include "Debug.h"
#include "glad.h"

#include <poll.h>
#include <stdlib.h>
#include <string.h>

/*
    XLIB NAMES ITS WINDOW TYPE `Window`, which is this engine's class. Renamed for the length of its
    headers, so the X type is XWindow in this file and the class keeps its name. The engine's headers
    are all included above for the same reason: Xlib also #defines None, Bool, Status and True.
*/
#define Window XWindow
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <GL/glx.h>
#undef Window

static Debugger* debug = new Debugger("Window",DEBUG_INFO);

struct WindowX11{
    Display*    display = NULL;
    XWindow     window = 0;
    GLXFBConfig config = NULL;
    GLXContext  context = NULL;
    Atom        wm_delete = 0;
    Atom        net_wm_state = 0;
    Atom        net_wm_state_fullscreen = 0;
    Atom        net_wm_state_above = 0;
    bool        f_shown = false;                //Show has run: _NET_WM_STATE is asked for, not set
    std::atomic<bool> f_mapped{false};          //read by the render loop, through IsMinimized
};

//For GetDisplaySize, which is static: the one connection the engine opens.
static Display* main_display = NULL;

//The swap interval is a GLX call on a DRAWABLE, made from the render thread through glad, so the one
//window it applies to is kept here. The engine opens one window.
typedef int (*SwapIntervalMESAFn)(unsigned int interval);
static Display*                  gl_display = NULL;
static XWindow                   gl_drawable = 0;
static PFNGLXSWAPINTERVALEXTPROC swap_interval_ext = NULL;
static SwapIntervalMESAFn        swap_interval_mesa = NULL;

static void* LoadGLXProc(const char* name){
    return (void*)glXGetProcAddressARB((const GLubyte*)name);
}

static bool SetGLXSwapInterval(int interval){
    if (swap_interval_ext){
        swap_interval_ext(gl_display,gl_drawable,interval);
        return true;
    }
    if (swap_interval_mesa){
        return swap_interval_mesa((unsigned int)interval) == 0;
    }
    return false;
}

/*
    Xlib's default answer to any protocol error is to end the process. Logged instead: no request made
    here is worth dying for, and one is EXPECTED to fail - a GL version the driver lacks is refused
    with an X error, not a NULL.
*/
static int LogXError(Display* display, XErrorEvent* e){
    char text[256] = "";
    XGetErrorText(display,e->error_code,text,sizeof(text));
    debug->Warn("X error: %s (request %u.%u)\n",text,(unsigned)e->request_code,(unsigned)e->minor_code);
    return 0;
}

//WM_CLOSE's two answers in Window.cpp: ask the app (f_close_asks_app), or close.
static void RequestClose(Window* wnd){
    if (wnd->f_close_asks_app){
        wnd->f_close_asked = true;
        return;
    }
    wnd->f_should_quit = true;
}

//_NET_WM_STATE, the EWMH way to ask for fullscreen or always-on-top. Before the window is shown the
//property is simply set; after, the window manager is asked, and it decides.
static void SetNetWMState(WindowX11* x, bool f_on, Atom state){
    if (!x->f_shown){
        if (f_on){
            XChangeProperty(x->display,x->window,x->net_wm_state,XA_ATOM,32,PropModeAppend,(unsigned char*)&state,1);
        }
        return;
    }
    XEvent e = {};
    e.xclient.type = ClientMessage;
    e.xclient.window = x->window;
    e.xclient.message_type = x->net_wm_state;
    e.xclient.format = 32;
    e.xclient.data.l[0] = f_on ? 1 : 0;     //_NET_WM_STATE_ADD or _REMOVE
    e.xclient.data.l[1] = (long)state;
    e.xclient.data.l[2] = 0;
    e.xclient.data.l[3] = 1;                //asked for by an application, not a pager
    XSendEvent(x->display,DefaultRootWindow(x->display),False,SubstructureRedirectMask | SubstructureNotifyMask,&e);
    XFlush(x->display);
}

/*
    The window's own events, as windproc in Window.cpp has them - Escape and Alt+Enter, the close
    button, size and place, mapping and focus - and then every event to the InputController, as
    windproc ends by calling HandleMessage.
*/
static void HandleEvent(Window* wnd, XEvent& e){
    WindowX11* x = wnd->x11;
    switch (e.type){
        case KeyPress:{
            KeySym sym = XLookupKeysym(&e.xkey,0);
            if (sym == XK_Escape && wnd->f_escape_closes_window){
                RequestClose(wnd);
            }
            if (sym == XK_Return && (e.xkey.state & Mod1Mask)){
                wnd->SetFullscreen(!wnd->f_fullscreen);
            }
            break;
        }
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == x->wm_delete){
                RequestClose(wnd);
            }
            break;
        case ConfigureNotify:{
            int w = e.xconfigure.width;
            int h = e.xconfigure.height;
            if (w != wnd->width || h != wnd->height){
                wnd->width = w;
                wnd->height = h;
                debug->Info("Resize to %i x %i px\n",w,h);
                wnd->f_resized = true;
                if (!(wnd->f_istogglingfullscreen || wnd->f_fullscreen)){
                    wnd->width_windowed = w;
                    wnd->height_windowed = h;
                }
            }
            //Where the client area is on the screen. The event's own x and y are relative to the
            //window manager's frame, so asked for rather than read.
            int rx = 0;
            int ry = 0;
            XWindow child = 0;
            XTranslateCoordinates(x->display,x->window,DefaultRootWindow(x->display),0,0,&rx,&ry,&child);
            wnd->left = rx;
            wnd->top = ry;
            if (wnd->inputcontroller){
                wnd->inputcontroller->ReportWindowPosition(int2(rx,ry));
            }
            break;
        }
        case MapNotify:
            x->f_mapped = true;
            break;
        //What most window managers do to a minimised window - so this is IsMinimized's answer.
        case UnmapNotify:
            x->f_mapped = false;
            break;
        case FocusIn:
            if (e.xfocus.mode != NotifyGrab && e.xfocus.mode != NotifyUngrab){
                wnd->f_has_focus = true;
            }
            break;
        case FocusOut:
            if (e.xfocus.mode != NotifyGrab && e.xfocus.mode != NotifyUngrab){
                wnd->f_has_focus = false;
            }
            break;
        case DestroyNotify:
            debug->Warn("Window destroyed\n");
            wnd->f_should_quit = true;
            break;
        default:
            break;
    }
    if (wnd->inputcontroller){
        wnd->inputcontroller->HandleXEvent(&e);
    }
}

Window* Window::CreateNewWindow(int width, int height, int wc_index, const char* title){
    (void)wc_index;     //Win32's window class - X has nothing like it

    /*
        FIRST, before any other Xlib call in the process: the main thread pumps this connection while
        the render thread swaps buffers on it, and Xlib is only safe across threads when told so.
    */
    XInitThreads();
    XSetErrorHandler(LogXError);
    Display* display = XOpenDisplay(NULL);
    if (!display){
        const char* name = getenv("DISPLAY");
        debug->Err("Could not open the X display %s - is there a desktop, and is DISPLAY set?\n",name ? name : "(DISPLAY is not set)");
        return NULL;
    }
    int screen = DefaultScreen(display);

    /*
        8-bit RGB with depth and stencil, double buffered. A 24-bit visual where one is offered, not the
        32-bit one a compositor also offers: with an alpha channel the window would be see-through
        wherever the frame's alpha is not 1. The engine draws into its own framebuffers anyway, and
        only blits the result here (CopyBufferToBackBuffer).
    */
    static const int config_attributes[] = {
        GLX_X_RENDERABLE,   True,
        GLX_DRAWABLE_TYPE,  GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,    GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE,  GLX_TRUE_COLOR,
        GLX_RED_SIZE,       8,
        GLX_GREEN_SIZE,     8,
        GLX_BLUE_SIZE,      8,
        GLX_DEPTH_SIZE,     24,
        GLX_STENCIL_SIZE,   8,
        GLX_DOUBLEBUFFER,   True,
        None
    };
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display,screen,config_attributes,&count);
    GLXFBConfig config = NULL;
    XVisualInfo* visual = NULL;
    for (int i = 0; i < count && !config; i++){
        XVisualInfo* v = glXGetVisualFromFBConfig(display,configs[i]);
        if (v && v->depth == 24){
            config = configs[i];
            visual = v;
        }else if (v){
            XFree(v);
        }
    }
    if (!config && count > 0){
        config = configs[0];
        visual = glXGetVisualFromFBConfig(display,config);
    }
    if (configs){
        XFree(configs);
    }
    if (!config || !visual){
        debug->Err("No GLX framebuffer with 8-bit RGB, depth and stencil on this display\n");
        XCloseDisplay(display);
        return NULL;
    }

    XSetWindowAttributes wa = {};
    wa.colormap = XCreateColormap(display,RootWindow(display,screen),visual->visual,AllocNone);
    wa.border_pixel = 0;
    wa.background_pixmap = None;    //nothing cleared under the frame, so a resize does not flash
    wa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
                  | EnterWindowMask | LeaveWindowMask | FocusChangeMask | StructureNotifyMask;
    XWindow xw = XCreateWindow(display,RootWindow(display,screen),0,0,width,height,0,visual->depth,InputOutput,
                               visual->visual,CWColormap | CWBorderPixel | CWBackPixmap | CWEventMask,&wa);
    XFree(visual);
    if (!xw){
        debug->Err("Could not create requested window\n");
        XCloseDisplay(display);
        return NULL;
    }

    Window* wnd = new Window();
    wnd->x11 = new WindowX11();
    wnd->x11->display = display;
    wnd->x11->window = xw;
    wnd->x11->config = config;
    main_display = display;

    //The close button. Without it a window manager closes the CONNECTION rather than asking.
    wnd->x11->wm_delete = XInternAtom(display,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(display,xw,&wnd->x11->wm_delete,1);
    wnd->x11->net_wm_state = XInternAtom(display,"_NET_WM_STATE",False);
    wnd->x11->net_wm_state_fullscreen = XInternAtom(display,"_NET_WM_STATE_FULLSCREEN",False);
    wnd->x11->net_wm_state_above = XInternAtom(display,"_NET_WM_STATE_ABOVE",False);

    //The name a taskbar groups windows by, and what a .desktop file's StartupWMClass would match.
    XClassHint* class_hint = XAllocClassHint();
    class_hint->res_name = (char*)(title ? title : "engine");
    class_hint->res_class = class_hint->res_name;
    XSetClassHint(display,xw,class_hint);
    XFree(class_hint);

    //The arrow, set rather than inherited - Window.cpp's class cursor has the long story of NOT doing so.
    XDefineCursor(display,xw,XCreateFontCursor(display,XC_left_ptr));

    //Autorepeat as repeated presses with no release between them, which is what Win32's raw input
    //gives. Left alone X sends a release and a press per repeat, and every held key would flicker.
    XkbSetDetectableAutoRepeat(display,True,NULL);

    wnd->width = width;
    wnd->height = height;
    wnd->width_windowed = width;
    wnd->height_windowed = height;
    wnd->SetTitle(title ? title : "Normal Window");
    wnd->inputcontroller = new InputController();
    debug->Ok("New X11 window 0x%lx on display %s\n",(unsigned long)xw,DisplayString(display));
    return wnd;
}

bool Window::Init(){
    if (!InitOpenGL()){
        return false;
    }
    RegisterDropFiles();
    return true;
}

bool Window::InitOpenGL(){
    Display* display = x11->display;
    PFNGLXCREATECONTEXTATTRIBSARBPROC create =
        (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    if (!create){
        debug->Err("GLX_ARB_create_context is not available\n");
        return false;
    }
    //The request Window.cpp makes of WGL - 4.5 core, with the debug bit - without its dummy-context
    //dance: GLX takes these on the first and only context.
    static const int context_attributes[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB,  4,
        GLX_CONTEXT_MINOR_VERSION_ARB,  5,
        GLX_CONTEXT_FLAGS_ARB,          GLX_CONTEXT_DEBUG_BIT_ARB,
        GLX_CONTEXT_PROFILE_MASK_ARB,   GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
    x11->context = create(display,x11->config,NULL,True,context_attributes);
    XSync(display,False);
    if (!x11->context){
        debug->Err("The GL driver offers no OpenGL 4.5 core context. A virtual machine's GL often stops below 4.5;"
                   " LIBGL_ALWAYS_SOFTWARE=1 gives Mesa's software renderer, which has it.\n");
        return false;
    }
    if (!glXMakeCurrent(display,x11->window,x11->context)){
        debug->Err("glXMakeCurrent failed\n");
        return false;
    }

    //Whichever swap-control extension the driver has. glXGetProcAddressARB answers for ANY name, so it
    //is the extension string that says whether a pointer means anything.
    const char* extensions = glXQueryExtensionsString(display,DefaultScreen(display));
    if (extensions && strstr(extensions,"GLX_EXT_swap_control")){
        swap_interval_ext = (PFNGLXSWAPINTERVALEXTPROC)LoadGLXProc("glXSwapIntervalEXT");
    }else if (extensions && strstr(extensions,"GLX_MESA_swap_control")){
        swap_interval_mesa = (SwapIntervalMESAFn)LoadGLXProc("glXSwapIntervalMESA");
    }
    gl_display = display;
    gl_drawable = x11->window;
    GLSetPlatform(LoadGLXProc,SetGLXSwapInterval);
    if (!InitGLExtensions()){
        return false;
    }

    const char* gl_version = (const char*)glGetString(GL_VERSION);
    const char* gl_vendor = (const char*)glGetString(GL_VENDOR);
    const char* gl_renderer = (const char*)glGetString(GL_RENDERER);
    debug->Info("OpenGL Version: %s\n",gl_version ? gl_version : "(null)");
    debug->Info("OpenGL Vendor: %s\n",gl_vendor ? gl_vendor : "(null)");
    debug->Info("OpenGL Renderer: %s\n",gl_renderer ? gl_renderer : "(null)");
    debug->Info("Swap control: %s\n",swap_interval_ext ? "GLX_EXT_swap_control" : (swap_interval_mesa ? "GLX_MESA_swap_control" : "none"));
    return true;
}

bool Window::MakeContextCurrent(){
    return glXMakeCurrent(x11->display,x11->window,x11->context) == True;
}

void Window::ReleaseContext(){
    glXMakeCurrent(x11->display,None,NULL);
}

void Window::SwapWindowBuffers(){
    CopyBufferToBackBuffer();
    glXSwapBuffers(x11->display,x11->window);
}

void Window::PumpUntilQuit(){
    Display* display = x11->display;
    int fd = ConnectionNumber(display);
    while (f_should_quit == false){
        while (!f_should_quit && XPending(display)){
            XEvent e;
            XNextEvent(display,&e);
            HandleEvent(this,e);
        }
        if (f_should_quit){
            break;
        }
        /*
            Nothing queued: wait on the connection, as Window.cpp waits on its message queue. 5 ms, not
            Window.cpp's 50, because this wait can miss an event: the render thread's own GLX calls
            read from the same connection and may queue one after XPending above has looked. 5 ms is
            the most that can cost, a quarter of a tick, for 200 empty wakeups a second.
        */
        struct pollfd p = {fd,POLLIN,0};
        poll(&p,1,5);
    }
}

int2 Window::GetDisplaySize(){
    if (!main_display){
        return int2(0,0);
    }
    //The whole X screen, which spans every monitor - XRandR would tell them apart and their rates.
    int w = DisplayWidth(main_display,DefaultScreen(main_display));
    int h = DisplayHeight(main_display,DefaultScreen(main_display));
    debug->Info("Current Display Settings: %d x %d\n",w,h);
    return int2(w,h);
}

void Window::Show(bool f_minimized){
    Display* display = x11->display;
    if (f_minimized){
        /*
            --minimized's two halves, as X has them: start ICONIC, and do not take focus - a user time
            of 0 tells an EWMH window manager that this window was not asked for by the person at the
            desk. Both are requests: a window manager may ignore either, and under XWayland the
            Wayland compositor decides.
        */
        XWMHints* hints = XAllocWMHints();
        hints->flags = StateHint | InputHint;
        hints->initial_state = IconicState;
        hints->input = True;
        XSetWMHints(display,x11->window,hints);
        XFree(hints);
        Atom user_time = XInternAtom(display,"_NET_WM_USER_TIME",False);
        long zero = 0;
        XChangeProperty(display,x11->window,user_time,XA_CARDINAL,32,PropModeReplace,(unsigned char*)&zero,1);
        //Unfocused until told otherwise: a window that never had focus may never be sent a FocusOut.
        if (inputcontroller){
            inputcontroller->SetFocused(false);
        }
    }
    x11->f_shown = true;
    XMapWindow(display,x11->window);
    XFlush(display);
}

void Window::Resize(int _width, int _height){
    /*
        The SIZE only. Where the window goes is the window manager's to decide on X, and most centre a
        new one themselves, which is what Window.cpp does by hand on Win32.

        An unmapped window - a --minimized one - may be told nothing about this until it is shown, so
        the renderer is told directly, as Window.cpp does for an iconic window.
    */
    if (!x11->f_mapped){
        width = _width;
        height = _height;
        if (!(f_istogglingfullscreen || f_fullscreen)){
            width_windowed = width;
            height_windowed = height;
        }
        f_resized = true;
    }
    XResizeWindow(x11->display,x11->window,(unsigned)_width,(unsigned)_height);
    XFlush(x11->display);
}

//From any thread, as on Win32: Xlib takes its own lock (XInitThreads), and the window manager does
//the rest, sending the new size back as a ConfigureNotify.
void Window::SetFullscreen(bool f_on){
    if (!x11 || f_on == f_fullscreen){
        return;
    }
    f_istogglingfullscreen = true;
    SetNetWMState(x11,f_on,x11->net_wm_state_fullscreen);
    f_fullscreen = f_on;
    f_istogglingfullscreen = false;
}

void Window::SetAlwaysOnTop(bool f_on){
    SetNetWMState(x11,f_on,x11->net_wm_state_above);
}

bool Window::IsMinimized(){
    return x11 && !x11->f_mapped;
}

//The old WM_NAME for window managers that read nothing else, and _NET_WM_NAME in UTF-8 for those that do.
void Window::SetTitle(std::string title){
    XStoreName(x11->display,x11->window,title.c_str());
    Atom net_wm_name = XInternAtom(x11->display,"_NET_WM_NAME",False);
    Atom utf8 = XInternAtom(x11->display,"UTF8_STRING",False);
    XChangeProperty(x11->display,x11->window,net_wm_name,utf8,8,PropModeReplace,
                    (const unsigned char*)title.c_str(),(int)title.size());
    XFlush(x11->display);
}

void Window::Close(){
    f_should_quit = true;
}

//Not yet: dropping files on an X window is the XDND protocol, a conversation of five message kinds
//with the source. SetOnFileDropped's callback is kept and simply never called here.
void Window::RegisterDropFiles(){
}

#endif //__linux__ && !__ANDROID__

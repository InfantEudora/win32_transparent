#include "RawInput.h"
#include "Debug.h"

static Debugger* debug = new Debugger("RawInput",DEBUG_INFO);

static const char* RAWINPUT_WNDCLASS = "win32_transparent_rawinput";

RawInputSource::~RawInputSource(){
    Stop();
}

LRESULT CALLBACK RawInputSource::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    if (msg == WM_NCCREATE){
        //Stash the instance so WM_INPUT can find it again. Done on NCCREATE because that is the
        //first message a window gets, before any WM_INPUT could arrive.
        CREATESTRUCTA* cs = (CREATESTRUCTA*)lp;
        SetWindowLongPtrA(hwnd,GWLP_USERDATA,(LONG_PTR)cs->lpCreateParams);
    }else if (msg == WM_INPUT){
        RawInputSource* self = (RawInputSource*)GetWindowLongPtrA(hwnd,GWLP_USERDATA);
        if (self){
            self->HandleRawInput((HRAWINPUT)lp);
        }
        //WM_INPUT must still reach DefWindowProc, which releases the buffer the system allocated
        //for this event. Skipping it leaks one per input event.
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

bool RawInputSource::CreateMessageWindow(){
    HINSTANCE instance = GetModuleHandleA(NULL);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = RawInputSource::WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = RAWINPUT_WNDCLASS;
    //A class already registered by a previous Start() is fine; anything else is fatal.
    if (!RegisterClassA(&wc)){
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS){
            debug->Err("RegisterClassA for the raw input window failed, error %lu\n",err);
            return false;
        }
    }

    //HWND_MESSAGE makes this a message-only window: never visible, never in the z-order, never
    //dragged or resized, and owned by THIS thread's message queue.
    hwnd = CreateWindowExA(0,RAWINPUT_WNDCLASS,"rawinput",0,0,0,0,0,HWND_MESSAGE,NULL,instance,this);
    if (!hwnd){
        debug->Err("CreateWindowExA for the raw input window failed, error %lu\n",GetLastError());
        return false;
    }

    //Usage page 1 ("generic desktop"), usage 2 = mouse, usage 6 = keyboard.
    RAWINPUTDEVICE devices[2] = {0};
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage     = 0x02;  //mouse
    devices[0].dwFlags     = RIDEV_INPUTSINK;
    devices[0].hwndTarget  = hwnd;
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage     = 0x06;  //keyboard
    devices[1].dwFlags     = RIDEV_INPUTSINK;
    devices[1].hwndTarget  = hwnd;

    if (!RegisterRawInputDevices(devices,2,sizeof(RAWINPUTDEVICE))){
        debug->Err("RegisterRawInputDevices failed, error %lu\n",GetLastError());
        DestroyWindow(hwnd);
        hwnd = NULL;
        return false;
    }

    debug->Ok("Raw input registered for mouse+keyboard on a message-only window\n");
    return true;
}

DWORD WINAPI RawInputSource::ThreadFunction(LPVOID param){
    RawInputSource* self = (RawInputSource*)param;
    if (!self){
        return 0;
    }

    //The window MUST be created on this thread: a window belongs to the message queue of the
    //thread that created it, and that ownership is the whole point of this class.
    self->f_setup_ok = self->CreateMessageWindow();
    self->f_setup_done = true;
    if (!self->f_setup_ok){
        return 0;
    }

    debug->Info("Raw input thread id %lu running\n",GetCurrentThreadId());

    //GetMessage blocks, so this thread costs nothing while no input is happening. It is woken by
    //WM_INPUT, and by the WM_QUIT that Stop() posts.
    MSG msg = {0};
    while (self->f_running){
        BOOL res = GetMessageA(&msg,NULL,0,0);
        if (res == 0 || res == -1){ //WM_QUIT, or an error
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (self->hwnd){
        DestroyWindow(self->hwnd);
        self->hwnd = NULL;
    }
    debug->Info("Raw input thread terminated\n");
    return 1;
}

bool RawInputSource::Start(InputController* target){
    if (!target){
        debug->Err("RawInputSource::Start called without an InputController\n");
        return false;
    }
    if (f_running){
        return f_setup_ok;
    }
    input = target;
    f_running = true;
    f_setup_done = false;
    f_setup_ok = false;

    thread_handle = CreateThread(NULL,0,RawInputSource::ThreadFunction,this,0,&thread_id);
    if (!thread_handle){
        debug->Err("Unable to create the raw input thread\n");
        f_running = false;
        return false;
    }

    //Wait for setup so the caller learns whether raw input is live before deciding to switch
    //InputController off its polling path.
    while (!f_setup_done){
        Sleep(1);
    }
    if (!f_setup_ok){
        f_running = false;
        return false;
    }
    return true;
}

void RawInputSource::Stop(){
    if (!f_running){
        return;
    }
    f_running = false;
    if (thread_id){
        PostThreadMessageA(thread_id,WM_QUIT,0,0);
    }
    if (thread_handle){
        WaitForSingleObject(thread_handle,1000);
        CloseHandle(thread_handle);
        thread_handle = NULL;
    }
    thread_id = 0;
}

void RawInputSource::HandleRawInput(HRAWINPUT raw_handle){
    if (!input){
        return;
    }

    //RAWINPUT for a mouse or keyboard is small and fixed-size, so a stack buffer is enough; only
    //HID device reports are variable-length, and those aren't registered here.
    BYTE buffer[sizeof(RAWINPUT)];
    UINT size = sizeof(buffer);
    UINT written = GetRawInputData(raw_handle,RID_INPUT,buffer,&size,sizeof(RAWINPUTHEADER));
    if (written == (UINT)-1 || written < sizeof(RAWINPUTHEADER)){
        return;
    }

    RAWINPUT* raw = (RAWINPUT*)buffer;

    if (raw->header.dwType == RIM_TYPEKEYBOARD){
        const RAWKEYBOARD& kb = raw->data.keyboard;
        //0xFF is a filler event some keyboards emit as part of an escaped sequence; it is not a key.
        if (kb.VKey == 0xFF){
            return;
        }
        USHORT vk = kb.VKey;
        bool e0 = (kb.Flags & RI_KEY_E0) != 0;
        //Raw input reports the SIDELESS modifier VKs, but the engine's keymap is built from the
        //sided ones (VK_LSHIFT and VK_RSHIFT are mapped separately, both onto INPUT_SHIFT), so
        //resolve them here or neither would ever match a mapping.
        if (vk == VK_SHIFT){
            vk = (USHORT)MapVirtualKeyA(kb.MakeCode,MAPVK_VSC_TO_VK_EX);
        }else if (vk == VK_CONTROL){
            vk = e0 ? VK_RCONTROL : VK_LCONTROL;
        }else if (vk == VK_MENU){
            vk = e0 ? VK_RMENU : VK_LMENU;
        }
        input->SubmitSystemKey(vk,(kb.Flags & RI_KEY_BREAK) == 0);
        return;
    }

    if (raw->header.dwType == RIM_TYPEMOUSE){
        const RAWMOUSE& m = raw->data.mouse;

        if (m.usFlags & MOUSE_MOVE_ABSOLUTE){
            //Tablets, RDP sessions and some VMs report a normalised absolute position rather than
            //a delta. Turning those into deltas needs its own state and screen-rect handling, and
            //the absolute cursor position is polled separately anyway (INPUT_MOUSE_X/Y via
            //GetCursorPos), so they are ignored here rather than mishandled.
        }else if (m.lLastX || m.lLastY){
            //The real prize: unaccelerated, unclipped, device-resolution movement. Unlike a
            //cursor-position delta this keeps working at the edge of the screen.
            debug->Info("Raw mouse delta %d,%d\n",m.lLastX,m.lLastY);
            input->SubmitAxisDelta(INPUT_MOUSE_DELTA_X,m.lLastX);
            input->SubmitAxisDelta(INPUT_MOUSE_DELTA_Y,m.lLastY);
        }

        //Raw input reports PHYSICAL buttons, so it ignores the system's left/right swap setting
        //(GetSystemMetrics(SM_SWAPBUTTON)) that the legacy WM_*BUTTON messages honour. Nothing
        //here reads that setting yet; if it ever matters, swap at this point.
        USHORT bf = m.usButtonFlags;
        if (bf & RI_MOUSE_LEFT_BUTTON_DOWN){   input->SubmitSystemKey(VK_LBUTTON,true);  }
        if (bf & RI_MOUSE_LEFT_BUTTON_UP){     input->SubmitSystemKey(VK_LBUTTON,false); }
        if (bf & RI_MOUSE_RIGHT_BUTTON_DOWN){  input->SubmitSystemKey(VK_RBUTTON,true);  }
        if (bf & RI_MOUSE_RIGHT_BUTTON_UP){    input->SubmitSystemKey(VK_RBUTTON,false); }
        if (bf & RI_MOUSE_MIDDLE_BUTTON_DOWN){ input->SubmitSystemKey(VK_MBUTTON,true);  }
        if (bf & RI_MOUSE_MIDDLE_BUTTON_UP){   input->SubmitSystemKey(VK_MBUTTON,false); }
        if (bf & RI_MOUSE_BUTTON_4_DOWN){      input->SubmitSystemKey(VK_XBUTTON1,true); }
        if (bf & RI_MOUSE_BUTTON_4_UP){        input->SubmitSystemKey(VK_XBUTTON1,false);}
        if (bf & RI_MOUSE_BUTTON_5_DOWN){      input->SubmitSystemKey(VK_XBUTTON2,true); }
        if (bf & RI_MOUSE_BUTTON_5_UP){        input->SubmitSystemKey(VK_XBUTTON2,false);}

        if (bf & RI_MOUSE_WHEEL){
            //usButtonData is a signed wheel delta in WHEEL_DELTA units, same as WM_MOUSEWHEEL.
            input->SubmitAxisDelta(INPUT_MOUSE_WHEEL,(SHORT)m.usButtonData / WHEEL_DELTA);
        }
    }
}

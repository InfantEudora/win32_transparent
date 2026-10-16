/*
    InputController's acquisition half on Linux, for the X11 window backend (core/Window_x11.cpp) -
    what HandleMessage and PollDevices are on Win32 and InputController_android.cpp is on Android.
    Everything it reads becomes the same SubmitSystemKey/SubmitAxisDelta/SubmitPointer calls the
    other platforms make, with keys as the VK codes of core/KeyCodes.h, so a binding or a recording
    means the same key on every desktop.
*/
#include "InputController.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include "Debug.h"

//Xlib's window type is called Window, which is the engine's class; it is XWindow for the length of
//its headers. See the same block in Window_x11.cpp.
#define Window XWindow
#include <X11/Xlib.h>
#include <X11/keysym.h>
#undef Window

static Debugger* debug = new Debugger("InputController", DEBUG_INFO);

/*
    An X keysym as the VK code Win32 would report for the same key, 0 for one with none.

    Looked up at index 0, the key's unshifted symbol (see HandleXEvent), so Shift+1 is still the
    '1' key, as a VK is - and letters follow the keyboard LAYOUT, as VKs do: the key marked Q on an
    AZERTY board is 'A' on both. The punctuation (OEM) codes are the US layout's, which is what
    Win32's are for a US board.
*/
static uint32_t KeysymToVK(KeySym s){
    if (s >= XK_a && s <= XK_z){
        return 'A' + (uint32_t)(s - XK_a);
    }
    if (s >= XK_A && s <= XK_Z){
        return 'A' + (uint32_t)(s - XK_A);
    }
    if (s >= XK_0 && s <= XK_9){
        return '0' + (uint32_t)(s - XK_0);
    }
    if (s >= XK_F1 && s <= XK_F24){
        return VK_F1 + (uint32_t)(s - XK_F1);
    }
    if (s >= XK_KP_0 && s <= XK_KP_9){
        return VK_NUMPAD0 + (uint32_t)(s - XK_KP_0);
    }
    switch (s){
        case XK_BackSpace:      return VK_BACK;
        case XK_Tab:
        case XK_ISO_Left_Tab:   return VK_TAB;
        case XK_Return:
        case XK_KP_Enter:       return VK_RETURN;
        case XK_Pause:          return VK_PAUSE;
        case XK_Caps_Lock:      return VK_CAPITAL;
        case XK_Escape:         return VK_ESCAPE;
        case XK_space:          return VK_SPACE;
        case XK_Prior:
        case XK_KP_Prior:       return VK_PRIOR;
        case XK_Next:
        case XK_KP_Next:        return VK_NEXT;
        case XK_End:
        case XK_KP_End:         return VK_END;
        case XK_Home:
        case XK_KP_Home:        return VK_HOME;
        case XK_Left:
        case XK_KP_Left:        return VK_LEFT;
        case XK_Up:
        case XK_KP_Up:          return VK_UP;
        case XK_Right:
        case XK_KP_Right:       return VK_RIGHT;
        case XK_Down:
        case XK_KP_Down:        return VK_DOWN;
        case XK_Print:          return VK_SNAPSHOT;
        case XK_Insert:
        case XK_KP_Insert:      return VK_INSERT;
        case XK_Delete:
        case XK_KP_Delete:      return VK_DELETE;
        case XK_Super_L:        return VK_LWIN;
        case XK_Super_R:        return VK_RWIN;
        case XK_Menu:           return VK_APPS;
        case XK_KP_Multiply:    return VK_MULTIPLY;
        case XK_KP_Add:         return VK_ADD;
        case XK_KP_Separator:   return VK_SEPARATOR;
        case XK_KP_Subtract:    return VK_SUBTRACT;
        case XK_KP_Decimal:     return VK_DECIMAL;
        case XK_KP_Divide:      return VK_DIVIDE;
        case XK_Num_Lock:       return VK_NUMLOCK;
        case XK_Scroll_Lock:    return VK_SCROLL;
        //The SIDED modifiers, as RawInput.cpp resolves them - the keymap is built from these.
        case XK_Shift_L:        return VK_LSHIFT;
        case XK_Shift_R:        return VK_RSHIFT;
        case XK_Control_L:      return VK_LCONTROL;
        case XK_Control_R:      return VK_RCONTROL;
        case XK_Alt_L:
        case XK_Meta_L:         return VK_LMENU;
        case XK_Alt_R:
        case XK_Meta_R:
        case XK_ISO_Level3_Shift: return VK_RMENU;   //AltGr, which Win32 reports as right Alt
        case XK_semicolon:      return VK_OEM_1;
        case XK_equal:          return VK_OEM_PLUS;
        case XK_comma:          return VK_OEM_COMMA;
        case XK_minus:          return VK_OEM_MINUS;
        case XK_period:         return VK_OEM_PERIOD;
        case XK_slash:          return VK_OEM_2;
        case XK_grave:          return VK_OEM_3;
        case XK_bracketleft:    return VK_OEM_4;
        case XK_backslash:      return VK_OEM_5;
        case XK_bracketright:   return VK_OEM_6;
        case XK_apostrophe:     return VK_OEM_7;
        case XK_less:           return VK_OEM_102;    //the extra key beside left Shift on ISO boards
        default:                return 0;
    }
}

void InputController::HandleXEvent(const void* xevent){
    const XEvent& e = *(const XEvent*)xevent;
    switch (e.type){
        case KeyPress:
        case KeyRelease:{
            //Autorepeat arrives as repeated presses with no release between them - Window_x11.cpp
            //asks for that with XkbSetDetectableAutoRepeat - which is what Win32's raw input does too.
            KeySym sym = XLookupKeysym((XKeyEvent*)&e.xkey,0);
            uint32_t vk = KeysymToVK(sym);
            if (vk){
                SubmitSystemKey(vk,e.type == KeyPress);
            }
            break;
        }
        case ButtonPress:
        case ButtonRelease:{
            bool down = (e.type == ButtonPress);
            const XButtonEvent& b = e.xbutton;
            ReportPointerPosition(int2(b.x_root,b.y_root));
            ReportWindowPosition(int2(b.x_root - b.x,b.y_root - b.y));
            SetMouseOverWindow(true);
            switch (b.button){
                case Button1:
                    SubmitSystemKey(VK_LBUTTON,down);
                    //Pointer 0 for the on-screen buttons, as Win32's WM_LBUTTON* drive it.
                    SubmitPointer(0,(float)b.x,(float)b.y,down);
                    break;
                case Button2:   SubmitSystemKey(VK_MBUTTON,down);   break;
                case Button3:   SubmitSystemKey(VK_RBUTTON,down);   break;
                case 8:         SubmitSystemKey(VK_XBUTTON1,down);  break;
                case 9:         SubmitSystemKey(VK_XBUTTON2,down);  break;
                //X reports a wheel notch as a press and release of buttons 4 (up) and 5 (down): one
                //notch each, counted on the press, in the same sign as Win32's WHEEL_DELTA.
                case Button4:   if (down){ SubmitAxisDelta(INPUT_MOUSE_WHEEL,1); }  break;
                case Button5:   if (down){ SubmitAxisDelta(INPUT_MOUSE_WHEEL,-1); } break;
                default:        break;
            }
            break;
        }
        case MotionNotify:{
            const XMotionEvent& m = e.xmotion;
            int2 at(m.x_root,m.y_root);
            /*
                Deltas from the cursor's own movement, where Win32 has raw device counts. These are
                accelerated by the desktop's pointer settings and stop at the screen's edge, so a
                camera drag may feel different here; X's raw motion (XInput2) is the step up if it
                does, at the price of linking libXi.
            */
            if (f_last_pointer_valid){
                if (at.x != last_pointer.x){
                    SubmitAxisDelta(INPUT_MOUSE_DELTA_X,at.x - last_pointer.x);
                }
                if (at.y != last_pointer.y){
                    SubmitAxisDelta(INPUT_MOUSE_DELTA_Y,at.y - last_pointer.y);
                }
            }
            last_pointer = at;
            f_last_pointer_valid = true;
            ReportPointerPosition(at);
            ReportWindowPosition(int2(m.x_root - m.x,m.y_root - m.y));
            SetMouseOverWindow(true);
            //The button state on every move, for the same reason HandleMessage gives it: a press that
            //ends outside the window still lets go of the on-screen button on the next move.
            SubmitPointer(0,(float)m.x,(float)m.y,(m.state & Button1Mask) != 0);
            break;
        }
        case EnterNotify:{
            const XCrossingEvent& c = e.xcrossing;
            last_pointer = int2(c.x_root,c.y_root);
            f_last_pointer_valid = true;
            ReportPointerPosition(last_pointer);
            ReportWindowPosition(int2(c.x_root - c.x,c.y_root - c.y));
            SetMouseOverWindow(true);
            break;
        }
        case LeaveNotify:
            SetMouseOverWindow(false);
            break;
        /*
            Focus, as WM_SETFOCUS and WM_KILLFOCUS. A GRAB is not a change of focus: a window
            manager grabs the keyboard for its own Alt+Tab, and the focus it hands back afterwards
            may well be ours again.
        */
        case FocusIn:
            if (e.xfocus.mode != NotifyGrab && e.xfocus.mode != NotifyUngrab){
                SetFocused(true);
            }
            break;
        case FocusOut:
            if (e.xfocus.mode != NotifyGrab && e.xfocus.mode != NotifyUngrab){
                SetFocused(false);
                //Outside SetFocused for the reason HandleMessage gives at WM_KILLFOCUS.
                ReleaseAllTouchPointers();
            }
            break;
        default:
            break;
    }
}

/*
    What still has to be POLLED: the cursor, as on Win32 - though here it is the last position the
    window's events reported rather than a fresh GetCursorPos, so it does not follow the cursor
    while it is outside the window and no button is held. Keys need no polling at all; they only
    ever arrive as events.
*/
void InputController::PollDevices(){
    std::vector<InputEvent> events;
    bool f_pointer = f_pointer_reported;
    int64_t packed = reported_pointer;
    int2 p((int)(packed >> 32),(int)(int32_t)(uint32_t)packed);
    if (f_pointer){
        for (KeyMap& map: keymap){
            if (map.mapped_keycode != INPUT_MOUSE_X && map.mapped_keycode != INPUT_MOUSE_Y){
                continue;
            }
            InputEvent e;
            e.type = INPUT_EVENT_AXIS_ABSOLUTE;
            e.mapped_keycode = (uint16_t)map.mapped_keycode;
            e.value = (map.mapped_keycode == INPUT_MOUSE_X) ? p.x : p.y;
            events.push_back(e);
        }
    }

    PollGamepad();

    if (events.empty() && !f_pointer){
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex);
    if (f_pointer){
        mouse_position = p;
    }
    pending_events.insert(pending_events.end(),events.begin(),events.end());
}

/*
    NO PADS YET. Neither X11 nor Wayland does gamepads: they are read from /dev/input/event* through
    the kernel's evdev interface, and the Xbox, PlayStation and Switch Pro drivers all report the
    kernel's one standard layout - so this becomes "translate to XInput's bits and axes, then
    ApplyGamepadButtons and SubmitAnalogAxes", as PollGamepad does on Win32. docs/linux_port_plan.md.
*/
void InputController::PollGamepad(){
}

void InputController::ListDevices(){
    debug->Info("ListDevices: gamepads are not read on Linux yet\n");
}

void InputController::SendMotorData(int l, int r){
    (void)l;
    (void)r;
}

#endif //__linux__ && !__ANDROID__

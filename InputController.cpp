#include "InputController.h"
#include "Debug.h"
static Debugger* debug = new Debugger("Input",DEBUG_INFO);

void KeyState::Up(){
    if (f_isdown){
        f_was_released = true;
    }
    f_isdown = false;
}

void KeyState::Down(){
    f_isdown = true;
}

InputController::InputController(){
    //Let's always map the mouse.
    AddKeyMap(0,MOUSE_X);
    AddKeyMap(0,MOUSE_Y);

    AddKeyMap(VK_UP,INPUT_MOVE_UP);
    AddKeyMap(VK_DOWN,INPUT_MOVE_DOWN);
    AddKeyMap(VK_LEFT,INPUT_MOVE_LEFT);
    AddKeyMap(VK_RIGHT,INPUT_MOVE_RIGHT);
}

KeyMap* InputController::AddKeyMap(uint32_t syskey, uint32_t mapped){
    KeyMap m = {
        .system_keycode = syskey,
        .mapped_keycode = mapped,
        .state = new KeyState()
    };
    keymap.push_back(m);
    return &keymap.back();
}

void InputController::UpdateKeyState(){
    SHORT s = GetAsyncKeyState(VK_UP);
    //debug->Info("State: %i\n",s);
    bool mousepoint_valid = false;
    POINT p;
    if (mousepoint_valid = GetCursorPos(&p)){
        //debug->Info("Mouse: %li x %li\n",p.x,p.y);
    }

    for (KeyMap& map: keymap){
        if (mousepoint_valid && (map.mapped_keycode == MOUSE_X)){
            map.state->value = p.x;
            continue;
        }
        if (mousepoint_valid && (map.mapped_keycode == MOUSE_Y)){
            map.state->value = p.y;
            continue;
        }
        SHORT s = GetAsyncKeyState(map.system_keycode);
        if (s){
            map.state->Down();
        }else{
            map.state->Up();
        }
    }
}

//Return the first keystate that has the keycode specified
KeyMap* InputController::GetBySystemKey(uint32_t sys_keycode){
    for (KeyMap& km:keymap){
        if(km.system_keycode == sys_keycode){
            return &km;
        }
    }
    return NULL;
}

//Return all keystates that have the mapped code specified
KeyMap* InputController::GetByMappedKey(uint32_t mapped_code){
    for (KeyMap& km:keymap){
        if(km.mapped_keycode == mapped_code){
            return &km;
        }
    }
    return NULL;
}

bool InputController::IsKeyDown(uint32_t mapped){
    KeyMap* m = GetByMappedKey(mapped);
    if (!m){
        return false;
    }
    return m->state->f_isdown;
}

bool InputController::WasKeyReleased(uint32_t mapped){
    KeyMap* m = GetByMappedKey(mapped);
    if (!m){
        return false;
    }
    return m->state->f_was_released;
}

//Clears the button and input transition flags
void InputController::Tick(){
    for (KeyMap& km:keymap){
        km.state->f_was_released = false;
    }
}

//Handles message from a WIN32 message handler.
void InputController::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam){
    if (msg == WM_MOUSELEAVE){
        //debug->Warn("Mouse has left the building.\n");
    }else if (msg == WM_NCMOUSEMOVE){
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        debug->Trace("WM_NCMOUSEMOVE x,y = %li,%li\n",x,y);
    }else if (msg == WM_MOUSEMOVE){
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        debug->Trace("WM_MOUSEMOVE x,y = %li,%li\n",x,y);
    }else if (msg == WM_MOUSEWHEEL){
        int wheel = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        debug->Trace("WM_MOUSEWHEEL x,y = %li,%li\n",x,y);
    }else if (msg == WM_NCMOUSELEAVE){
    }else if (msg == WM_SETCURSOR){
    }else if (msg == WM_CHAR){

    }else if (msg == WM_KEYDOWN){
        debug->Trace("WM_KEYDOWN wParam= %li\n",wParam);
    }else if (msg == WM_KEYUP){
        debug->Trace("WM_KEYUP wParam= %li\n",wParam);
    }else{
        debug->Trace("msg = %li (0x%04X)\n",msg,msg);
    }
}
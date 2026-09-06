#include "InputController.h"
#include "Debug.h"
static Debugger* debug = new Debugger("Input",DEBUG_INFO);

void KeyState::Up(){
    if (f_isdown == 1){
        f_was_released = true;
    }
    if (f_isdown > 0)
        f_isdown--;
}

void KeyState::Down(){
    f_isdown = num_mappings;
}

InputController::InputController(){
    //Mouse mapping is handled different from keys, but are read in the same way once mapped.
    AddKeyMap(0,INPUT_MOUSE_X);
    AddKeyMap(0,INPUT_MOUSE_Y);
    AddKeyMap(0,INPUT_MOUSE_WHEEL);

    AddKeyMap(VK_UP,INPUT_MOVE_UP);
    AddKeyMap(VK_DOWN,INPUT_MOVE_DOWN);
    AddKeyMap(VK_LEFT,INPUT_MOVE_LEFT);
    AddKeyMap(VK_RIGHT,INPUT_MOVE_RIGHT);

    AddKeyMap('W',INPUT_TURN_UP);
    AddKeyMap('A',INPUT_TURN_LEFT);
    AddKeyMap('S',INPUT_TURN_DOWN);
    AddKeyMap('D',INPUT_TURN_RIGHT);

    AddKeyMap(VK_PAUSE,INPUT_PAUSE);

    AddKeyMap(VK_LBUTTON,INPUT_CLICK_LEFT);
    AddKeyMap(VK_MBUTTON,INPUT_CLICK_MIDDLE);
    AddKeyMap(VK_RBUTTON,INPUT_CLICK_RIGHT);

    //TODO: Make multiple mappings work by somehow orring the up/down together.
    AddKeyMap(VK_LSHIFT,INPUT_SHIFT);
    AddKeyMap(VK_RSHIFT,INPUT_SHIFT);
}

KeyMap* InputController::AddKeyMap(uint32_t syskey, uint32_t mapped){
    bool new_mapping = true;
    KeyMap* same_map = NULL;
    for (KeyMap& map: keymap){
        if (map.mapped_keycode == mapped){
            //Alread have a mapping for it.
            new_mapping = false;
            same_map = &map;
            break;
        }
    }
    KeyMap m;
    m.system_keycode = syskey;
    m.mapped_keycode = mapped;
    if (new_mapping){
        m.state = new KeyState();
    }else{
        m.state = same_map->state;
        same_map->state->num_mappings++;
    }
    keymap.push_back(m);
    return &keymap.back();
}

void InputController::UpdateKeyState(){
    PollDevices();
    ApplyPendingEvents();
}

void InputController::SubmitEvent(const InputEvent& event){
    std::lock_guard<std::mutex> lock(state_mutex);
    pending_events.push_back(event);
}

//Samples the OS devices into events.
//
//This reports each key's LEVEL every tick rather than its edges, which is not what the event
//stream ultimately wants but is exactly what keeps this step behaviour-identical to the polling
//loop it replaces - quirks included. Down() is idempotent while Up() only decrements, so a
//keycode with two mappings (LSHIFT and RSHIFT both map to INPUT_SHIFT) still reads as down for
//one extra tick after release, and the wheel's dummy mapping still gets a pointless
//GetAsyncKeyState(0) that resolves to "up". Raw Input is where levels become real edges, and
//where that multi-mapping counting bug gets fixed on purpose instead of by accident.
void InputController::PollDevices(){
    //Batched into a local first, so the whole poll costs one lock acquisition rather than one per
    //key.
    std::vector<InputEvent> events;
    events.reserve(keymap.size() + 1);

    POINT p;
    bool mousepoint_valid = GetCursorPos(&p);

    //Whether to sample the simulation's keys at all. The cursor position below is view data
    //(picking, the debug UI) rather than simulation input, so it keeps tracking either way.
    bool sample_keys = f_has_focus;

    for (KeyMap& map: keymap){
        InputEvent e;
        if (map.mapped_keycode == INPUT_MOUSE_X || map.mapped_keycode == INPUT_MOUSE_Y){
            if (!mousepoint_valid){
                continue;
            }
            e.type = INPUT_EVENT_AXIS_ABSOLUTE;
            e.mapped_keycode = (uint16_t)map.mapped_keycode;
            e.value = (map.mapped_keycode == INPUT_MOUSE_X) ? p.x : p.y;
            events.push_back(e);
            continue;
        }
        //Unfocused keys are reported as UP rather than simply not sampled: a key held down while
        //the user alt-tabs away must release, not stay latched at its last polled level.
        e.type = (sample_keys && GetAsyncKeyState(map.system_keycode)) ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP;
        e.mapped_keycode = (uint16_t)map.mapped_keycode;
        events.push_back(e);
    }

    std::lock_guard<std::mutex> lock(state_mutex);
    if (mousepoint_valid){
        mouse_position = {p.x,p.y};
    }
    pending_events.insert(pending_events.end(),events.begin(),events.end());
}

//Physics thread, top of the tick. Everything submitted since the last call becomes this tick's
//input - including whatever the window message thread pushed in from its own thread, which is the
//handoff that used to be an unsynchronised write straight into KeyState.
void InputController::ApplyPendingEvents(){
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        tick_events.swap(pending_events);
        pending_events.clear();
    }

    for (const InputEvent& e: tick_events){
        //Multiple mappings of one keycode share a single KeyState, so resolving by mapped code is
        //correct here: Down()/Up() act on that shared state exactly as the old loop did.
        KeyMap* m = GetByMappedKey(e.mapped_keycode);
        if (!m || !m->state){
            continue;
        }
        switch (e.type){
            case INPUT_EVENT_KEY_DOWN:
                m->state->Down();
            break;
            case INPUT_EVENT_KEY_UP:
                m->state->Up();
            break;
            case INPUT_EVENT_AXIS_ABSOLUTE:
                m->state->delta = e.value - m->state->value;
                m->state->value = e.value;
                m->state->f_processed = false;
            break;
            case INPUT_EVENT_AXIS_RELATIVE:
                m->state->value += e.value;
                m->state->delta += e.value;
                m->state->f_processed = false;
            break;
            default:
            break;
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
    return !!m->state->f_isdown;
}

bool InputController::WasKeyReleased(uint32_t mapped){
    KeyMap* m = GetByMappedKey(mapped);
    if (!m){
        return false;
    }
    return m->state->f_was_released;
}

int32_t InputController::GetDelta(uint32_t mapped, KeyMap** map_out){
    KeyMap* m = GetByMappedKey(mapped);
    if (!m){
        return 0;
    }
    if (map_out){
        *map_out = m;
    }
    m->state->f_processed = true;
    return m->state->delta;
}

//The remaining accessors below are all genuinely cross-thread - the render thread reads the mouse
//position for picking and writes the hovered normal/position after its buffer readback, while the
//physics thread polls and reads. They take the lock rather than relying on a scattering of
//atomics and a hopeful comment.
int2 InputController::GetAbsoluteMousePosition(){
    std::lock_guard<std::mutex> lock(state_mutex);
    return mouse_position;
}

int2 InputController::GetRelativeMousePosition(){
    std::lock_guard<std::mutex> lock(state_mutex);
    return (mouse_position - window_state.window_position);
}

bool InputController::IsMouseOverWindow(){
    std::lock_guard<std::mutex> lock(state_mutex);
    return window_state.f_mouse_over_window;
}

void InputController::SetMouseOverWindow(bool over){
    std::lock_guard<std::mutex> lock(state_mutex);
    window_state.f_mouse_over_window = over;
}

void InputController::SetHoveredObjectID(objectid_t id){
    hovered_object = id;
}

objectid_t InputController::GetHoveredObjectID(){
    return hovered_object;
}

void InputController::SetHoveredNormal(vec3 normal){
    std::lock_guard<std::mutex> lock(state_mutex);
    window_state.hovered_normal = normal;
}

vec3 InputController::GetHoveredNormal(){
    std::lock_guard<std::mutex> lock(state_mutex);
    return window_state.hovered_normal;
}

void InputController::SetHoveredPosition(vec3 pos){
    std::lock_guard<std::mutex> lock(state_mutex);
    window_state.hovered_position = pos;
}

vec3 InputController::GetHoveredPosition(){
    std::lock_guard<std::mutex> lock(state_mutex);
    return window_state.hovered_position;
}

//Clears the button and input transition flags
void InputController::Tick(){
    //debug->Info("Input Controller Tick\n");
    for (KeyMap& km:keymap){
        km.state->f_was_released = false;

        if (km.state->f_processed){
            km.state->delta = 0;
            km.state->f_processed = false;
        }
    }
}

//Handles message from a WIN32 message handler, which are from a different thread.
void InputController::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam){

    if (msg == WM_NCMOUSEMOVE){
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        SetMouseOverWindow(true);
        //debug->Trace("WM_NCMOUSEMOVE x,y = %li,%li\n",x,y);
    }else if (msg == WM_MOUSEMOVE){
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        SetMouseOverWindow(true);
        //debug->Trace("WM_MOUSEMOVE x,y = %li,%li\n",x,y);
    }else if (msg == WM_MOUSEWHEEL){
        int d = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        //debug->Trace("WM_MOUSEWHEEL x,y = %li,%li delta=%i\n",x,y,d);
        //The wheel is the one input that has always been genuinely event-driven, and it used to be
        //written straight into KeyState from this thread while the physics thread was reading and
        //clearing it. Now it goes through the queue like everything else.
        InputEvent e;
        e.type = INPUT_EVENT_AXIS_RELATIVE;
        e.mapped_keycode = INPUT_MOUSE_WHEEL;
        e.value = d;
        SubmitEvent(e);
        SetMouseOverWindow(true);
    }else if (msg == WM_MOUSELEAVE){
        debug->Trace("WM_MOUSELEAVE wParam= %li\n",wParam);
        SetMouseOverWindow(false);
    }else if (msg == WM_ACTIVATE){
        //Losing the foreground stops the simulation's input being sampled at all - the next
        //PollDevices reports every key as up, so nothing stays latched. ImGui is unaffected; it
        //has its own Win32 handler.
        f_has_focus = (LOWORD(wParam) != WA_INACTIVE);
        debug->Trace("WM_ACTIVATE focus=%i\n",(int)f_has_focus);
    }else if (msg == WM_SETFOCUS){
        f_has_focus = true;
    }else if (msg == WM_KILLFOCUS){
        f_has_focus = false;
    }else if (msg == WM_SETCURSOR){
    }else if (msg == WM_CHAR){
    }else if (msg == WM_CAPTURECHANGED){
    }else if (msg == WM_KEYDOWN){
        debug->Trace("WM_KEYDOWN wParam= %li\n",wParam);
    }else if (msg == WM_KEYUP){
        debug->Trace("WM_KEYUP wParam= %li\n",wParam);
    }else if (msg == WM_MOVE){
        int x = (int)(short) LOWORD(lParam);
        int y = (int)(short) HIWORD(lParam);
        std::lock_guard<std::mutex> lock(state_mutex);
        window_state.window_position = {x,y};
    }else{
        debug->Trace("msg = %li (0x%04X)\n",msg,msg);
    }
}
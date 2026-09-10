#include "InputController.h"
#include "Debug.h"
static Debugger* debug = new Debugger("Input",DEBUG_INFO);

InputController::InputController(){
    //Mouse mapping is handled different from keys, but are read in the same way once mapped.
    AddKeyMap(0,INPUT_MOUSE_X);
    AddKeyMap(0,INPUT_MOUSE_Y);
    AddKeyMap(0,INPUT_MOUSE_WHEEL);
    AddKeyMap(0,INPUT_MOUSE_DELTA_X);
    AddKeyMap(0,INPUT_MOUSE_DELTA_Y);

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

void InputController::UpdateKeyState(uint64_t sim_tick){
    PollDevices();
    ApplyPendingEvents(sim_tick);
}

void InputController::SubmitEvent(const InputEvent& event){
    std::lock_guard<std::mutex> lock(state_mutex);
    pending_events.push_back(event);
}

void InputController::SetRawInputActive(bool active){
    f_raw_input_active = active;
}

//keymap is only ever built during setup (AddKeyMap) and read afterwards, so walking it from the
//raw input thread is safe. f_held is touched only by the physics thread in ApplyPendingEvents.
void InputController::SubmitSystemKey(uint32_t system_keycode, bool down){
    //Unfocused, a key going down is somebody typing in another application. A key coming UP is
    //always honoured, so anything already held can still release.
    if (down && !f_has_focus){
        return;
    }

    std::vector<InputEvent> events;
    for (KeyMap& map: keymap){
        if (map.system_keycode != system_keycode){
            continue;
        }
        InputEvent e;
        e.type = down ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP;
        e.mapped_keycode = (uint16_t)map.mapped_keycode;
        //Which physical key this came from. Lets ApplyPendingEvents count each mapping of a
        //multiply-mapped action separately instead of guessing.
        e.value = (int32_t)system_keycode;
        events.push_back(e);
    }
    if (events.empty()){
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex);
    pending_events.insert(pending_events.end(),events.begin(),events.end());
}

//Unfocused, this is the mouse being used in another application. Raw Input is registered
//RIDEV_INPUTSINK and so keeps delivering regardless of which window is in front - it has to, a
//message-only window can never BE the foreground window - which means the focus filtering the OS
//used to do for us (WM_MOUSEWHEEL only ever reaches the focused window) is now ours. Unlike a key
//there is no "always honour the up edge" concern: a relative axis latches nothing, so dropping it
//outright cannot leave anything stuck. Scripted input is unaffected - AdvanceSyntheticHolds writes
//into pending_events directly and never comes through here.
void InputController::SubmitAxisDelta(uint32_t mapped_keycode, int32_t delta){
    if (delta == 0 || !f_has_focus){
        return;
    }
    InputEvent e;
    e.type = INPUT_EVENT_AXIS_RELATIVE;
    e.mapped_keycode = (uint16_t)mapped_keycode;
    e.value = delta;
    SubmitEvent(e);
}

//Samples whatever still has to be polled.
//
//The absolute cursor position always: Raw Input reports MOVEMENT, not a cursor, and the cursor is
//what picking and the debug UI need. Keys only when raw input isn't running - if RawInputSource
//failed to start we fall back to GetAsyncKeyState, still emitting edges (by diffing against each
//mapping's f_held) so the rest of the pipeline behaves identically either way.
void InputController::PollDevices(){
    std::vector<InputEvent> events;

    POINT p;
    bool mousepoint_valid = GetCursorPos(&p);
    if (mousepoint_valid){
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

    if (!f_raw_input_active){
        bool sample_keys = f_has_focus;
        for (KeyMap& map: keymap){
            if (map.system_keycode == 0){
                continue;   //the mouse axes and wheel have no system key to poll
            }
            bool down = sample_keys && (GetAsyncKeyState(map.system_keycode) & 0x8000) != 0;
            if (down == map.f_held){
                continue;   //no edge, nothing to report
            }
            InputEvent e;
            e.type = down ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP;
            e.mapped_keycode = (uint16_t)map.mapped_keycode;
            e.value = (int32_t)map.system_keycode;
            events.push_back(e);
        }
    }

    //Sampled here so the gamepad is read once per tick along with everything else, rather than by
    //each app calling its own controller from its own update function.
    PollGamepad();

    if (events.empty() && !mousepoint_valid){
        return;
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
void InputController::ApplyPendingEvents(uint64_t sim_tick){
    //Scripted holds go first, and emit into the same queue as everything else, so a recording of
    //this tick cannot tell a scripted press from a real one. Only ever advanced once per SIMULATED
    //tick - see the declaration for why counting calls instead would be a wall-clock bug.
    if (!f_hold_tick_valid || sim_tick != last_hold_tick){
        f_hold_tick_valid = true;
        last_hold_tick = sim_tick;
        AdvanceSyntheticHolds();
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        tick_events.swap(pending_events);
        pending_events.clear();
    }

    //Focus was lost since the last tick: drop everything that was held. Done before the events so
    //a key-up that did arrive is simply a no-op afterwards.
    if (f_release_all_keys.exchange(false)){
        for (KeyMap& km: keymap){
            if (!km.f_held){
                continue;
            }
            km.f_held = false;
            if (km.state && km.state->f_isdown > 0){
                km.state->f_isdown--;
                if (km.state->f_isdown == 0){
                    km.state->f_was_released = true;
                }
            }
        }
    }

    for (const InputEvent& e: tick_events){
        KeyMap* m = NULL;
        if ((e.type == INPUT_EVENT_KEY_DOWN || e.type == INPUT_EVENT_KEY_UP) && e.value != 0){
            //A hardware key: resolve the exact mapping it came from, so that a keycode driven by
            //several physical keys (VK_LSHIFT and VK_RSHIFT both -> INPUT_SHIFT) counts each of
            //them once and only reads as released when the last one comes up.
            for (KeyMap& km: keymap){
                if (km.mapped_keycode == e.mapped_keycode && km.system_keycode == (uint32_t)e.value){
                    m = &km;
                    break;
                }
            }
        }
        if (!m){
            //Axis events, and synthetic key events with no system key behind them (what the UI and
            //the MCP server will submit once they become "players").
            m = GetByMappedKey(e.mapped_keycode);
        }
        if (!m || !m->state){
            continue;
        }
        switch (e.type){
            case INPUT_EVENT_KEY_DOWN:
                //Edge, not level: a repeat while already held is ignored rather than re-counted.
                if (!m->f_held){
                    m->f_held = true;
                    m->state->f_isdown++;
                }
            break;
            case INPUT_EVENT_KEY_UP:
                if (m->f_held){
                    m->f_held = false;
                    if (m->state->f_isdown > 0){
                        m->state->f_isdown--;
                    }
                    if (m->state->f_isdown == 0){
                        m->state->f_was_released = true;
                    }
                }
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
            case INPUT_EVENT_AXIS_SCALAR:
                m->state->fvalue = e.fvalue;
                m->state->f_processed = false;
            break;
            default:
            break;
        }
    }
}

//--- Scripted input -----------------------------------------------------------------------------

void InputController::HoldKey(uint32_t mapped_keycode, uint32_t duration_ticks){
    AddSyntheticHold(mapped_keycode,false,0.0f,duration_ticks);
}

void InputController::HoldAxis(uint32_t mapped_keycode, float value, uint32_t duration_ticks){
    AddSyntheticHold(mapped_keycode,true,value,duration_ticks);
}

void InputController::AddSyntheticHold(uint32_t mapped_keycode, bool axis, float value, uint32_t duration_ticks){
    std::lock_guard<std::mutex> lock(state_mutex);
    for (size_t i=0;i<synthetic_holds.size();i++){
        if (synthetic_holds[i].mapped_keycode != mapped_keycode){
            continue;
        }
        if (duration_ticks == 0){
            //Releasing: leave the hold in place with no time left, so the next tick emits its
            //key-up / zero properly rather than dropping it and stranding the control held.
            synthetic_holds[i].ticks_remaining = 0;
            return;
        }
        //Re-asserting the same control refreshes it. f_started is deliberately left alone: an
        //ongoing press must not produce a second key-down.
        synthetic_holds[i].f_axis = axis;
        synthetic_holds[i].value = value;
        synthetic_holds[i].ticks_remaining = duration_ticks;
        return;
    }
    if (duration_ticks == 0){
        return; //nothing was held, nothing to release
    }
    SyntheticHold h;
    h.mapped_keycode = mapped_keycode;
    h.f_axis = axis;
    h.value = value;
    h.ticks_remaining = duration_ticks;
    synthetic_holds.push_back(h);
}

void InputController::ReleaseSynthetic(){
    std::lock_guard<std::mutex> lock(state_mutex);
    for (SyntheticHold& h: synthetic_holds){
        h.ticks_remaining = 0;  //next tick emits the release
    }
}

bool InputController::HasSyntheticHolds(){
    std::lock_guard<std::mutex> lock(state_mutex);
    return !synthetic_holds.empty();
}

void InputController::AdvanceSyntheticHolds(){
    std::vector<InputEvent> events;
    std::lock_guard<std::mutex> lock(state_mutex);

    for (size_t i=0;i<synthetic_holds.size();){
        SyntheticHold& h = synthetic_holds[i];

        if (!h.f_started){
            h.f_started = true;
            InputEvent e;
            e.mapped_keycode = (uint16_t)h.mapped_keycode;
            if (h.f_axis){
                e.type = INPUT_EVENT_AXIS_SCALAR;
                e.fvalue = h.value;
            }else{
                e.type = INPUT_EVENT_KEY_DOWN;
            }
            events.push_back(e);
        }

        //ticks_remaining counts ticks still to be held INCLUDING this one, so a hold of 1 is down
        //for exactly one tick and releases on the next.
        if (h.ticks_remaining > 0){
            h.ticks_remaining--;
            i++;
            continue;
        }

        InputEvent e;
        e.mapped_keycode = (uint16_t)h.mapped_keycode;
        if (h.f_axis){
            e.type = INPUT_EVENT_AXIS_SCALAR;
            e.fvalue = 0.0f;
        }else{
            e.type = INPUT_EVENT_KEY_UP;
        }
        events.push_back(e);
        synthetic_holds.erase(synthetic_holds.begin() + i);
    }

    pending_events.insert(pending_events.end(),events.begin(),events.end());
}

float InputController::GetAxis(uint32_t mapped_keycode){
    KeyMap* m = GetByMappedKey(mapped_keycode);
    if (!m || !m->state){
        return 0.0f;
    }
    return m->state->fvalue;
}

//--- Gamepad (XInput) ---------------------------------------------------------------------------

void InputController::ListDevices(){
    debug->Info("X-Input: Checking for contollers using X-Input\n");
    XINPUT_STATE state;
    ZeroMemory(&state,sizeof(XINPUT_STATE));

    //All four slots now, not just slot 0.
    for (DWORD i=0;i<XUSER_MAX_COUNT;i++){
        if (XInputGetState(i,&state) != ERROR_SUCCESS){
            continue;
        }
        debug->Info("X-Input: Controller at %lu\n",i);
        if (dev_index < 0){
            dev_index = (int)i;
        }
        XINPUT_CAPABILITIES cap;
        if (XInputGetCapabilities(i,0,&cap) == ERROR_SUCCESS){
            debug->Info("X-Input: Got Capabilities\n");
            debug->Info("X-Input:  - Flags %lu\n",cap.Flags);
        }
    }
    if (dev_index < 0){
        debug->Warn("No Game Controller was found using XInput\n");
    }
}

GamePadMap* InputController::AddGamePadMap(int analog_index, uint32_t mapped){
    GamePadMap m;
    m.analog_index = analog_index;
    m.mapped_keycode = mapped;
    gamepad_map.push_back(m);
    return &gamepad_map.back();
}

void InputController::PollGamepad(){
    XINPUT_STATE state;
    ZeroMemory(&state,sizeof(XINPUT_STATE));

    if (dev_index < 0){
        //Nothing connected: look again, but only every GAMEPAD_RESCAN_TICKS - see the comment on
        //that define for why probing empty slots every tick is not free.
        if (gamepad_rescan_countdown > 0){
            gamepad_rescan_countdown--;
            return;
        }
        gamepad_rescan_countdown = GAMEPAD_RESCAN_TICKS;
        for (DWORD i=0;i<XUSER_MAX_COUNT;i++){
            if (XInputGetState(i,&state) == ERROR_SUCCESS){
                dev_index = (int)i;
                debug->Ok("X-Input: controller connected at index %lu\n",i);
                break;
            }
        }
        if (dev_index < 0){
            return;
        }
    }else if (XInputGetState((DWORD)dev_index,&state) != ERROR_SUCCESS){
        debug->Warn("X-Input: controller at index %i disconnected\n",dev_index);
        dev_index = -1;
        //Zeroed, so a yanked cable can't leave a stick stuck at its last deflection. The old code
        //left the last values in place and never looked for the pad again.
        for (int i=0;i<GAMEPAD_MAX_ANALOG_VALUES;i++){
            analog_values[i] = 0;
        }
        gamepad_rescan_countdown = GAMEPAD_RESCAN_TICKS;
        return;
    }

    if (!f_has_focus){
        //Same rule as the keyboard: another application is in front, so this is not our input.
        for (int i=0;i<GAMEPAD_MAX_ANALOG_VALUES;i++){
            analog_values[i] = 0;
        }
        return;
    }

    //Indices and scaling exactly as the old GamePadController had them, so every existing
    //AddGamePadMap(index,...) in the apps keeps meaning the same thing.
    analog_values[0] = state.Gamepad.sThumbLX;
    analog_values[1] = state.Gamepad.sThumbLY;
    analog_values[2] = state.Gamepad.sThumbRX;
    analog_values[3] = state.Gamepad.sThumbRY;
    analog_values[4] = (SHORT)state.Gamepad.bLeftTrigger * 128;
    analog_values[5] = (SHORT)state.Gamepad.bRightTrigger * 128;

    //Rumble decay, unchanged.
    bool send_disable = false;
    if (lmotor > 0){
        lmotor = clamp(lmotor - 1000,0,65000);
        if (lmotor == 0){
            send_disable = true;
        }
    }
    if (rmotor > 0){
        rmotor = clamp(rmotor - 1000,0,65000);
        if (rmotor == 0){
            send_disable = true;
        }
    }
    if ((rmotor > 0) || (lmotor > 0) || send_disable){
        SendMotorData(lmotor,rmotor);
    }
}

float InputController::GetNormalizedAnalogValue(uint32_t mapped_key){
    if (dev_index == -1){
        return 0.0f;
    }
    for (GamePadMap& map: gamepad_map){
        if (map.mapped_keycode != mapped_key){
            continue;
        }
        if (map.analog_index >= 0 && map.analog_index < GAMEPAD_MAX_ANALOG_VALUES){
            int raw_value = analog_values[map.analog_index];
            int32_t zero_offset = map.zero_offset;
            int32_t dead_zone = map.dead_zone;
            float normalized = 0.0f;
            raw_value = raw_value - zero_offset;
            if (raw_value > dead_zone){
                normalized = (float)(raw_value - dead_zone) / (32767.0f - dead_zone);
            }else if (raw_value < -dead_zone){
                normalized = (float)(raw_value + dead_zone) / (32768.0f - dead_zone);
            }
            return normalized;
        }
    }
    return 0.0f;
}

//Conclusion: Sending things with the HID interface does not work.
void InputController::SendMotorData(int l, int r){
    if (dev_index < 0){
        return;
    }
    XINPUT_VIBRATION v;
    v.wLeftMotorSpeed = l;
    v.wRightMotorSpeed = r;
    XInputSetState((DWORD)dev_index,&v);
}

//--- Lookups ------------------------------------------------------------------------------------

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

int32_t InputController::GetValue(uint32_t mapped, KeyMap** map_out){
    KeyMap* m = GetByMappedKey(mapped);
    if (!m){
        return 0;
    }
    if (map_out){
        *map_out = m;
    }
    m->state->f_processed = true;
    return m->state->value;
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

void InputController::SetFocused(bool focused){
    bool was = f_has_focus.exchange(focused);
    if (was && !focused){
        //Ask the next tick to release everything held. The key-up for a key still down when the
        //user alt-tabs does normally arrive (raw input is registered RIDEV_INPUTSINK, so it keeps
        //being delivered), but relying on that alone would leave the key stuck if it didn't.
        f_release_all_keys = true;
    }
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
        //clearing it. Now it goes through the queue like everything else - unless raw input is
        //live, in which case RI_MOUSE_WHEEL already reported this same scroll and counting it here
        //too would double every notch.
        if (!f_raw_input_active){
            SubmitAxisDelta(INPUT_MOUSE_WHEEL,d);
        }
        SetMouseOverWindow(true);
    }else if (msg == WM_MOUSELEAVE){
        debug->Trace("WM_MOUSELEAVE wParam= %li\n",wParam);
        SetMouseOverWindow(false);
    }else if (msg == WM_ACTIVATE){
        //Losing the foreground stops the simulation seeing key presses at all, and releases
        //anything still held. ImGui is unaffected; it has its own Win32 handler.
        SetFocused(LOWORD(wParam) != WA_INACTIVE);
        debug->Trace("WM_ACTIVATE focus=%i\n",(int)f_has_focus);
    }else if (msg == WM_SETFOCUS){
        SetFocused(true);
    }else if (msg == WM_KILLFOCUS){
        SetFocused(false);
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
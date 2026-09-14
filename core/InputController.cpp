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

//One mapping, whatever kind of hardware drives it. A second mapping for an action it already
//knows shares that action's KeyState, which is what lets several keys (or a key and a stick) hold
//one action between them.
//
//NOTE the returned pointer is into `keymap`, so it is invalidated by the next mapping added. It is
//safe to use immediately and not worth keeping - which is why AddGamePadMap takes its dead zone as
//an argument rather than expecting the caller to write it through this pointer afterwards.
KeyMap* InputController::AddMapping(uint32_t syskey, uint32_t mapped, int analog_index){
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
    m.analog_index = analog_index;
    if (new_mapping){
        m.state = new KeyState();
    }else{
        m.state = same_map->state;
        same_map->state->num_mappings++;
    }
    keymap.push_back(m);
    return &keymap.back();
}

KeyMap* InputController::AddKeyMap(uint32_t syskey, uint32_t mapped){
    return AddMapping(syskey,mapped,-1);
}

//--- On-screen buttons ---------------------------------------------------------------------------
//See the block comment in the header for why these live in here and why they are edge-driven.

int InputController::AddTouchButton(const TouchRect& rect, uint32_t mapped, const char* label){
    TouchButton button;
    button.rect = rect;
    button.mapped_keycode = mapped;
    if (label){
        snprintf(button.label,sizeof(button.label),"%s",label);
    }
    //Its own keycode, never the app's, never 0. See TOUCH_SYSKEY_BASE for what sharing one costs.
    button.system_keycode = next_touch_syskey++;
    touch_buttons.push_back(button);

    //The ordinary mapping, so everything downstream - edge detection, the multiply-mapped count,
    //the focus gate, HoldKey, recording - treats this exactly like a key.
    AddKeyMap(button.system_keycode,mapped);

    debug->Info("Touch button %u -> action %u (index %d)\n",
                button.system_keycode,mapped,(int)touch_buttons.size() - 1);
    return (int)touch_buttons.size() - 1;
}

void InputController::SetTouchButtonRect(int index, const TouchRect& rect){
    if ((index < 0) || (index >= (int)touch_buttons.size())){
        return;
    }
    touch_buttons[index].rect = rect;
}

void InputController::SubmitPointer(int32_t pointer_id, float x, float y, bool down){
    //Find the slot this pointer already owns, if any.
    int slot = -1;
    for (int i = 0; i < INPUT_CONTROLLER_MAX_TOUCHES; i++){
        if (touch_pointers[i].id == pointer_id){
            slot = i;
            break;
        }
    }

    if (!down){
        if (slot < 0){
            //Never saw it go down - pressed before the layout existed, or beyond the pointer
            //limit. Nothing to release.
            return;
        }
        int button_index = touch_pointers[slot].button_index;
        touch_pointers[slot].id = -1;
        touch_pointers[slot].button_index = -1;
        if ((button_index >= 0) && (button_index < (int)touch_buttons.size())){
            touch_buttons[button_index].f_down = false;
            SubmitSystemKey(touch_buttons[button_index].system_keycode,false);
        }
        return;
    }

    if (slot >= 0){
        //A move of a pointer already down. Deliberately nothing: the button was CAPTURED on press
        //and is held until this pointer lifts, so a drifting thumb cannot drop a held direction
        //and an edge cannot chatter at a rect boundary. See the header on why sliding between
        //buttons is not a feature yet.
        return;
    }

    //A new pointer. Take a free slot; if there is none, ignore it rather than evicting somebody
    //else's finger - the platform layer already clamps to this same limit.
    for (int i = 0; i < INPUT_CONTROLLER_MAX_TOUCHES; i++){
        if (touch_pointers[i].id == -1){
            slot = i;
            break;
        }
    }
    if (slot < 0){
        return;
    }

    //First hit wins, so overlapping rects resolve by declaration order rather than by accident.
    int hit = -1;
    for (int i = 0; i < (int)touch_buttons.size(); i++){
        if (touch_buttons[i].rect.Contains(x,y)){
            hit = i;
            break;
        }
    }

    //Tracked even when it hit nothing (button_index -1), so its release is recognised as this
    //pointer's rather than searched for among the buttons.
    touch_pointers[slot].id = pointer_id;
    touch_pointers[slot].button_index = hit;

    if (hit >= 0){
        touch_buttons[hit].f_down = true;
        SubmitSystemKey(touch_buttons[hit].system_keycode,true);
    }
}

void InputController::ReleaseAllTouchPointers(){
    for (int i = 0; i < INPUT_CONTROLLER_MAX_TOUCHES; i++){
        int button_index = touch_pointers[i].button_index;
        touch_pointers[i].id = -1;
        touch_pointers[i].button_index = -1;
        if ((button_index >= 0) && (button_index < (int)touch_buttons.size())){
            touch_buttons[button_index].f_down = false;
            //Honoured even unfocused: SubmitSystemKey only gates key-DOWNS, so anything held can
            //always release.
            SubmitSystemKey(touch_buttons[button_index].system_keycode,false);
        }
    }
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
            if (map.system_keycode >= GAMEPAD_SYSKEY_BASE){
                //A SYNTHETIC keycode, not a key: a gamepad button (PollGamepad owns those) or an
                //on-screen button (SubmitPointer owns those). GetAsyncKeyState knows nothing about
                //either and would return 0, which this loop would read as "released" and act on -
                //cancelling every press the instant the source that owns it reported one.
                //
                //One threshold covers both because TOUCH_SYSKEY_BASE sits above the gamepad range,
                //so this kept working for touch buttons without being changed. Worth knowing
                //rather than relying on: a fourth source numbered BELOW this would be polled to
                //death here with nothing to indicate why.
                continue;
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
    //sim_tick is unused here now that scripted holds advance from ApplyTickInput instead. Kept in
    //the signature because this is the call every pass makes and the tick it belongs to is worth
    //having at hand - a recorder stamping hardware input wants it, and item 7 will.
    (void)sim_tick;
    DrainAndApplyEvents(false);
}

void InputController::ApplyTickInput(uint64_t sim_tick){
    //Scripted holds emit into the same queue as everything else, so a recording of this tick
    //cannot tell a scripted press from a real one.
    //
    //The guard is a backstop, not the mechanism: the caller runs this exactly once per ticking
    //pass, which is once per tick. It matters because Scene::UpdatePhysics is public and an app
    //may drive the simulation itself - see the note on Scene::BeginPass - and a hold must not
    //count down twice for one tick if it does.
    if (!f_hold_tick_valid || sim_tick != last_hold_tick){
        f_hold_tick_valid = true;
        last_hold_tick = sim_tick;
        AdvanceSyntheticHolds();
    }
    //Appended: this pass already applied whatever hardware input it sampled, and that is part of
    //this tick's input too.
    DrainAndApplyEvents(true);
}

void InputController::DrainAndApplyEvents(bool f_append){
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (f_append){
            tick_events.insert(tick_events.end(),pending_events.begin(),pending_events.end());
        }else{
            tick_events.swap(pending_events);
        }
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
                    //Only the FIRST mapping to go down raises the edge, exactly as only the last
                    //one to come up raises f_was_released. A second key on the same action while
                    //the first is still held is not a new press of that action.
                    if (m->state->f_isdown == 1){
                        m->state->f_was_pressed = true;
                    }
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
        /*
            Re-asserting the same control refreshes it.

            f_started is deliberately left alone for a BUTTON: an ongoing press must not produce a
            second key-down. An AXIS is not the same case, and treating it as one was a bug. An
            axis event's VALUE is its entire content, so a re-assert that changes the value has to
            emit again - otherwise h.value is updated, nothing is sent, and KeyState::fvalue keeps
            the old value for as long as the hold keeps being refreshed.

            That is exactly what a scripted player does when it changes direction: hold steer at
            +1, then at -1 before the first hold has expired. The second command was accepted and
            silently did nothing, which reads as a control that has stuck - and it defeats any bot
            that steers by re-issuing a hold, which is how both of this repo's bots steer.
        */
        if (axis && (synthetic_holds[i].value != value)){
            synthetic_holds[i].f_started = false;
        }
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
    //...or the tick a hold released on, which is the tick its edge is readable. See the header.
    return !synthetic_holds.empty() || f_synthetic_release_tick;
}

void InputController::AdvanceSyntheticHolds(){
    std::vector<InputEvent> events;
    std::lock_guard<std::mutex> lock(state_mutex);

    //Cleared here rather than where it is read, because this runs exactly once per simulated tick
    //and a reader may ask any number of times within one.
    f_synthetic_release_tick = false;

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
        f_synthetic_release_tick = true;
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

//See the header. System keycode 0 because no KEY drives this mapping - the analog index is what
//names its hardware.
KeyMap* InputController::AddGamePadMap(int analog_index, uint32_t mapped, int32_t dead_zone, int32_t zero_offset){
    if ((analog_index < 0) || (analog_index >= GAMEPAD_MAX_ANALOG_VALUES)){
        debug->Err("AddGamePadMap: analog index %i is outside 0..%i, mapping ignored\n",
                   analog_index,GAMEPAD_MAX_ANALOG_VALUES - 1);
        return NULL;
    }
    KeyMap* m = AddMapping(0,mapped,analog_index);
    if (m){
        m->dead_zone = dead_zone;
        m->zero_offset = zero_offset;
    }
    return m;
}

//The raw XInput short, through the mapping's zero offset and dead zone, to -1..1. One copy of the
//arithmetic; it used to live inside GetNormalizedAnalogValue, which was the only reader.
static float NormalizeAnalog(int raw_value, int32_t zero_offset, int32_t dead_zone){
    raw_value = raw_value - zero_offset;
    if (raw_value > dead_zone){
        return (float)(raw_value - dead_zone) / (32767.0f - dead_zone);
    }
    if (raw_value < -dead_zone){
        return (float)(raw_value + dead_zone) / (32768.0f - dead_zone);
    }
    return 0.0f;
}

/*
    Turns the sticks into ordinary axis events.

    ONLY ON A CHANGE, which is the whole reason this is not a plain per-tick write. A gamepad is
    polled, so it has a value every tick whether or not anyone touched it; an event stream is not,
    and a centred stick submitting 0.0 every tick would overwrite whatever else was driving that
    action - which in practice means every scripted HoldAxis stops working the moment a controller
    is plugged in. Reporting only transitions makes the two sources coexist: last writer wins, and
    a stick nobody is touching is not a writer.

    It also keeps a recording honest and small: what gets written down is the thumb actually
    moving, not fifty identical samples a second of it resting.
*/
void InputController::SubmitAnalogAxes(){
    for (KeyMap& map: keymap){
        if (!map.IsAnalog()){
            continue;
        }
        float value = NormalizeAnalog(analog_values[map.analog_index],map.zero_offset,map.dead_zone);
        if (map.f_analog_sent && (value == map.last_analog_value)){
            continue;
        }
        map.last_analog_value = value;
        map.f_analog_sent = true;

        InputEvent e;
        e.type = INPUT_EVENT_AXIS_SCALAR;
        e.mapped_keycode = (uint16_t)map.mapped_keycode;
        e.fvalue = value;
        SubmitEvent(e);
    }
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
        //...and report the way back to centre, or an axis stays at its last deflection in every
        //KeyState that was reading it.
        SubmitAnalogAxes();
        //Same for the buttons: a pad unplugged mid-press must not leave the action latched down.
        ApplyGamepadButtons(0);
        gamepad_rescan_countdown = GAMEPAD_RESCAN_TICKS;
        return;
    }

    if (!f_has_focus){
        //Same rule as the keyboard: another application is in front, so this is not our input.
        for (int i=0;i<GAMEPAD_MAX_ANALOG_VALUES;i++){
            analog_values[i] = 0;
        }
        SubmitAnalogAxes();
        ApplyGamepadButtons(0);
        return;
    }

    //Buttons before the sticks, so a press and a stick deflection sampled in the same call reach
    //the queue in a fixed order rather than depending on where this sits in the function.
    ApplyGamepadButtons(state.Gamepad.wButtons);

    //Indices and scaling exactly as the old GamePadController had them, so every existing
    //AddGamePadMap(index,...) in the apps keeps meaning the same thing.
    analog_values[0] = state.Gamepad.sThumbLX;
    analog_values[1] = state.Gamepad.sThumbLY;
    analog_values[2] = state.Gamepad.sThumbRX;
    analog_values[3] = state.Gamepad.sThumbRY;
    analog_values[4] = (SHORT)state.Gamepad.bLeftTrigger * 128;
    analog_values[5] = (SHORT)state.Gamepad.bRightTrigger * 128;

    //And into the event stream, so a stick reaches gameplay by the same route a key does.
    SubmitAnalogAxes();

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

//See the declaration. Deliberately routes through SubmitSystemKey rather than writing KeyState
//directly: that is what gives a pad button the same edges, the same multi-mapping counting and the
//same place in the recorded event stream as a key on the keyboard.
void InputController::ApplyGamepadButtons(uint16_t buttons){
    if (buttons == gamepad_buttons){
        return;
    }
    //Every XInput button bit. 0x0400/0x0800 are unassigned by XInput and deliberately absent.
    static const uint16_t button_bits[] = {
        XINPUT_GAMEPAD_DPAD_UP,        XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,      XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START,          XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_LEFT_THUMB,     XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_LEFT_SHOULDER,  XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_A,              XINPUT_GAMEPAD_B,
        XINPUT_GAMEPAD_X,              XINPUT_GAMEPAD_Y
    };
    uint16_t changed = buttons ^ gamepad_buttons;
    //Updated BEFORE the submits, so this is consistent even if one of them is dropped by the
    //focus gate inside SubmitSystemKey - the next poll still diffs against what the pad reported.
    gamepad_buttons = buttons;
    for (size_t i = 0;i < sizeof(button_bits)/sizeof(button_bits[0]);i++){
        uint16_t bit = button_bits[i];
        if (!(changed & bit)){
            continue;
        }
        SubmitSystemKey(GAMEPAD_SYSKEY_BASE + bit,(buttons & bit) != 0);
    }
}

//An alias for GetAxis, kept because four apps call it by this name.
//
//It used to walk `gamepad_map` and normalise the raw value on the spot, which meant a gamepad
//axis could ONLY be read through this function - a scripted HoldAxis on the same action was
//invisible to it, and GetAxis was in turn blind to the stick. PollGamepad now submits the stick
//as an ordinary axis event, so both land in one KeyState and there is a single place to read an
//axis from, whoever is driving it.
float InputController::GetNormalizedAnalogValue(uint32_t mapped_key){
    return GetAxis(mapped_key);
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

/*
    Reading an edge CONSUMES it, and that is what makes item 88's rule work rather than merely
    delay the problem - see Tick(). The flag is set on the way out whether or not the edge was
    up: "this action was looked at on this pass" is the useful fact, and a consumer that polls an
    action every pass should not have an unrelated older edge kept alive for it.

    Marked even when the lookup says the action is not down, for the same reason.
*/
bool InputController::WasKeyReleased(uint32_t mapped){
    KeyMap* m = GetByMappedKey(mapped);
    if (!m){
        return false;
    }
    m->state->f_released_read = true;
    return m->state->f_was_released;
}

bool InputController::WasKeyPressed(uint32_t mapped){
    KeyMap* m = GetByMappedKey(mapped);
    if (!m){
        return false;
    }
    m->state->f_pressed_read = true;
    return m->state->f_was_pressed;
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

/*
    Clears the button and input transition flags at the end of a physics pass.

    `f_ticked` says whether that pass actually ran a tick, and it is what fixes backlog item 88.
    THE RULE: an edge is cleared once it has been READ, or once a TICKING pass has been and gone.
    An edge nobody has looked at yet survives a non-ticking pass.

    Why that is the right rule rather than "never clear until a tick": input is drained on every
    pass, ticking or not, because UpdateView and the pause key need it while the simulation is
    stopped. Those consumers read their actions EVERY pass, so their edges are consumed on the
    pass they appear and behave exactly as before - a mute toggle fires once, not once per pass
    for as long as the game is paused, which is what a blanket "keep it until a tick" would do.

    Gameplay actions are read only from the ticking branch, so nothing consumes them while the
    simulation is stepped; their edges now wait, and the next tick sees them. That is the whole
    bug: a rotate pressed while paused was raised on a spinning pass and cleared at the end of it.

    The `f_ticked` clear is the backstop that keeps this bounded, and it is deliberately not a
    grace COUNT like the axis path below: the consumer being waited for is the tick itself, so
    "until the next tick" is the exact lifetime rather than an approximation of it. An action no
    code reads at all is cleared by the next tick and cannot accumulate.

    Two presses of one action between ticks still collapse into a single edge. That is inherent
    to a boolean and is the same thing that happens to two presses inside one tick today.
*/
void InputController::Tick(bool f_ticked){
    //debug->Info("Input Controller Tick\n");
    for (KeyMap& km:keymap){
        if (km.state->f_released_read || f_ticked){
            km.state->f_was_released = false;
        }
        if (km.state->f_pressed_read || f_ticked){
            km.state->f_was_pressed = false;
        }
        km.state->f_released_read = false;
        km.state->f_pressed_read = false;

        if (km.state->f_processed){
            //Somebody read it this pass, so it has done its job.
            km.state->delta = 0;
            km.state->f_processed = false;
            km.state->delta_unread_passes = 0;
        }else if (km.state->delta != 0){
            /*
                Nobody read it. Hold it briefly - the render thread consumes mouse deltas for
                camera mouse-look during the window after this call, and clearing immediately
                would leave it reading zeroes - but not forever.

                Forever is what this used to be, and it is a real bug rather than a tidiness
                point: a relative axis ACCUMULATES, so anything that stops the readers banks
                movement. A paused simulation is the obvious case (no tick runs, so no gameplay
                reads) and an app whose input gate returns early is another. The delta then
                arrives in one lump on the first read after the resume, and whatever it drives
                teleports. Measured in APP=Breakout as the paddle snapping to the wall on the
                first scripted step after a pause.
            */
            km.state->delta_unread_passes++;
            if (km.state->delta_unread_passes > INPUT_DELTA_GRACE_PASSES){
                km.state->delta = 0;
                km.state->delta_unread_passes = 0;
            }
        }else{
            km.state->delta_unread_passes = 0;
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
        /*
            The mouse drives the on-screen buttons as pointer 0 - the seam that makes them testable
            on Windows without a touchscreen (docs/touch_input_plan.md step 2).

            Forwarding the button state on every MOVE, rather than only on the up/down messages, is
            what makes this robust without SetCapture. Press inside a button, drag outside the
            window and release, and Win32 delivers no WM_LBUTTONUP to us at all - so the button
            would stay latched and the piece would keep moving, which is exactly the failure the
            touch plan's focus-loss note warns about. Here the next move with MK_LBUTTON clear
            releases it. Capture would also fix it, and would fight ImGui for the mouse.

            The down=true case costs nothing: SubmitPointer deliberately ignores a move of a
            pointer already down, because a button is CAPTURED by the pointer that pressed it.
        */
        SubmitPointer(0,(float)x,(float)y,(wParam & MK_LBUTTON) != 0);
        //debug->Trace("WM_MOUSEMOVE x,y = %li,%li\n",x,y);
    }else if (msg == WM_LBUTTONDOWN){
        SubmitPointer(0,(float)GET_X_LPARAM(lParam),(float)GET_Y_LPARAM(lParam),true);
    }else if (msg == WM_LBUTTONUP){
        SubmitPointer(0,(float)GET_X_LPARAM(lParam),(float)GET_Y_LPARAM(lParam),false);
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
        bool focused = (LOWORD(wParam) != WA_INACTIVE);
        SetFocused(focused);
        if (!focused){
            ReleaseAllTouchPointers();
        }
        debug->Trace("WM_ACTIVATE focus=%i\n",(int)f_has_focus);
    }else if (msg == WM_SETFOCUS){
        SetFocused(true);
    }else if (msg == WM_KILLFOCUS){
        SetFocused(false);
        /*
            OUTSIDE SetFocused, not inside it. SetFocused takes state_mutex and so does
            SubmitSystemKey, which ReleaseAllTouchPointers calls - and state_mutex is not
            recursive, so folding this in would deadlock the window thread on the first alt-tab.

            f_release_all_keys already drops what the mappings are holding, so this is not about
            the key state. It is about the pointer-ownership table and each button's f_down flag:
            without it a button the user was holding when they alt-tabbed stays lit for the
            drawing layer, and its pointer slot stays occupied.
        */
        ReleaseAllTouchPointers();
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
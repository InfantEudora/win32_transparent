#ifndef _INPUTCONTROLLER_H_
#define _INPUTCONTROLLER_H_
class InputController;
#include <windowsx.h>
#include <windows.h>
#include "stdint.h"
#include <vector>
#include <atomic>
#include <mutex>
#include <xinput.h>
#include "Object.h"
#include "type_int2.h"

//How often to look for a gamepad that isn't there yet, in ticks (50 = about 1s at 50Hz).
//XInputGetState on an EMPTY slot is expensive - it goes out to the driver - so probing all four
//slots every tick costs more than a physics step. XInput has no hotplug notification either, so a
//slow rescan is the only way to notice a controller being plugged in. Connected pads are read
//every tick, which is cheap.
#define GAMEPAD_RESCAN_TICKS 50
#define GAMEPAD_MAX_ANALOG_VALUES 8

/*
    Gamepad BUTTONS, as synthetic system keycodes.

    XInput delivers the D-pad, face buttons, bumpers, start/back and stick clicks as bits in
    wButtons - none of which AddGamePadMap can reach, because that maps ANALOG indices. So the one
    control layout a gamepad game usually wants (the D-pad) was not mappable at all.

    Rather than a second mapping table with its own edge detection, each button gets a synthetic
    "system keycode" here and travels the ordinary keyboard path: AddKeyMap to bind it,
    SubmitSystemKey to report it. That means a button gets edge detection (WasKeyReleased),
    multiple-mappings-per-action counting, the unfocused-input gate and recordability for free,
    and an app binds one with the call it already knows:

        input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,INPUT_MOVE_LEFT);
        input->AddKeyMap(VK_LEFT,INPUT_MOVE_LEFT);   //both, on the same action, is fine

    The value is the XInput bit itself offset by the base, so there is no table to keep in step -
    the define IS the mapping. The base is above 0xFFFF, and Win32 VK_ codes are 0..255, so these
    can never collide with a real key.
*/
#define GAMEPAD_SYSKEY_BASE         0x10000
#define GAMEPAD_KEY_DPAD_UP         (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_DPAD_UP)
#define GAMEPAD_KEY_DPAD_DOWN       (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_DPAD_DOWN)
#define GAMEPAD_KEY_DPAD_LEFT       (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_DPAD_LEFT)
#define GAMEPAD_KEY_DPAD_RIGHT      (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_DPAD_RIGHT)
#define GAMEPAD_KEY_START           (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_START)
#define GAMEPAD_KEY_BACK            (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_BACK)
#define GAMEPAD_KEY_LEFT_THUMB      (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_LEFT_THUMB)
#define GAMEPAD_KEY_RIGHT_THUMB     (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_RIGHT_THUMB)
#define GAMEPAD_KEY_LEFT_SHOULDER   (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_LEFT_SHOULDER)
#define GAMEPAD_KEY_RIGHT_SHOULDER  (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_RIGHT_SHOULDER)
#define GAMEPAD_KEY_A               (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_A)
#define GAMEPAD_KEY_B               (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_B)
#define GAMEPAD_KEY_X               (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_X)
#define GAMEPAD_KEY_Y               (GAMEPAD_SYSKEY_BASE + XINPUT_GAMEPAD_Y)

struct GamePadMap{
    int analog_index = -1;
    uint32_t mapped_keycode = 0;   // Our keycode
    int32_t zero_offset = 0;
    int32_t dead_zone = 50;
};

/*
    Handles input messages from a window message queue,
    or get's them from the system.

    We want to be able to map multiple keys to the same task:
    W = Up
    UpArrow = Up

    but also multiple tasks to the same key:
    A -> rotate and up
*/
typedef enum{
    INPUT_NONE  = 0,
    INPUT_TURN_LEFT,
    INPUT_TURN_RIGHT,
    INPUT_TURN_UP,
    INPUT_TURN_DOWN,
    INPUT_MOVE_LEFT,
    INPUT_MOVE_RIGHT,
    INPUT_MOVE_UP,
    INPUT_MOVE_DOWN,
    INPUT_PAUSE,
    INPUT_MOUSE_X,          //absolute cursor position, polled - view data (picking, the UI)
    INPUT_MOUSE_Y,
    INPUT_MOUSE_WHEEL,
    //Raw, unaccelerated, unclipped mouse movement from Raw Input, accumulated per tick and read
    //with GetDelta. This is what mouse-look should use: unlike a cursor-position delta it keeps
    //reporting motion once the pointer is against the edge of the screen, and it is not distorted
    //by the pointer acceleration curve. Existing camera code still reads the INPUT_MOUSE_X/Y
    //cursor delta - switching it over is a one-line change per call site, but it changes feel and
    //sensitivity, so it is left as a deliberate decision rather than folded into this refactor.
    INPUT_MOUSE_DELTA_X,
    INPUT_MOUSE_DELTA_Y,
    INPUT_CLICK_LEFT,
    INPUT_CLICK_MIDDLE,
    INPUT_CLICK_RIGHT,
    INPUT_SHIFT,
    INPUT_LAST
}keycode_t;

//One discrete input change, as a plain value type. POD on purpose: the deterministic-simulation
//direction needs input that can be written to a file, replayed tick for tick, and eventually sent
//over a network - none of which a polled snapshot or a std::function command can do. Everything
//that wants to move the simulation ends up here: the window message thread today, and later a
//raw-input thread, the debug UI, and the MCP server (which becomes just another "player").
typedef enum{
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY_DOWN,       //mapped_keycode is down
    INPUT_EVENT_KEY_UP,         //mapped_keycode is up
    INPUT_EVENT_AXIS_ABSOLUTE,  //value IS the axis's new value (polled mouse position today)
    INPUT_EVENT_AXIS_RELATIVE,  //value is ADDED to the axis (mouse wheel today, raw mouse later)
    //fvalue IS the axis's new scalar value. For anything that isn't a binary action - throttle,
    //steering, brake - whatever the source: a stick deflection, or a scripted player.
    INPUT_EVENT_AXIS_SCALAR
}inputeventtype_t;

struct InputEvent{
    uint16_t type = INPUT_EVENT_NONE;       //inputeventtype_t
    uint16_t mapped_keycode = INPUT_NONE;   //keycode_t this event is about
    int32_t value = 0;                      //integer payload; system keycode for key events
    float fvalue = 0.0f;                    //scalar payload, INPUT_EVENT_AXIS_SCALAR only
};

//A scripted input hold. Exists only because a caller whose round trip is slower than the tick
//rate still needs to "hold a button" - an MCP tool today, a replay driver later. It emits nothing
//but ORDINARY key and axis events and then stops, so neither the simulation nor a recording can
//tell it from a human's finger: the hold is a convenience for the CALLER, never a concept the sim
//knows about. This replaced the per-subsystem Vehicle::HoldDrive/HoldBrake/HoldSteer latches,
//which solved the same problem once per subsystem.
struct SyntheticHold{
    uint32_t mapped_keycode = 0;
    uint32_t ticks_remaining = 0;   //simulation ticks, never milliseconds
    bool f_axis = false;            //scalar axis rather than a button
    float value = 0.0f;
    bool f_started = false;         //the press/set has already been emitted
};

struct KeyState{
    //Number of MAPPINGS currently held, not a boolean - several physical keys can drive one
    //keycode (VK_LSHIFT and VK_RSHIFT both map to INPUT_SHIFT), and the action is down while any
    //of them is. Maintained by edge, in InputController::ApplyPendingEvents.
    int                     f_isdown = 0;
    bool                    f_was_released = false; //the LAST held mapping came up this tick
    bool                    f_processed = false;  // If the input was processed
    int32_t                 value = 0;
    float                   fvalue = 0.0f;
    std::atomic<int32_t>    delta = {0};          // Delta value this tick
    int                     num_mappings = 1;   // Amount of keys that are mapped to this state.
};

struct KeyMap{
    uint32_t system_keycode = 0;   // The system keycode.
    uint32_t mapped_keycode = 0;   // Our keycode
    KeyState* state = NULL;        // The state. Multiple maps can refer to the same state.
    //Whether THIS mapping's key is currently held. Tracked per mapping so f_isdown can be an
    //honest count: the old code re-asserted a level every tick and decremented once per poll,
    //which left a two-mapping keycode reading as down for an extra tick after release.
    bool f_held = false;
};

//Everything the window message thread hands over. Kept out of KeyState so that the fields two
//threads genuinely share are in one place behind one lock, rather than scattered.
struct WindowInputState{
    int2 window_position = {};      //WM_MOVE
    bool f_mouse_over_window = false;
    vec3 hovered_normal = {};       //written by the render thread after the ID/normal readback
    vec3 hovered_position = {};
};

class InputController{
    public:
    InputController();

    //Physics thread, once at the top of a tick: samples the devices and folds everything
    //submitted since the last call into the KeyState that gameplay code reads. Equivalent to
    //PollDevices() followed by ApplyPendingEvents().
    void UpdateKeyState(uint64_t sim_tick);

    //--- The input event stream ---------------------------------------------------------------
    //Callable from ANY thread. This is the only sanctioned way for something outside the physics
    //thread to affect input, and eventually the only way to affect the simulation at all.
    void SubmitEvent(const InputEvent& event);
    //A hardware key/button changed state. Submits one event per mapping of that system key, each
    //carrying the system keycode in InputEvent::value so the applier can count each physical key
    //separately. Key-downs are dropped while unfocused; key-ups always go through, so nothing can
    //stay latched. Called from the raw input thread.
    void SubmitSystemKey(uint32_t system_keycode, bool down);
    //Accumulate onto a relative axis (raw mouse movement, wheel). Any thread.
    void SubmitAxisDelta(uint32_t mapped_keycode, int32_t delta);

    //--- Scripted input: a "player" that isn't a person ------------------------------------------
    //Hold a button, or a scalar axis at `value`, for duration_ticks of SIMULATION time - so it
    //behaves identically whether the sim is running freely, paused and single-stepped, or replayed.
    //Re-asserting the same control refreshes its remaining time instead of stacking a second hold,
    //and duration_ticks == 0 releases it. Deliberately NOT focus-gated: this input does not come
    //from the OS, so an unfocused window must not silence it (that would break every scripted MCP
    //run, which is exactly when the window is not in front). Any thread.
    void HoldKey(uint32_t mapped_keycode, uint32_t duration_ticks);
    void HoldAxis(uint32_t mapped_keycode, float value, uint32_t duration_ticks);
    void ReleaseSynthetic();    //cancel every scripted hold, releasing each properly
    //Whether any scripted hold is currently live. For an app whose input handling is gated on the
    //window having focus: that gate is there to stop OS input meant for another application from
    //driving the game, and a scripted hold is not OS input - see the note above. Any thread.
    //
    //This deliberately stays TRUE for the one further tick on which the last hold's RELEASE is
    //delivered. A hold is erased in the same AdvanceSyntheticHolds call that emits its key-up, so
    //without this it reads false on exactly the tick WasKeyReleased() reports the release - and an
    //app gating on `!HasFocus() && !HasSyntheticHolds()` would silently drop every edge-triggered
    //scripted action (a rotate, a fire, a hard drop) while never missing a held one. That is the
    //one case the gate exists to allow, so the flag has to outlive the hold by the tick that
    //carries its edge.
    bool HasSyntheticHolds();
    //Current value of a scalar axis. 0 when nothing is driving it.
    float GetAxis(uint32_t mapped_keycode);
    //Physics thread only. Drains everything submitted since the last call, applies it to KeyState,
    //and keeps it as this tick's input.
    //
    //sim_tick is Scene::GetPhysicsTick(). Input is polled and applied on EVERY physics-thread loop,
    //including while the simulation is paused - otherwise a key press could never unpause it - but
    //scripted holds may only count down when the simulation clock actually moves. This function is
    //called far more often than a tick runs (the loop keeps spinning while paused, and while
    //single-stepping it spins many times per stepped tick), so counting loops instead of ticks
    //would put hold durations back on wall-clock time, which is the exact bug tick-denominating
    //them was meant to remove.
    void ApplyPendingEvents(uint64_t sim_tick);
    //What ApplyPendingEvents just applied: the exact input this tick ran with. A recorder writes
    //this out; a replay submits it back. Physics thread only, valid until the next call.
    const std::vector<InputEvent>& GetTickEvents() const { return tick_events; }
    //Samples whatever still has to be polled. The absolute cursor position always (Raw Input
    //reports movement, not a cursor), and the keyboard/mouse buttons only when raw input is not
    //running - see SetRawInputActive.
    void PollDevices();

    //Told by RawInputSource once raw acquisition is actually live. While it is, PollDevices stops
    //sampling keys and HandleMessage stops forwarding WM_MOUSEWHEEL, so nothing is counted twice.
    //If raw input fails to start, this is never set and the polling path stays in charge - the
    //engine keeps working, just without edges or raw deltas.
    void SetRawInputActive(bool active);
    bool IsRawInputActive(){ return f_raw_input_active; }

    //False while another application is in the foreground. Input is not sampled then, so the
    //simulation doesn't respond to keys pressed in whatever the user alt-tabbed to. Maintained
    //from WM_ACTIVATE/WM_SETFOCUS/WM_KILLFOCUS in HandleMessage. Note ImGui does NOT come through
    //here (imgui_impl_win32 has its own handler), so the debug UI is unaffected by this gate.
    bool HasFocus(){ return f_has_focus; }

    //Called from thread that created the window
    void HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    KeyMap* AddKeyMap(uint32_t syskey, uint32_t mapped);
    KeyMap* GetBySystemKey(uint32_t sys_code);
    KeyMap* GetByMappedKey(uint32_t mapped_code);

    bool    IsKeyDown(uint32_t mapped);
    bool    WasKeyReleased(uint32_t mapped);
    int32_t GetDelta(uint32_t mapped, KeyMap** map_out = NULL);
    int32_t GetValue(uint32_t mapped, KeyMap** map_out = NULL);


    int2    GetAbsoluteMousePosition();
    int2    GetRelativeMousePosition(); //Relative to the window

    void    Tick();
    void    SetHoveredObjectID(objectid_t id);
    objectid_t GetHoveredObjectID();
    void    SetHoveredNormal(vec3 normal);
    vec3    GetHoveredNormal();
    void    SetHoveredPosition(vec3 pos);
    vec3    GetHoveredPosition();
    bool    IsMouseOverWindow();

    //There needs to be at least some form of feedback from UI which object was selected/hovered.


    //--- Gamepad (XInput) -----------------------------------------------------------------------
    //Absorbed from the old separate GamePadController, which was a parallel input class with its
    //own keymap that two apps polled by hand. Same analog indices, same normalisation, same rumble
    //decay, so existing mappings keep their meaning - but sampled once per tick with the rest of
    //the input, across all four XInput slots instead of a hardcoded slot 0, and with reconnect.
    void ListDevices();
    GamePadMap* AddGamePadMap(int analog_index, uint32_t mapped);
    float GetNormalizedAnalogValue(uint32_t mapped_key);
    void SendMotorData(int l, int r);

    int analog_values[GAMEPAD_MAX_ANALOG_VALUES] = {0};
    int lmotor = 0;
    int rmotor = 0;
    int dev_index = -1;     //XInput user index in use, -1 when no controller is connected
    std::vector<GamePadMap>gamepad_map;

    //Mouse position is also stored in keymap, and seperately
    std::vector<KeyMap>keymap;

protected:
    //Guards ONLY the cross-thread handoff: the pending event queue and the WindowInputState the
    //window/render threads write. KeyState is deliberately not in here - it is written and read
    //solely by the physics thread (inside Application's physics_mutex), so the hot IsKeyDown /
    //GetDelta path that gameplay code hits many times a tick stays lock-free. This replaces the
    //old "atomicise the odd field and hope" approach, and the `hovered_normal` TODO with it.
    std::mutex state_mutex;
    void SetMouseOverWindow(bool over); //window thread; takes state_mutex
    void SetFocused(bool focused);      //window thread; raises f_release_all_keys on focus loss
    void PollGamepad();                 //physics thread, from PollDevices
    //Diffs `buttons` (an XInput wButtons word) against the last one seen and submits a key event
    //for every bit that changed, so a pad button behaves exactly like a keyboard key. Called with
    //0 when the pad is unplugged or the window loses focus, which releases anything still held.
    void ApplyGamepadButtons(uint16_t buttons);
    uint16_t gamepad_buttons = 0;       //last wButtons applied, for edge detection
    uint32_t gamepad_rescan_countdown = 0;
    //Physics thread, first thing in ApplyPendingEvents: advances every scripted hold by one tick,
    //emitting the ordinary events that start and end it.
    void AdvanceSyntheticHolds();
    uint64_t last_hold_tick = 0;
    bool f_hold_tick_valid = false;
    void AddSyntheticHold(uint32_t mapped_keycode, bool axis, float value, uint32_t duration_ticks);
    std::vector<SyntheticHold> synthetic_holds; //guarded by state_mutex
    //Raised when a hold emits its key-up and is erased, cleared by the NEXT AdvanceSyntheticHolds
    //- so it marks exactly the tick on which that release is readable. See HasSyntheticHolds.
    bool f_synthetic_release_tick = false;       //guarded by state_mutex
    std::vector<InputEvent> pending_events; //producers append, the physics thread drains
    std::vector<InputEvent> tick_events;    //physics thread only: this tick's applied input
    WindowInputState window_state;

    //Read on the physics thread every poll, written from the window thread, and only ever a plain
    //flag - atomic is enough and avoids taking the lock in the polling hot path.
    std::atomic<bool> f_has_focus{true};
    std::atomic<bool> f_raw_input_active{false};
    //Set when focus is lost. The next ApplyPendingEvents releases every held mapping, so a key
    //held down at the moment the user alt-tabs away cannot stay stuck - belt and braces next to
    //SubmitSystemKey's focus gate, which relies on the key-up actually arriving.
    std::atomic<bool> f_release_all_keys{false};

    int2 mouse_position;    //physics thread only, from the polled cursor position

    std::atomic<objectid_t>hovered_object = {OBJECTID_INVALID};
};

#endif
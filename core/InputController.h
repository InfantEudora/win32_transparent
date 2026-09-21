#ifndef _INPUTCONTROLLER_H_
#define _INPUTCONTROLLER_H_
class InputController;
//Only the ACQUISITION half of this class is platform-specific (PollDevices, PollGamepad, the
//VK_*/XInput plumbing, HandleMessage) - see InputController_win32.cpp / InputController_android.cpp.
//Everything below that line (mappings, KeyState, edges, scripted holds, axes) is portable, which
//is why these three headers are the only Windows thing in here.
#if defined(_WIN32)
#include <windowsx.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#endif
#include "stdint.h"
#include <vector>
#include <atomic>
#include <mutex>
#if defined(_WIN32)
#include <xinput.h>
#endif
#include "Object.h"
#include "type_int2.h"

#if defined(__ANDROID__)
// Forward declarations so the header doesn't drag the NDK's android_native_app_glue
// and sensor headers into every translation unit that merely wants a keymap.
struct android_app;
struct AInputEvent;
struct ASensorManager;
struct ASensor;
struct ASensorEventQueue;
#include "type_vec2.h"

// Touch sample for one pointer index, refreshed on every event (independent of any log
// throttling) so callers -- e.g. an ImGui panel -- can show it live.
struct TouchState {
    char    str_action[16] = "none";
    //The platform's STABLE id for this finger. This array is indexed by the pointer's INDEX in
    //the event, which is not the same thing and does not survive another finger lifting -- see
    //the fill loop in InputController_android.cpp. Carried so a debug view can show which is
    //which, and so index-vs-id confusion is visible rather than silent.
    int32_t id = -1;
    int32_t action = 0;
    float   x = 0.0f, y = 0.0f, pressure = 0.0f, size = 0.0f; //Pressure seems to always be 1.0f and size 0.0f
    char    tool[16] = "none";
    int drag_active = 0;
    float drag_last_x = 0.0f;
    float drag_last_y = 0.0f;
    float drag_rot_x = 0.0f;
    float drag_rot_y = 0.0f;
};

// Last key event seen from the device's buttons (hardware or touch-panel virtual keys).
// Was called KeyState before the engine's InputController came across -- that name now
// belongs to the mapping machinery, and these are unrelated things: this is a raw
// "what did the OS last report", not a mapped action's state.
struct LastKeyEvent {
    char name[16];
    char str_action[8];
    int32_t action;
    int32_t keycode;
};
#endif

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
    SubmitSystemKey to report it. That means a button gets edge detection (WasKeyPressed and
    WasKeyReleased),
    multiple-mappings-per-action counting, the unfocused-input gate and recordability for free,
    and an app binds one with the call it already knows:

        input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,INPUT_MOVE_LEFT);
        input->AddKeyMap(VK_LEFT,INPUT_MOVE_LEFT);   //both, on the same action, is fine

    The value is the XInput bit itself offset by the base, so there is no table to keep in step -
    the define IS the mapping. The base is above 0xFFFF, and Win32 VK_ codes are 0..255, so these
    can never collide with a real key.
*/
#define GAMEPAD_SYSKEY_BASE         0x10000

/*
    XInput's button bits, spelled out for builds with no <xinput.h>.

    The GAMEPAD_KEY_* defines below are built from these, and being macros they expand only where
    they are USED - so a platform without XInput compiles this header happily right up until the
    first app writes GAMEPAD_KEY_A, and then reports an undeclared identifier from a header that
    looks entirely portable. These values are a fixed part of the XInput ABI (XINPUT_GAMEPAD_*
    in XInput.h) and cannot drift, so naming them here costs nothing and keeps the keycode space
    identical on every platform - which matters, because a recorded input run carries these
    numbers and has to replay the same way wherever it is played back.
*/
#if !defined(_WIN32)
#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000
#endif

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

/*
    Synthetic keycodes for the on-screen buttons - see AddTouchButton further down.

    EVERY INPUT SOURCE MUST OWN ITS KEYCODES, and the failure when one does not is silent. A key
    event whose `value` is 0 falls through to GetByMappedKey (InputController.cpp), which returns
    the FIRST mapping for that action - so a button submitting {KEY_DOWN, INPUT_TETRIS_LEFT, 0}
    would share f_held with VK_LEFT. Press the button, then press and release the arrow key, and
    the action releases with a finger still on the screen, because both wrote the same mapping.
    Invisible on a device with no keyboard; very visible on Windows, which is where this is tested.

    AddTouchButton allocates these itself and never shows them to the app, which also means two
    buttons bound to one action get DIFFERENT keycodes and so correct f_isdown counting for free -
    the same property two physical keys on one action already have.

    Above the gamepad range (which reaches 0x18000 at most, being the base plus a 16-bit XInput
    bit) and far above both Win32 VK_ codes (0..255) and Android AKEYCODE_ values, so nothing can
    collide.
*/
#define TOUCH_SYSKEY_BASE           0x20000

//How many simultaneously-down pointers are tracked. Five is what the Android port's panel reports
//via AMotionEvent_getPointerCount(). It is NOT inside any Android guard, and that is the point:
//the whole reason the seam is at SubmitPointer is that a mouse on Windows can drive it as
//pointer 0 before any touchscreen exists.
#define INPUT_CONTROLLER_MAX_TOUCHES 5

//GamePadMap is gone: an analog stick is now a KeyMap like everything else - see the analog block
//in KeyMap below, and AddGamePadMap. It used to be a parallel table with its own lookup, which
//meant a gamepad axis had no KeyState, so GetAxis returned 0 for it forever and a scripted
//HoldAxis on it was dropped without a word.

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

/*
    How many physics passes a RELATIVE axis delta may sit unread before Tick() throws it away.

    It cannot be zero. A delta is not necessarily consumed by the simulation: the render thread
    reads mouse deltas for camera mouse-look (Application::UpdateUICameraControls -> GetDelta) at
    framerate, and Tick() runs at the end of a physics pass, so a delta has to survive at least
    long enough for that window to open or mouse-look loses motion.

    It must also not be unbounded, which is what it was. Deltas ACCUMULATE (INPUT_EVENT_AXIS_RELATIVE
    adds), and the clear was conditional on somebody having read them - so across anything that
    stops the readers, a pause above all, the movement piled up silently and arrived as one jump on
    the tick after the resume. Two passes is ~40 ms at 50 Hz: several frames of grace for the render
    thread, and a hard cap on how much travel a pause can bank.
*/
#define INPUT_DELTA_GRACE_PASSES 2

struct KeyState{
    //Number of MAPPINGS currently held, not a boolean - several physical keys can drive one
    //keycode (VK_LSHIFT and VK_RSHIFT both map to INPUT_SHIFT), and the action is down while any
    //of them is. Maintained by edge, in InputController::ApplyPendingEvents.
    int                     f_isdown = 0;
    bool                    f_was_released = false; //the LAST held mapping came up this tick
    //The mirror of f_was_released: the FIRST mapping went down this tick (f_isdown 0 -> 1).
    //
    //On a keyboard, firing an action on the release edge instead is unnoticeable, which is how
    //this gap survived unnoticed - Tetris fires rotate, hard drop and hold that way. Under a
    //thumb on a touchscreen it reads as lag: the piece turns when you LIFT. Having both edges
    //available is what makes the choice a decision about FEEL, made per action, rather than a
    //constraint of the API.
    bool                    f_was_pressed = false;
    /*
        Whether anything has actually READ each edge since it was raised - the two are separate
        because an action may legitimately be watched on one edge and not the other.

        This is what lets an edge OUTLIVE the pass it was raised on, which is backlog item 88.
        Input is drained on every physics pass (a paused editor still needs a working camera), but
        gameplay only runs on the passes that tick, so a press arriving while the simulation is
        paused used to be raised and cleared with no tick in between ever seeing it. Tick() now
        keeps an edge nobody has read yet; see the rule there.

        Physics thread only, like the flags they describe.
    */
    bool                    f_pressed_read = false;
    bool                    f_released_read = false;
    bool                    f_processed = false;  // If the input was processed
    int32_t                 value = 0;
    float                   fvalue = 0.0f;
    std::atomic<int32_t>    delta = {0};          // Delta value this tick
    //Physics passes this delta has sat unread. Tick() drops it past INPUT_DELTA_GRACE_PASSES, so
    //an axis nobody is consuming cannot bank movement indefinitely. Physics thread only.
    int                     delta_unread_passes = 0;
    int                     num_mappings = 1;   // Amount of keys that are mapped to this state.
};

/*
    One physical thing driving one action. Several may point at the same KeyState (VK_LSHIFT and
    VK_RSHIFT both -> INPUT_SHIFT), which is what lets an action be held by any of them.

    A mapping is either a KEY or an ANALOG AXIS, and `analog_index` is what says which. The two
    used to live in separate tables with separate lookups; they are one table now, because the
    only real difference between them is WHICH PIECE OF HARDWARE the mapping listens to, and
    everything downstream - the KeyState, the event stream, recordability, the focus gate - wants
    to treat them identically. See AddGamePadMap.
*/
struct KeyMap{
    uint32_t system_keycode = 0;   // The system keycode, for a KEY mapping.
    uint32_t mapped_keycode = 0;   // Our keycode
    KeyState* state = NULL;        // The state. Multiple maps can refer to the same state.
    //Whether THIS mapping's key is currently held. Tracked per mapping so f_isdown can be an
    //honest count: the old code re-asserted a level every tick and decremented once per poll,
    //which left a two-mapping keycode reading as down for an extra tick after release.
    bool f_held = false;

    //--- Analog source ----------------------------------------------------------------------
    //Which gamepad analog drives this action, or -1 when this mapping is an ordinary key. This
    //is the analog counterpart of system_keycode above: it names the hardware, and the two are
    //mutually exclusive on any one mapping.
    int     analog_index = -1;
    //Applied before the dead zone, for a stick whose rest position is not centred.
    int32_t zero_offset = 0;
    //Deflection below which the axis reads as zero. The default is nearly nothing; a worn
    //thumbstick wants a few thousand, or its rest position registers as a steady push.
    int32_t dead_zone = 50;
    //The last value PollGamepad actually SUBMITTED for this mapping, and whether it ever has.
    //Kept per mapping, next to the hardware description it belongs to.
    //
    //This is what makes polling and events coexist: an axis event is submitted only when the
    //stick's normalised value CHANGES, so a centred (or absent) stick goes quiet after one zero
    //instead of stamping 0.0 over the action every single tick - which would silently defeat
    //every scripted HoldAxis the moment a controller happened to be plugged in.
    float   last_analog_value = 0.0f;
    bool    f_analog_sent = false;

    bool IsAnalog() const { return analog_index >= 0; }
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
    //and keeps it as this tick's input. Called on EVERY pass of the physics loop, ticking or not,
    //because the pause key and the editor's picking both read input on passes that do not tick.
    //
    //It deliberately does NOT advance scripted holds - see ApplyTickInput, which does, and the
    //comment there for why the two cannot be the same call.
    //
    //sim_tick is Scene::GetPhysicsTick(). Input is polled and applied on EVERY physics-thread loop,
    //including while the simulation is paused - otherwise a key press could never unpause it - but
    //scripted holds may only count down when the simulation clock actually moves. This function is
    //called far more often than a tick runs (the loop keeps spinning while paused, and while
    //single-stepping it spins many times per stepped tick), so counting loops instead of ticks
    //would put hold durations back on wall-clock time, which is the exact bug tick-denominating
    //them was meant to remove.
    void ApplyPendingEvents(uint64_t sim_tick);
    /*
        Physics thread only, on a pass that WILL tick, before anything reads an edge. Advances
        every scripted hold by one tick and applies what that emits.

        THIS IS SEPARATE FROM ApplyPendingEvents BECAUSE THE TWO ANSWER TO DIFFERENT CLOCKS, and
        conflating them was backlog item 84. Holds are denominated in ticks, so they may only
        advance when a tick actually runs; the old code approximated that with "sim_tick has
        changed since last time", checked from ApplyPendingEvents - which runs BEFORE BeginPass
        drains the command queue and decides whether this pass ticks. Free-running that is
        harmless, because every pass ticks and the approximation is off by one pass at most.

        While single-stepping it is fatal. On the pass that will run tick N the clock still reads
        N-1, so no hold advances; the hold advances on the NEXT pass, by which time the queued
        step is spent and the pass does not tick. The key-down was therefore raised on a spinning
        pass and cleared by NextInput() at the end of it, with no tick in between to see it. Every
        edge-triggered gameplay action in every app was undeliverable under sim_step - a fire, a
        serve, a launch, a rotate - while level-triggered input worked, because f_isdown survives
        a pass boundary and an edge flag does not.

        Driving it from the tick itself removes the approximation rather than tuning it.
    */
    void ApplyTickInput(uint64_t sim_tick);
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

    /*
        THE predicate for "should this app act on input at all right now" - use this rather than
        writing the gate by hand.

            if (!input->IsInputLive()){
                return;     //somebody else's keystrokes; not ours
            }

        It is `HasFocus() || HasSyntheticHolds()`, and the second half is the part that was got
        wrong by hand. Gating on focus alone looks obviously right and silently breaks every
        scripted run: an MCP- or replay-driven session is precisely the case where this window is
        NOT in front, so a focus-only gate drops the input that automation exists to deliver.
        Scripted input does not come from the OS, so the reason for the gate does not apply to it.

        Three apps wrote this predicate out by hand and had to get both halves right; the fourth
        (`ApplicationTank`) deliberately does something different, and that difference is worth
        knowing - it puts its HARDWARE handling behind HasFocus() and its scripted handling outside
        any gate at all, because its gamepad block writes the pedals unconditionally and would
        otherwise fight a scripted drive. Either shape is fine. Writing `!HasFocus()` and stopping
        there is the one that is not.

        WHEN NOT TO USE IT: cursor-driven work. "Where is the mouse pointing" is meaningless while
        another application owns the pointer, and a scripted hold must not make a camera chase a
        cursor being used elsewhere - so picking, click-drag, mouse-look and wheel zoom stay on
        plain HasFocus(). The rule is that this predicate is about ACTIONS, not about the cursor.
    */
    bool IsInputLive(){ return HasFocus() || HasSyntheticHolds(); }

#if defined(_WIN32)
    //Called from thread that created the window. Takes the HWND because answering a message is
    //not always enough - WM_MOUSELEAVE has to be ASKED for, per entry, on the window it concerns.
    void HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#elif defined(__ANDROID__)
    //--- Android acquisition -----------------------------------------------------------------
    //The Android counterpart of HandleMessage/PollDevices: sensors and the touch panel are this
    //platform's input hardware. These feed the SAME keymap/KeyState machinery above via
    //SubmitSystemKey/SubmitAxisDelta, so a sensor axis is indistinguishable from a thumbstick
    //to everything downstream -- including GetAxis, HoldAxis and any recorded/scripted run.
    //See InputController_android.cpp.

    //Enables whichever of the gyroscope/accelerometer this device actually has, both on the same
    //event queue (a device missing one just never produces events of that type).
    void InitSensors(struct android_app* app);
    void DrainSensorEvents();

    //Returns 1 if the event was consumed (matches android_app::onInputEvent's convention).
    //suppress_drag is true when the caller wants this event not to affect drag_rot_x/y (e.g.
    //because ImGui is capturing it -- dragging a slider shouldn't also spin the scene under it).
    int32_t HandleInputEvent(AInputEvent* event, bool suppress_drag);
#endif

    KeyMap* AddKeyMap(uint32_t syskey, uint32_t mapped);
    KeyMap* GetBySystemKey(uint32_t sys_code);
    KeyMap* GetByMappedKey(uint32_t mapped_code);

    /*
        ON-SCREEN BUTTONS - a THIRD INPUT FAMILY, alongside AddKeyMap and AddGamePadMap, so an app
        binds one the same way it binds the other two and nothing downstream can tell them apart:

            input->AddKeyMap(VK_LEFT,        INPUT_TETRIS_LEFT);
            input->AddGamePadMap(0,          INPUT_TETRIS_LEFT);
            input->AddTouchButton(rect_left, INPUT_TETRIS_LEFT);   //this one

        IN HERE RATHER THAN BESIDE IT, deliberately. The note further down on absorbing the gamepad
        records that the old separate GamePadController was wrong precisely because it was a
        parallel input class with its own keymap that apps polled by hand. A TouchController that
        apps polled by hand would be the same mistake with a different noun.

        EDGES, NEVER LEVELS. SubmitPointer emits one KEY_DOWN on press and one KEY_UP on release,
        and nothing anywhere polls "is a finger inside this rect". That is the load-bearing choice:
        with edges, the rate the pointer layer runs at is irrelevant, because KeyState holds the
        action down between the two events and DAS/ARR keeps counting in ticks on the physics
        thread where it already lives. Poll instead and button feel becomes a function of frame
        rate, and the rect list and the live pointer positions both have to cross threads.

        THIS PANEL HAS NO RENDERING OPINION. It owns rectangles and emits keycodes;
        GetTouchButtons() hands the list to whatever draws - UIOverlay, today. Input does not draw,
        and the drawing layer never feeds anything back.

        Recording and replay come out right for free: because these are ordinary key events on a
        synthetic keycode, a recording captures "touch button 2 down @ tick 412" - the ACTION, not
        the finger position - which replays at any screen size, any dpi, and after any later change
        to the layout. Route touch through picking or record raw coordinates instead and every
        recording made before a button moved ten pixels is silently wrong.
    */

    //Screen-space rectangle in PIXELS, top-left origin - the same space Android's
    //AMotionEvent_getX/getY report in, and the same one UIOverlay draws in. That correspondence is
    //deliberate: the rectangle that gets hit-tested is the rectangle that gets drawn.
    //
    //Physical-size layout (a button in millimetres, anchored to a corner) belongs a layer ABOVE
    //this - it needs dpi, and this struct is what it would produce.
    struct TouchRect{
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool Contains(float px,float py) const {
            return (px >= x) && (px < (x + w)) && (py >= y) && (py < (y + h));
        }
    };

    struct TouchButton{
        TouchRect rect;
        uint32_t  system_keycode = 0;   //allocated from TOUCH_SYSKEY_BASE; the app never sees it
        uint32_t  mapped_keycode = 0;   //what it drives - kept for drawing and debugging
        //Optional, for a drawing layer to put on the button. A FIXED BUFFER rather than a
        //std::string or a borrowed const char*: this struct is walked from the thread that
        //hit-tests, and a caller passing a temporary would otherwise leave a dangling pointer.
        //Short on purpose - a legend, not a sentence.
        char      label[12] = "";
        //Purely for a drawing layer: true while some pointer is holding this button. Written by
        //SubmitPointer on the same thread that hit-tests.
        bool      f_down = false;
        /*
            Where the pointer holding this button is NOW, in the same screen pixels as `rect`.
            Only meaningful while f_down; stale once it clears.

            THIS IS WHAT MAKES A SLIDER POSSIBLE, and it is additive rather than a change to how
            capture works. A button's keycode carries an EDGE - pressed, released - and that is
            the right shape for a button and the wrong one for a control whose whole value is
            where along itself you are holding it. SubmitPointer already receives every move of a
            held pointer (see the WM_MOUSEMOVE note in this file's win32 block, which forwards
            the button state on every move so a release outside the window is not missed); it
            simply had nowhere to put the position. Now it does.

            Capture is UNCHANGED: the button a pointer pressed is still the button it holds until
            that pointer lifts, wherever it travels in between. So a drag that leaves the rect
            keeps driving the control it started on, which for a slider is exactly right - the
            alternative, dropping the drag the moment the cursor slips off a 10-pixel track, is
            the thing every slider in every toolkit is careful not to do.

            RACY BY DESIGN, like f_down beside it: written by the thread that hit-tests, read by
            whichever thread draws or applies it, with no lock. Two floats can be read one frame
            apart from each other; for a control this is a sub-pixel artefact on one frame, and
            it is not worth a mutex on the pointer path to prevent.
        */
        float     pointer_x = 0.0f;
        float     pointer_y = 0.0f;
    };

    /*
        Binds a rectangle to an action. Call during setup, alongside the AddKeyMap calls.

        RETURNS AN INDEX, not a pointer. A pointer into `touch_buttons` is invalidated by the very
        next AddTouchButton, so a caller that stored one and then added another button would be
        holding a dangling pointer with nothing to say so - and adding buttons in a row is the
        normal usage.

        SET THE RECT TO ANYTHING (zero is fine) AND POSITION IT IN LayoutTouchButtons INSTEAD. See
        SetTouchButtonRect for why the two are separate, and Application::LayoutTouchButtons for
        where the geometry belongs.
    */
    int AddTouchButton(const TouchRect& rect, uint32_t mapped, const char* label = NULL);

    /*
        Moves an existing button. This is the ONLY part of a button that may change after setup.

        The split is what makes a relayout safe. A button's IDENTITY - its synthetic keycode and
        the KeyMap that carries it - is allocated once by AddTouchButton and never touched again,
        because `keymap` is read lock-free by PollDevices on the physics thread and appending to it
        at run time would both race that walk and grow it without bound on every resize. Its
        GEOMETRY is four floats that only SubmitPointer reads.

        So a window resize or an orientation change re-runs the layout and nothing re-runs the
        binding. The residual race is those four float writes against a hit test happening in the
        same instant; the worst outcome is one press attributed to the wrong button during the
        frame the window changed size, which is not worth a lock on the pointer path to prevent.

        Out-of-range indices are ignored, so a layout that has lost count fails quietly rather than
        corrupting a neighbour.
    */
    void SetTouchButtonRect(int index, const TouchRect& rect);
    int  GetNumTouchButtons() const {return (int)touch_buttons.size();}

    /*
        One pointer changed. `pointer_id` is the platform's STABLE id for that finger, not its
        index in the event - indices shift when a finger lifts while others stay down, and
        ownership would transfer to the wrong finger in exactly the multi-touch case this design
        exists for. The Win32 mouse is always pointer 0.

        down=true on press and on every subsequent move of a pointer already down; down=false on
        release. A pointer that presses inside a button CAPTURES it and holds it until that same
        pointer lifts, wherever it moves in between - a millimetre of thumb drift must not drop a
        held direction, and an edge must not chatter at a rect boundary. Sliding from one button
        onto another therefore does nothing; whether it should is a question about feel, to be
        answered with the buttons in front of you rather than now.

        Safe from whichever thread delivers pointer events, but only ONE such thread: it reaches
        KeyState through SubmitSystemKey (which takes state_mutex and queues) while keeping its own
        pointer-ownership table lock-free. On Win32 that is the window thread; on Android it is the
        one pumping the ALooper.
    */
    void SubmitPointer(int32_t pointer_id, float x, float y, bool down);

    //Releases every button any pointer is holding, and forgets the pointers. For a cancelled
    //gesture (ACTION_CANCEL) and for focus loss, neither of which delivers an ordinary release.
    void ReleaseAllTouchPointers();

    //The rect list, for a drawing layer. Input does not draw.
    const std::vector<TouchButton>& GetTouchButtons() const {return touch_buttons;}

    bool    IsKeyDown(uint32_t mapped);
    bool    WasKeyReleased(uint32_t mapped);
    //Symmetric with WasKeyReleased: true on the tick the action went down. See KeyState.
    bool    WasKeyPressed(uint32_t mapped);
    int32_t GetDelta(uint32_t mapped, KeyMap** map_out = NULL);
    int32_t GetValue(uint32_t mapped, KeyMap** map_out = NULL);


    int2    GetAbsoluteMousePosition();
    int2    GetRelativeMousePosition(); //Relative to the window

    /*
        End of a physics pass: clear the per-pass transition flags. `f_ticked` is whether the pass
        actually ran a tick, and an unread edge survives a pass that did not - backlog item 88.
        The full rule, and why it is not simply "keep it until a tick", is on the definition.

        NOT defaulted, deliberately. A default would be the old unconditional clear, which is
        precisely the bug - and it would be chosen silently, by a caller that had not thought
        about which clock it is on. Being made to answer the question is the point.
    */
    void    Tick(bool f_ticked);
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

    /*
        Bind a gamepad analog to an action. This is AddKeyMap for a stick: it makes an ordinary
        entry in `keymap` with an ordinary KeyState, so the axis is readable with GetAxis, can be
        driven by HoldAxis, and is recorded like any other input.

        That it did NOT do those things is the bug this replaced. AddGamePadMap used to fill a
        separate `gamepad_map` and create no KeyState at all, so the obvious one-liner

            input->AddGamePadMap(0,INPUT_MY_STEER);   //looks complete, was not

        left GetAxis returning 0 forever and every scripted HoldAxis on that action dropped on the
        floor, with nothing logged anywhere. Apps worked around it by ALSO calling
        AddKeyMap(0,action) to force a KeyState into existence - an idiom that was never written
        down. That extra call is now unnecessary (harmless if left in; it just adds a second
        mapping onto the same state).

        dead_zone and zero_offset are here rather than left to be poked into the returned pointer
        because the returned pointer is only valid until the next mapping is added - `keymap` is a
        vector. Pass them and keep nothing.
    */
    KeyMap* AddGamePadMap(int analog_index, uint32_t mapped, int32_t dead_zone = 50, int32_t zero_offset = 0);

    //Current value of a gamepad analog, -1..1. EXACTLY GetAxis now: a stick's value is applied to
    //the same KeyState as every other scalar axis, so there is one place to read an axis from
    //whether a thumb, a script or a replay is driving it. Kept as a name because four apps call it.
    float GetNormalizedAnalogValue(uint32_t mapped_key);
    void SendMotorData(int l, int r);

    //Raw, unnormalised XInput values as of the last poll, indexed by analog_index. The mapped,
    //dead-zoned, -1..1 value is what GetAxis gives you; this is the hardware underneath it.
    int analog_values[GAMEPAD_MAX_ANALOG_VALUES] = {0};
    int lmotor = 0;
    int rmotor = 0;
    int dev_index = -1;     //XInput user index in use, -1 when no controller is connected

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
#if defined(__ANDROID__)
public:
    //On Windows this is driven from inside HandleMessage, off WM_ACTIVATE. Android has no
    //message pump here: focus arrives as APP_CMD_GAINED_FOCUS/APP_CMD_LOST_FOCUS in
    //Application, so the setter has to be reachable from outside the class.
#endif
    void SetFocused(bool focused);      //window thread; raises f_release_all_keys on focus loss
#if defined(__ANDROID__)
protected:
#endif
    void PollGamepad();                 //physics thread, from PollDevices
    //The shared body of AddKeyMap and AddGamePadMap: makes one mapping, giving it a fresh
    //KeyState or sharing the one an existing mapping for the same action already has.
    KeyMap* AddMapping(uint32_t syskey, uint32_t mapped, int analog_index);
    //Turns this poll's analog_values into INPUT_EVENT_AXIS_SCALAR events, one per analog mapping
    //whose value CHANGED. Called from PollGamepad, including on the disconnect and lost-focus
    //paths after they zero the values - so a stick held at full deflection when the cable is
    //pulled reports its way back to centre instead of staying latched.
    void SubmitAnalogAxes();
    //Diffs `buttons` (an XInput wButtons word) against the last one seen and submits a key event
    //for every bit that changed, so a pad button behaves exactly like a keyboard key. Called with
    //0 when the pad is unplugged or the window loses focus, which releases anything still held.
    void ApplyGamepadButtons(uint16_t buttons);
    uint16_t gamepad_buttons = 0;       //last wButtons applied, for edge detection
    uint32_t gamepad_rescan_countdown = 0;

    //--- On-screen buttons, see AddTouchButton ---------------------------------------------------
    //Which button each live pointer captured on press. Sized to the touch limit the platform layer
    //already clamps to; a pointer beyond that is ignored rather than tracked wrongly.
    struct TouchPointer{
        int32_t id = -1;            //-1 = slot free
        int     button_index = -1;  //-1 = pressed outside every button, tracked so its release is
                                    //recognised as this pointer's rather than hunted for
    };
    std::vector<TouchButton> touch_buttons;
    TouchPointer touch_pointers[INPUT_CONTROLLER_MAX_TOUCHES];
    //Next synthetic keycode to hand out. One per button, never reused.
    uint32_t next_touch_syskey = TOUCH_SYSKEY_BASE;
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
    //Shared body of ApplyPendingEvents and ApplyTickInput: take everything submitted since the
    //last drain and apply it. f_append keeps what tick_events already holds, so a ticking pass
    //ends up with the hardware input it sampled AND the scripted input its tick advanced - which
    //together are that tick's input, and are what a recorder has to write out.
    void DrainAndApplyEvents(bool f_append);
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

#if defined(__ANDROID__)
public:
    //--- Android sensor/touch state ------------------------------------------------------------
    //Raw hardware state, kept as its own members rather than folded into analog_values[] because
    //callers (hello_world's debug panel) display it directly. Mapping a sensor axis to an ACTION
    //goes through AddGamePadMap/analog_values like a thumbstick does -- see the note there.

    //Gyro: angular velocity (rad/s) plus integrated rotation from physically turning the device.
    float latest_gyro[3] = { 0.0f, 0.0f, 0.0f };
    float gyro_rot_x = 0.0f;
    float gyro_rot_y = 0.0f;
    //Accelerometer: raw proper acceleration (m/s^2, gravity included).
    float latest_accel[3] = { 0.0f, 0.0f, 0.0f };

    //One TouchState per simultaneously-down finger; pointer_count is how many of
    //touch[0..pointer_count-1] are live right now.
    TouchState touch[INPUT_CONTROLLER_MAX_TOUCHES];
    int pointer_count = 0;
    vec2 touch_midpoint = vec2();
    vec2 two_finger_vector = vec2();
    float two_finger_distance = 0.0f;
    float two_finger_distance_delta = 0.0f;

    //Last button (key event) seen. NOT the engine's KeyState -- that name is taken by the
    //mapping machinery above, so this one keeps its own type (see LastKeyEvent).
    LastKeyEvent last_key = { "none", "none", 0 };
    bool disable_gyro = false;  //ignore gyro events; useful for debugging and testing
#endif

#if defined(__ANDROID__)
public:
    //NDK sensor handles, owned by InitSensors/DrainSensorEvents. Opaque here on purpose --
    //see the forward declarations at the top of this file.
    ASensorManager* sensor_manager = NULL;
    const ASensor* gyroscope = NULL;
    const ASensor* accelerometer = NULL;
    ASensorEventQueue* sensor_queue = NULL;
#endif
};

#endif
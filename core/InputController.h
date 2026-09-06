#ifndef _INPUTCONTROLLER_H_
#define _INPUTCONTROLLER_H_
class InputController;
#include <windowsx.h>
#include <windows.h>
#include "stdint.h"
#include <vector>
#include <atomic>
#include <mutex>
#include "Object.h"
#include "type_int2.h"

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
    INPUT_MOUSE_X,
    INPUT_MOUSE_Y,
    INPUT_MOUSE_WHEEL,
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
    INPUT_EVENT_AXIS_RELATIVE   //value is ADDED to the axis (mouse wheel today, raw mouse later)
}inputeventtype_t;

struct InputEvent{
    uint16_t type = INPUT_EVENT_NONE;       //inputeventtype_t
    uint16_t mapped_keycode = INPUT_NONE;   //keycode_t this event is about
    int32_t value = 0;
};

struct KeyState{
    int                     f_isdown = 0;
    bool                    f_was_released = false;
    bool                    f_processed = false;  // If the input was processed
    int32_t                 value = 0;
    float                   fvalue = 0.0f;
    std::atomic<int32_t>    delta = {0};          // Delta value this tick
    int                     num_mappings = 1;   // Amount of keys that are mapped to this state.
    void                    Down();
    void                    Up();
};

struct KeyMap{
    uint32_t system_keycode = 0;   // The system keycode.
    uint32_t mapped_keycode = 0;   // Our keycode
    KeyState* state = NULL;        // The state. Multiple maps can refer to the same state.
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
    void UpdateKeyState();

    //--- The input event stream ---------------------------------------------------------------
    //Callable from ANY thread. This is the only sanctioned way for something outside the physics
    //thread to affect input, and eventually the only way to affect the simulation at all.
    void SubmitEvent(const InputEvent& event);
    //Physics thread only. Drains everything submitted since the last call, applies it to KeyState,
    //and keeps it as this tick's input.
    void ApplyPendingEvents();
    //What ApplyPendingEvents just applied: the exact input this tick ran with. A recorder writes
    //this out; a replay submits it back. Physics thread only, valid until the next call.
    const std::vector<InputEvent>& GetTickEvents() const { return tick_events; }
    //Samples the OS devices and turns them into events. Transitional: it polls GetAsyncKeyState /
    //GetCursorPos, so it re-states each key's LEVEL every tick rather than reporting edges. That
    //is deliberate for now - it reproduces the old polling behaviour exactly, including its
    //multi-mapping quirks - and is what moving to Raw Input replaces with real edges.
    void PollDevices();

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
    std::vector<InputEvent> pending_events; //producers append, the physics thread drains
    std::vector<InputEvent> tick_events;    //physics thread only: this tick's applied input
    WindowInputState window_state;

    //Read on the physics thread every poll, written from the window thread, and only ever a plain
    //flag - atomic is enough and avoids taking the lock in the polling hot path.
    std::atomic<bool> f_has_focus{true};

    int2 mouse_position;    //physics thread only, from the polled cursor position

    std::atomic<objectid_t>hovered_object = {OBJECTID_INVALID};
};

#endif
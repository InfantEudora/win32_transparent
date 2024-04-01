#ifndef _INPUTCONTROLLER_H_
#define _INPUTCONTROLLER_H_
class InputController;
#include <windowsx.h>
#include <windows.h>
#include "stdint.h"
#include <vector>
#include <atomic>
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
}keycode_t;

struct KeyState{
    bool                    f_isdown = false;
    bool                    f_was_released = false;
    int32_t                 value = 0;
    float                   fvalue = 0.0f;
    std::atomic<int32_t>    delta = 0;          // Delta value this tick
    int                     num_mappings = 1;   // Amount of keys that are mapped to this state.
    void                    Down();
    void                    Up();
};

struct KeyMap{
    uint32_t system_keycode = 0;   // The system keycode.
    uint32_t mapped_keycode = 0;   // Our keycode
    KeyState* state = NULL;        // The state. Multiple maps can refer to the same state.
};

class InputController{
    public:
    InputController();

    //Can be called from any thread
    void UpdateKeyState();
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

    //There needs to be at least some form of feedback from UI which object was selected/hovered.


    //Mouse position is also stored in keymap, and seperately
    std::vector<KeyMap>keymap;

protected:
    int2 mouse_position;
    int2 window_position;   //Stored here seperately

    std::atomic<objectid_t>hovered_object = OBJECTID_INVALID;

};

#endif
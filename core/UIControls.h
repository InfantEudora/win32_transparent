#pragma once
/*
    THE CONTROLS SCREEN, generic over an app's input map. docs/menu_plan.md, choice 5.

    The app names its ROWS - a label and the actions it covers ("Run" is LEFT, RIGHT and the
    stick's MOVE) - and nothing else. Every binding shown comes from InputController::keymap, the
    map the game actually reads, sorted into keyboard, mouse and gamepad columns. So the screen
    cannot drift from the game: a key added in SetupInput appears here by itself.

    LIVE: each binding is a keycap that lights while THAT key is held (KeyMap::f_held is per
    physical key, so pressing Left lights Left and not A, though both run). An analog binding -
    a stick or a trigger - carries a bar showing its deflection, from GetAxis. Under the table, a
    strip shows the pad itself: connected or not, every button (bound or not, from
    GetGamepadButtons) and all six axes, which answers "is my pad working" for buttons the game
    never uses. Scripted input (archer_hold, a replay) lights what it drives too, which is how the
    screen is checked without a person at the pad.

    REBINDING IS NOT BUILT, but the shape allows it: a row resolves to keymap INDICES (Bindings), so
    a later rebind is "pick a cell, wait for a key, write keymap[i].system_keycode, save it to the
    settings" without changing what is drawn or how.

    RENDER THREAD (Draw). It reads the keymap's live state without a lock: f_held and the axis
    values are written by the physics thread, and a frame's view of them being a pass stale is
    exactly as wrong as the screen is allowed to be.
*/
#include <cstdint>
#include <string>
#include <vector>

class UIOverlay;
class InputController;

enum UIControlsDevice{
    UI_DEVICE_KEYBOARD,
    UI_DEVICE_MOUSE,
    UI_DEVICE_GAMEPAD,
    UI_DEVICE_NONE,             //a mapping with no hardware to name - a mouse axis
    //(an on-screen touch rect counts as the MOUSE: on a desktop the mouse is what presses it)
};

struct UIControlsStyle{
    uint32_t text = 0xFFCCE8F2;         //AABBGGRR, as UIColor packs it
    uint32_t text_dim = 0x99CCE8F2;
    uint32_t header = 0xFF8FC4A0;
    uint32_t band = 0xC0100E08;         //behind the whole table, so busy art cannot eat it
    uint32_t cap = 0x40CCE8F2;          //a keycap, at rest
    uint32_t cap_lit = 0xFF50C496;      //...and held
    uint32_t cap_text_lit = 0xFF100E08;
    uint32_t bar = 0xFF50C496;
};

class UIControls{
public:
    struct Row{
        std::string label;
        std::vector<uint32_t> actions;
    };
    void AddRow(const char* label, const std::vector<uint32_t>& actions);
    const std::vector<Row>& Rows() const { return rows; }

    //The keymap indices behind a row on one device, in map order.
    static std::vector<int> Bindings(const InputController* input, const Row& row, int device);
    //Which column a mapping belongs in, and what its keycap says ("Space", "A", "L stick X", "Left").
    static int         DeviceOf(uint32_t system_keycode, int analog_index);
    static std::string KeyName(uint32_t system_keycode, int analog_index);

    //The table in min..max (pixels, the overlay's space), and the pad strip along its bottom.
    void Draw(UIOverlay* overlay, InputController* input, float x0, float y0, float x1, float y1,
              const UIControlsStyle& style) const;

private:
    std::vector<Row> rows;
};

#pragma once
/*
    A MENU: a vertical list of items - buttons, choices, sliders, toggles - driven by the mouse, the
    keyboard or a pad, and drawn on UIOverlay. docs/menu_plan.md has the why.

    TWO HALVES, ON TWO THREADS:

      Update(UIMenuInput)   the logic, from whichever thread has the input - archer's physics
                            thread, in UpdateTitle. It moves the focus, changes values and says
                            what happened: an item ACTIVATED (a button), CHANGED (a value), or
                            BACK. Nothing here reads a key: the APP fills UIMenuInput from its own
                            actions, so a pad-only game, a touch game and an MCP tool are just
                            different fillers.
      UIMenuDraw(...)       the look, on the render thread (UIMenuDraw.cpp, the only half that
                            needs GL), from a copy of the items taken under the menu's lock.

    THE LAYOUT IS A PURE FUNCTION OF THE SCREEN SIZE (UIMenuLayout), and both halves compute it,
    so what is clicked is always what was drawn. Sizes are fractions of the screen HEIGHT, so the
    menu keeps its shape at any window size and aspect.

    NAVIGATION, the usual rules:
      - up/down move the focus, wrapping, skipping disabled items;
      - left/right step the focused value - a choice clamps at its ends, a slider by `step`;
      - confirm activates a button, steps a choice round (wrapping) and flips a toggle;
      - back is reported, never acted on: what Back means is the app's.
    THE POINTER:
      - hovering moves the focus, but only when the pointer MOVES, so a cursor left resting over
        one button does not fight the keyboard;
      - a button activates on RELEASE over the item it was pressed on, so a press dragged off
        cancels it;
      - a slider follows the pointer from the press until the release;
      - a choice steps back when clicked on the left half of its value and on otherwise;
      - `f_pointer` false (no focus, say) ignores the pointer entirely.

    The logic is engine-free: tools/menu_test.cpp drives it with no window.
*/
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

enum UIMenuKind{
    UI_MENU_BUTTON,
    UI_MENU_CHOICE,
    UI_MENU_SLIDER,
    UI_MENU_TOGGLE,
};

struct UIMenuItem{
    int         kind = UI_MENU_BUTTON;
    std::string label;
    int         id = 0;                 //the app's own tag, handed back in UIMenuResult
    bool        f_enabled = true;       //disabled: drawn dim, skipped by up/down, deaf to clicks
    std::vector<std::string> choices;   //UI_MENU_CHOICE
    int         choice = 0;
    float       value = 0.0f;           //UI_MENU_SLIDER, shown as a percentage of lo..hi
    float       lo = 0.0f, hi = 1.0f, step = 0.1f;
    bool        f_on = false;           //UI_MENU_TOGGLE
};

//One pass of input, in the overlay's space: pixels, top-left origin, the window's client area.
struct UIMenuInput{
    float screen_w = 0.0f, screen_h = 0.0f;
    bool  up = false, down = false, left = false, right = false;
    bool  confirm = false, back = false;
    bool  f_pointer = false;            //the pointer may be used this pass
    float pointer_x = 0.0f, pointer_y = 0.0f;
    bool  pointer_pressed = false, pointer_released = false, pointer_down = false;
};

enum UIMenuEvent{
    UI_MENU_NONE,
    UI_MENU_ACTIVATED,                  //a button, pressed
    UI_MENU_CHANGED,                    //a value, changed - read it back with Item()/Value()...
    UI_MENU_BACK,
};

struct UIMenuResult{
    int event = UI_MENU_NONE;
    int item = -1;
    int id = 0;
};

//Where the column of items sits. All fractions of the screen HEIGHT except centre_x (of the width).
struct UIMenuLayout{
    float centre_x = 0.5f;
    float top = 0.55f;                  //the first item's top edge
    float width = 0.44f;
    float item_h = 0.058f;
    float gap = 0.012f;
};

struct UIMenuRect{
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    bool Contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

class UIMenu{
public:
    UIMenuLayout layout;

    //Each returns the item's index.
    int  AddButton(const char* label, int id = 0);
    int  AddChoice(const char* label, const std::vector<std::string>& choices, int current, int id = 0);
    int  AddSlider(const char* label, float value, float lo, float hi, float step, int id = 0);
    int  AddToggle(const char* label, bool f_on, int id = 0);
    void Clear();

    UIMenuResult Update(const UIMenuInput& in);

    int  Count() const;
    int  Focus() const;
    void SetFocus(int item);            //an out-of-range or disabled item is ignored
    int  Pressed() const;               //the item the pointer is holding down, -1 for none
    UIMenuItem Item(int item) const;    //a copy - safe from any thread
    std::vector<UIMenuItem> Items() const;

    void SetLabel(int item, const char* label);
    void SetEnabled(int item, bool f_enabled);
    void SetChoice(int item, int choice);
    void SetValue(int item, float value);
    void SetOn(int item, bool f_on);

    //The layout, for both halves. Pure: no lock, nothing stored.
    UIMenuRect ItemRect(int item, float screen_w, float screen_h) const;
    //The part of a slider or choice row that holds its value - the slider's track, the choice's
    //"< value >" - which is where a click on that row means something.
    UIMenuRect ValueRect(int item, float screen_w, float screen_h) const;
    //A slider's track within its ValueRect, leaving room on the right for "100%" - what a click
    //maps onto and what is drawn, the one rect for both.
    UIMenuRect TrackRect(int item, float screen_w, float screen_h) const;

private:
    int  Add(const UIMenuItem& item);
    int  HitLocked(float x, float y, float w, float h) const;
    int  StepFocusLocked(int from, int dir) const;
    bool StepValueLocked(int item, int dir, bool f_wrap);
    float SliderAtLocked(int item, float x, float w, float h) const;

    mutable std::mutex mutex;
    std::vector<UIMenuItem> items;
    int   focus = 0;
    int   pressed = -1;
    float last_px = -1e9f, last_py = -1e9f;
};

/*
    THE LOOK. Colours are the app's; sizes follow the layout. Drawn by UIMenuDraw (UIMenuDraw.cpp),
    RENDER THREAD, between the overlay's Begin and Draw like any other overlay content.
*/
class UIOverlay;
struct UIMenuStyle{
    uint32_t panel = 0x96100E08;            //AABBGGRR, as UIColor packs it
    uint32_t panel_focus = 0xDC1C2A20;
    uint32_t panel_pressed = 0xF0101A14;
    uint32_t outline_focus = 0xFFCCE8F2;
    uint32_t text = 0xFFCCE8F2;
    uint32_t text_dim = 0x80CCE8F2;
    uint32_t track = 0x40CCE8F2;
    uint32_t fill = 0xEB50C496;
    float    text_scale = 0.46f;            //of the item's height
};
void UIMenuDraw(UIOverlay* overlay, const UIMenu& menu, float screen_w, float screen_h, const UIMenuStyle& style);

#include "UIControls.h"

#include <algorithm>
#include <cmath>
#include "InputController.h"
#include "InputRecording.h"     //FormatSystemKey, the fallback name for a key the table below misses
#include "UIOverlay.h"

void UIControls::AddRow(const char* label, const std::vector<uint32_t>& actions){
    Row r;
    r.label = label;
    r.actions = actions;
    rows.push_back(r);
}

int UIControls::DeviceOf(uint32_t code, int analog_index){
    if (analog_index >= 0){
        return UI_DEVICE_GAMEPAD;
    }
    //An on-screen rect (AddTouchButton): on a desktop it is the mouse that presses it.
    if (code >= TOUCH_SYSKEY_BASE){
        return UI_DEVICE_MOUSE;
    }
    if (code == 0){
        return UI_DEVICE_NONE;
    }
    if (code >= GAMEPAD_SYSKEY_BASE){
        return UI_DEVICE_GAMEPAD;
    }
#if defined(_WIN32)
    if (code == VK_LBUTTON || code == VK_RBUTTON || code == VK_MBUTTON || code == VK_XBUTTON1 || code == VK_XBUTTON2){
        return UI_DEVICE_MOUSE;
    }
#endif
    return UI_DEVICE_KEYBOARD;
}

struct UIKeyName{
    uint32_t    code;
    const char* name;
};

std::string UIControls::KeyName(uint32_t code, int analog_index){
    static const char* axes[] = { "L stick X", "L stick Y", "R stick X", "R stick Y", "LT", "RT" };
    if (analog_index >= 0){
        return (analog_index < 6) ? axes[analog_index] : "Axis";
    }
    if (code >= TOUCH_SYSKEY_BASE){
        return "Click";
    }
    //Short, and by the names printed on the hardware: the column says which device it is.
    static const UIKeyName names[] = {
        {GAMEPAD_KEY_A,"A"}, {GAMEPAD_KEY_B,"B"}, {GAMEPAD_KEY_X,"X"}, {GAMEPAD_KEY_Y,"Y"},
        {GAMEPAD_KEY_LEFT_SHOULDER,"LB"}, {GAMEPAD_KEY_RIGHT_SHOULDER,"RB"},
        {GAMEPAD_KEY_BACK,"Back"}, {GAMEPAD_KEY_START,"Start"},
        {GAMEPAD_KEY_LEFT_THUMB,"LS"}, {GAMEPAD_KEY_RIGHT_THUMB,"RS"},
        {GAMEPAD_KEY_DPAD_UP,"D-pad Up"}, {GAMEPAD_KEY_DPAD_DOWN,"D-pad Down"},
        {GAMEPAD_KEY_DPAD_LEFT,"D-pad Left"}, {GAMEPAD_KEY_DPAD_RIGHT,"D-pad Right"},
#if defined(_WIN32)
        {VK_LBUTTON,"Left"}, {VK_RBUTTON,"Right"}, {VK_MBUTTON,"Middle"},
        {VK_XBUTTON1,"Button 4"}, {VK_XBUTTON2,"Button 5"},
        {VK_SPACE,"Space"}, {VK_ESCAPE,"Esc"}, {VK_RETURN,"Enter"}, {VK_TAB,"Tab"},
        {VK_BACK,"Backspace"}, {VK_LEFT,"Left"}, {VK_RIGHT,"Right"}, {VK_UP,"Up"}, {VK_DOWN,"Down"},
        {VK_HOME,"Home"}, {VK_END,"End"}, {VK_PRIOR,"PgUp"}, {VK_NEXT,"PgDn"},
        {VK_INSERT,"Ins"}, {VK_DELETE,"Del"}, {VK_PAUSE,"Pause"},
        {VK_SHIFT,"Shift"}, {VK_LSHIFT,"L Shift"}, {VK_RSHIFT,"R Shift"},
        {VK_CONTROL,"Ctrl"}, {VK_LCONTROL,"L Ctrl"}, {VK_RCONTROL,"R Ctrl"},
#endif
    };
    for (const UIKeyName& k : names){
        if (k.code == code){
            return k.name;
        }
    }
    return FormatSystemKey(code);
}

std::vector<int> UIControls::Bindings(const InputController* input, const Row& row, int device){
    std::vector<int> out;
    if (!input){
        return out;
    }
    for (int i = 0; i < (int)input->keymap.size(); i++){
        const KeyMap& km = input->keymap[i];
        if (std::find(row.actions.begin(),row.actions.end(),km.mapped_keycode) == row.actions.end()){
            continue;
        }
        if (DeviceOf(km.system_keycode,km.analog_index) == device){
            out.push_back(i);
        }
    }
    return out;
}

/*
    One keycap: the name in a rounded box, lit while held; an analog one also carries its
    deflection as a bar along its bottom, from the middle out for a stick, from the left for a
    trigger. Returns the cap's width, for the next one to sit beside it.
*/
static float DrawCap(UIOverlay* o, const char* name, float x, float cy, float ts, bool f_lit, int analog_index,
                     float deflection, const UIControlsStyle& style){
    const float pad = ts * 0.45f;
    const float cw = o->MeasureText(name,ts).x + pad * 2.0f;
    const float ch = ts * 1.45f;
    const vec2 a(x,cy - ch * 0.5f), b(x + cw,cy + ch * 0.5f);
    o->AddRect(a,b,ts * 0.3f,f_lit ? style.cap_lit : style.cap);
    o->AddText(name,vec2(x + pad,cy + ts * 0.36f),ts,f_lit ? style.cap_text_lit : style.text,UI_ALIGN_LEFT);
    if (analog_index >= 0){
        const float by = b.y - ts * 0.18f, bh = std::max(2.0f,ts * 0.14f);
        const float bx0 = x + pad * 0.5f, bx1 = x + cw - pad * 0.5f;
        const bool f_trigger = analog_index >= 4;
        const float from = f_trigger ? bx0 : (bx0 + bx1) * 0.5f;
        const float d = std::max(-1.0f,std::min(deflection,1.0f));
        const float to = f_trigger ? bx0 + (bx1 - bx0) * std::fabs(d) : from + (bx1 - bx0) * 0.5f * d;
        if (std::fabs(to - from) > 0.5f){
            //On a lit cap the bar takes the text's dark colour: in the bar's own it vanished into the cap.
            o->AddRect(vec2(std::min(from,to),by - bh * 0.5f),vec2(std::max(from,to),by + bh * 0.5f),bh * 0.5f,
                       f_lit ? style.cap_text_lit : style.bar);
        }
    }
    return cw;
}

void UIControls::Draw(UIOverlay* o, InputController* input, float x0, float y0, float x1, float y1,
                      const UIControlsStyle& style) const{
    if (!o || !input || x1 <= x0 || y1 <= y0){
        return;
    }
    //Rows, a header, and two lines of pad strip under a gap.
    const int lines = (int)rows.size() + 1 + 3;
    const float lh = (y1 - y0) / (float)lines;
    const float ts = std::min(lh * 0.48f,40.0f);
    const float pad = ts * 0.8f;
    o->AddRect(vec2(x0,y0),vec2(x1,y1),ts * 0.6f,style.band);

    const float wtot = x1 - x0 - pad * 2.0f;
    //Action, keyboard, mouse, gamepad. The mouse has least to show and the pad's names are longest.
    const float col[4] = { x0 + pad, x0 + pad + wtot * 0.22f, x0 + pad + wtot * 0.52f, x0 + pad + wtot * 0.61f };
    const char* heads[4] = { "ACTION", "KEYBOARD", "MOUSE", "GAMEPAD" };
    float cy = y0 + lh * 0.5f;
    for (int c = 0; c < 4; c++){
        o->AddText(heads[c],vec2(col[c],cy + ts * 0.36f * 0.8f),ts * 0.8f,style.header,UI_ALIGN_LEFT);
    }
    for (const Row& row : rows){
        cy += lh;
        o->AddText(row.label.c_str(),vec2(col[0],cy + ts * 0.36f),ts,style.text,UI_ALIGN_LEFT);
        for (int device = 0; device < 3; device++){
            float x = col[device + 1];
            const float limit = (device < 2) ? col[device + 2] - ts * 0.4f : x1 - pad;
            std::vector<int> bound = Bindings(input,row,device);
            if (bound.empty()){
                o->AddText("-",vec2(x,cy + ts * 0.36f),ts,style.text_dim,UI_ALIGN_LEFT);
                continue;
            }
            for (int i : bound){
                const KeyMap& km = input->keymap[i];
                std::string name = KeyName(km.system_keycode,km.analog_index);
                //A cap that would run past its column is left out rather than overprinting the next.
                if (x + o->MeasureText(name.c_str(),ts).x + ts * 0.9f > limit){
                    o->AddText("...",vec2(x,cy + ts * 0.36f),ts,style.text_dim,UI_ALIGN_LEFT);
                    break;
                }
                float defl = km.IsAnalog() ? input->GetAxis(km.mapped_keycode) : 0.0f;
                bool f_lit = km.IsAnalog() ? std::fabs(defl) > 0.05f : km.f_held;
                x += DrawCap(o,name.c_str(),x,cy,ts,f_lit,km.analog_index,defl,style) + ts * 0.35f;
            }
        }
    }

    //--- The pad itself, bound or not --------------------------------------------------------
    cy += lh * 1.5f;
    const bool f_pad = input->dev_index >= 0;
    o->AddText(f_pad ? "PAD" : "NO PAD",vec2(col[0],cy + ts * 0.36f * 0.8f),ts * 0.8f,f_pad ? style.header : style.text_dim,UI_ALIGN_LEFT);
    static const uint32_t pad_buttons[] = {
        GAMEPAD_KEY_A, GAMEPAD_KEY_B, GAMEPAD_KEY_X, GAMEPAD_KEY_Y, GAMEPAD_KEY_LEFT_SHOULDER, GAMEPAD_KEY_RIGHT_SHOULDER,
        GAMEPAD_KEY_BACK, GAMEPAD_KEY_START, GAMEPAD_KEY_LEFT_THUMB, GAMEPAD_KEY_RIGHT_THUMB,
        GAMEPAD_KEY_DPAD_UP, GAMEPAD_KEY_DPAD_DOWN, GAMEPAD_KEY_DPAD_LEFT, GAMEPAD_KEY_DPAD_RIGHT,
    };
    const uint16_t held = input->GetGamepadButtons();
    const float sts = ts * 0.8f;
    float x = col[1];
    for (uint32_t b : pad_buttons){
        std::string name = KeyName(b,-1);
        if (name.compare(0,6,"D-pad ") == 0){
            name = name.substr(6);      //"Up", under a strip that is all pad
        }
        x += DrawCap(o,name.c_str(),x,cy,sts,(held & (uint16_t)(b - GAMEPAD_SYSKEY_BASE)) != 0,-1,0.0f,style) + sts * 0.3f;
    }
    cy += lh;
    x = col[1];
    for (int a = 0; a < 6; a++){
        //Raw and unscaled, so a stick's rest drift shows: sticks are +-32767, triggers 0..32640.
        float v = (float)input->analog_values[a] / 32767.0f;
        std::string name = KeyName(0,a);
        x += DrawCap(o,name.c_str(),x,cy,sts,std::fabs(v) > 0.25f,a,v,style) + sts * 0.3f;
    }
}

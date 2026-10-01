#include "UIMenu.h"

#include <algorithm>
#include <cmath>

//The logic half of UIMenu - see UIMenu.h. No GL and no engine: tools/menu_test.cpp links this alone.

int UIMenu::Add(const UIMenuItem& item){
    std::lock_guard<std::mutex> lock(mutex);
    items.push_back(item);
    return (int)items.size() - 1;
}

int UIMenu::AddButton(const char* label, int id){
    UIMenuItem it;
    it.kind = UI_MENU_BUTTON;
    it.label = label;
    it.id = id;
    return Add(it);
}

int UIMenu::AddChoice(const char* label, const std::vector<std::string>& choices, int current, int id){
    UIMenuItem it;
    it.kind = UI_MENU_CHOICE;
    it.label = label;
    it.id = id;
    it.choices = choices;
    it.choice = choices.empty() ? 0 : std::max(0,std::min(current,(int)choices.size() - 1));
    return Add(it);
}

int UIMenu::AddSlider(const char* label, float value, float lo, float hi, float step, int id){
    UIMenuItem it;
    it.kind = UI_MENU_SLIDER;
    it.label = label;
    it.id = id;
    it.lo = lo;
    it.hi = (hi > lo) ? hi : lo + 1.0f;
    it.step = (step > 0.0f) ? step : (it.hi - it.lo) * 0.1f;
    it.value = std::max(it.lo,std::min(value,it.hi));
    return Add(it);
}

int UIMenu::AddToggle(const char* label, bool f_on, int id){
    UIMenuItem it;
    it.kind = UI_MENU_TOGGLE;
    it.label = label;
    it.id = id;
    it.f_on = f_on;
    return Add(it);
}

void UIMenu::Clear(){
    std::lock_guard<std::mutex> lock(mutex);
    items.clear();
    focus = 0;
    pressed = -1;
}

UIMenuRect UIMenu::ItemRect(int item, float w, float h) const{
    UIMenuRect r;
    const float iw = layout.width * h, ih = layout.item_h * h;
    r.x0 = layout.centre_x * w - iw * 0.5f;
    r.x1 = r.x0 + iw;
    r.y0 = layout.top * h + (float)item * (ih + layout.gap * h);
    r.y1 = r.y0 + ih;
    return r;
}

//The right half of the row, less a margin: the label has the left.
UIMenuRect UIMenu::ValueRect(int item, float w, float h) const{
    UIMenuRect r = ItemRect(item,w,h);
    const float pad = (r.y1 - r.y0) * 0.4f;
    r.x0 = r.x0 + (r.x1 - r.x0) * 0.5f;
    r.x1 = r.x1 - pad;
    return r;
}

UIMenuRect UIMenu::TrackRect(int item, float w, float h) const{
    UIMenuRect r = ValueRect(item,w,h);
    const float ih = layout.item_h * h;
    r.x1 = std::max(r.x0 + 1.0f,r.x1 - ih * 1.6f);
    return r;
}

int UIMenu::HitLocked(float x, float y, float w, float h) const{
    for (int i = 0; i < (int)items.size(); i++){
        if (items[i].f_enabled && ItemRect(i,w,h).Contains(x,y)){
            return i;
        }
    }
    return -1;
}

int UIMenu::StepFocusLocked(int from, int dir) const{
    const int n = (int)items.size();
    for (int k = 1; k <= n; k++){
        int i = ((from + dir * k) % n + n) % n;
        if (items[i].f_enabled){
            return i;
        }
    }
    return from;
}

bool UIMenu::StepValueLocked(int item, int dir, bool f_wrap){
    UIMenuItem& it = items[item];
    switch (it.kind){
        case UI_MENU_CHOICE:{
            const int n = (int)it.choices.size();
            if (n < 2){
                return false;
            }
            int c = it.choice + dir;
            c = f_wrap ? ((c % n + n) % n) : std::max(0,std::min(c,n - 1));
            if (c == it.choice){
                return false;
            }
            it.choice = c;
            return true;
        }
        case UI_MENU_SLIDER:{
            //Stepped on a grid from lo, so ten presses from 0 land on exactly 1 rather than 0.9999.
            float steps = std::round((it.value - it.lo) / it.step) + (float)dir;
            float v = std::max(it.lo,std::min(it.lo + steps * it.step,it.hi));
            if (v == it.value){
                return false;
            }
            it.value = v;
            return true;
        }
        case UI_MENU_TOGGLE:
            it.f_on = !it.f_on;
            return true;
    }
    return false;
}

float UIMenu::SliderAtLocked(int item, float x, float w, float h) const{
    const UIMenuItem& it = items[item];
    UIMenuRect r = TrackRect(item,w,h);
    float t = (r.x1 > r.x0) ? (x - r.x0) / (r.x1 - r.x0) : 0.0f;
    t = std::max(0.0f,std::min(t,1.0f));
    //Snapped to the step, as the keys are, so the mouse cannot leave 0.73128 in the settings file.
    float v = it.lo + std::round(t * (it.hi - it.lo) / it.step) * it.step;
    return std::max(it.lo,std::min(v,it.hi));
}

UIMenuResult UIMenu::Update(const UIMenuInput& in){
    std::lock_guard<std::mutex> lock(mutex);
    UIMenuResult out;
    const int n = (int)items.size();
    if (n == 0){
        return out;
    }
    if (focus < 0 || focus >= n || !items[focus].f_enabled){
        focus = StepFocusLocked(std::max(0,std::min(focus,n - 1)),1);
    }
    auto result = [this](int event, int item){
        UIMenuResult r;
        r.event = event;
        r.item = item;
        r.id = (item >= 0 && item < (int)items.size()) ? items[item].id : 0;
        return r;
    };

    //--- The pointer: hover, then press, drag and release -------------------------------------
    if (in.f_pointer && in.screen_w > 0.0f && in.screen_h > 0.0f){
        const float w = in.screen_w, h = in.screen_h;
        int hit = HitLocked(in.pointer_x,in.pointer_y,w,h);
        bool f_moved = in.pointer_x != last_px || in.pointer_y != last_py;
        last_px = in.pointer_x;
        last_py = in.pointer_y;
        if (f_moved && hit >= 0 && pressed < 0){
            focus = hit;
        }
        if (in.pointer_pressed && hit >= 0){
            pressed = hit;
            focus = hit;
            if (items[hit].kind == UI_MENU_SLIDER){
                float v = SliderAtLocked(hit,in.pointer_x,w,h);
                if (v != items[hit].value){
                    items[hit].value = v;
                    out = result(UI_MENU_CHANGED,hit);
                }
            }
        }else if (pressed >= 0 && pressed < n && items[pressed].kind == UI_MENU_SLIDER &&
                  (in.pointer_down || in.pointer_released)){
            //Dragging: the slider follows the pointer even off its row, until the release.
            float v = SliderAtLocked(pressed,in.pointer_x,w,h);
            if (v != items[pressed].value){
                items[pressed].value = v;
                out = result(UI_MENU_CHANGED,pressed);
            }
        }
        if (in.pointer_released){
            int was = pressed;
            pressed = -1;
            if (was >= 0 && was == hit){
                UIMenuItem& it = items[was];
                if (it.kind == UI_MENU_BUTTON){
                    return result(UI_MENU_ACTIVATED,was);
                }
                if (it.kind == UI_MENU_TOGGLE){
                    it.f_on = !it.f_on;
                    return result(UI_MENU_CHANGED,was);
                }
                if (it.kind == UI_MENU_CHOICE){
                    UIMenuRect r = ValueRect(was,w,h);
                    int dir = (in.pointer_x < (r.x0 + r.x1) * 0.5f) ? -1 : 1;
                    if (StepValueLocked(was,dir,true)){
                        return result(UI_MENU_CHANGED,was);
                    }
                }
            }
        }
        if (out.event != UI_MENU_NONE){
            return out;
        }
    }else if (in.pointer_released){
        pressed = -1;
    }

    //--- Keys and pad --------------------------------------------------------------------------
    if (in.back){
        return result(UI_MENU_BACK,focus);
    }
    if (in.up && !in.down){
        focus = StepFocusLocked(focus,-1);
    }else if (in.down && !in.up){
        focus = StepFocusLocked(focus,1);
    }
    if ((in.left != in.right) && items[focus].kind != UI_MENU_BUTTON){
        if (StepValueLocked(focus,in.right ? 1 : -1,false)){
            return result(UI_MENU_CHANGED,focus);
        }
    }
    if (in.confirm){
        UIMenuItem& it = items[focus];
        if (it.kind == UI_MENU_BUTTON){
            return result(UI_MENU_ACTIVATED,focus);
        }
        if (it.kind != UI_MENU_SLIDER && StepValueLocked(focus,1,true)){
            return result(UI_MENU_CHANGED,focus);
        }
    }
    return out;
}

int UIMenu::Count() const{
    std::lock_guard<std::mutex> lock(mutex);
    return (int)items.size();
}

int UIMenu::Focus() const{
    std::lock_guard<std::mutex> lock(mutex);
    return focus;
}

void UIMenu::SetFocus(int item){
    std::lock_guard<std::mutex> lock(mutex);
    if (item >= 0 && item < (int)items.size() && items[item].f_enabled){
        focus = item;
    }
}

int UIMenu::Pressed() const{
    std::lock_guard<std::mutex> lock(mutex);
    return pressed;
}

UIMenuItem UIMenu::Item(int item) const{
    std::lock_guard<std::mutex> lock(mutex);
    return (item >= 0 && item < (int)items.size()) ? items[item] : UIMenuItem();
}

std::vector<UIMenuItem> UIMenu::Items() const{
    std::lock_guard<std::mutex> lock(mutex);
    return items;
}

void UIMenu::SetLabel(int item, const char* label){
    std::lock_guard<std::mutex> lock(mutex);
    if (item >= 0 && item < (int)items.size()){
        items[item].label = label;
    }
}

void UIMenu::SetEnabled(int item, bool f_enabled){
    std::lock_guard<std::mutex> lock(mutex);
    if (item >= 0 && item < (int)items.size()){
        items[item].f_enabled = f_enabled;
    }
}

void UIMenu::SetChoice(int item, int choice){
    std::lock_guard<std::mutex> lock(mutex);
    if (item >= 0 && item < (int)items.size() && !items[item].choices.empty()){
        items[item].choice = std::max(0,std::min(choice,(int)items[item].choices.size() - 1));
    }
}

void UIMenu::SetValue(int item, float value){
    std::lock_guard<std::mutex> lock(mutex);
    if (item >= 0 && item < (int)items.size()){
        UIMenuItem& it = items[item];
        it.value = std::max(it.lo,std::min(value,it.hi));
    }
}

void UIMenu::SetOn(int item, bool f_on){
    std::lock_guard<std::mutex> lock(mutex);
    if (item >= 0 && item < (int)items.size()){
        items[item].f_on = f_on;
    }
}

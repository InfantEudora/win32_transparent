#include "UIMenu.h"
#include "UIOverlay.h"

#include <cmath>
#include <cstdio>

/*
    The look half of UIMenu - see UIMenu.h. RENDER THREAD. Rounded panels, text and, for a slider,
    a track with its fill: nothing the overlay does not already draw (docs/ui_overlay_plan.md).

    Text sits on its baseline at the row's centre plus 0.36 of its size, which centres the
    capitals of the overlay's font in the row (apps/bomber's DrawMenuButton found the number).
*/
void UIMenuDraw(UIOverlay* overlay, const UIMenu& menu, float w, float h, const UIMenuStyle& style){
    if (!overlay || w <= 0.0f || h <= 0.0f){
        return;
    }
    const std::vector<UIMenuItem> items = menu.Items();
    const int focus = menu.Focus();
    const int pressed = menu.Pressed();
    for (int i = 0; i < (int)items.size(); i++){
        const UIMenuItem& it = items[i];
        const UIMenuRect r = menu.ItemRect(i,w,h);
        const float ih = r.y1 - r.y0;
        const float radius = ih * 0.3f;
        const float ts = ih * style.text_scale;
        const float pad = ih * 0.4f;
        const float baseline = (r.y0 + r.y1) * 0.5f + ts * 0.36f;
        const bool f_focus = (i == focus) && it.f_enabled;
        const uint32_t fill = (i == pressed) ? style.panel_pressed : (f_focus ? style.panel_focus : style.panel);
        const uint32_t text = it.f_enabled ? style.text : style.text_dim;
        overlay->AddRect(vec2(r.x0,r.y0),vec2(r.x1,r.y1),radius,fill);
        if (f_focus){
            overlay->AddRectOutline(vec2(r.x0,r.y0),vec2(r.x1,r.y1),radius,std::max(1.5f,ih * 0.04f),style.outline_focus);
        }
        if (it.kind == UI_MENU_BUTTON){
            overlay->AddText(it.label.c_str(),vec2((r.x0 + r.x1) * 0.5f,baseline),ts,text,UI_ALIGN_CENTER);
            continue;
        }
        overlay->AddText(it.label.c_str(),vec2(r.x0 + pad,baseline),ts,text,UI_ALIGN_LEFT);
        const UIMenuRect v = menu.ValueRect(i,w,h);
        const float vc = (v.x0 + v.x1) * 0.5f;
        if (it.kind == UI_MENU_CHOICE){
            const char* value = it.choices.empty() ? "" : it.choices[it.choice].c_str();
            overlay->AddText(value,vec2(vc,baseline),ts,text,UI_ALIGN_CENTER);
            //The arrows dim at the ends a step cannot pass (the keys clamp there; a click wraps).
            const bool f_first = it.choice <= 0, f_last = it.choice >= (int)it.choices.size() - 1;
            overlay->AddText("<",vec2(v.x0,baseline),ts,f_first ? style.text_dim : text,UI_ALIGN_LEFT);
            overlay->AddText(">",vec2(v.x1,baseline),ts,f_last ? style.text_dim : text,UI_ALIGN_RIGHT);
        }else if (it.kind == UI_MENU_TOGGLE){
            overlay->AddText(it.f_on ? "On" : "Off",vec2(vc,baseline),ts,text,UI_ALIGN_CENTER);
        }else if (it.kind == UI_MENU_SLIDER){
            //The track is TrackRect, the same rect a click maps onto; the percentage is right of it.
            char pct[16];
            const float t = (it.hi > it.lo) ? (it.value - it.lo) / (it.hi - it.lo) : 0.0f;
            snprintf(pct,sizeof(pct),"%d%%",(int)std::lround(t * 100.0f));
            const UIMenuRect track = menu.TrackRect(i,w,h);
            const float tx0 = track.x0, tx1 = track.x1;
            const float th = std::max(3.0f,ih * 0.16f);
            const float cy = (r.y0 + r.y1) * 0.5f;
            overlay->AddRect(vec2(tx0,cy - th * 0.5f),vec2(tx1,cy + th * 0.5f),th * 0.5f,style.track);
            if (t > 0.0f){
                overlay->AddRect(vec2(tx0,cy - th * 0.5f),vec2(tx0 + (tx1 - tx0) * t,cy + th * 0.5f),th * 0.5f,style.fill);
            }
            //The knob, so a value of 0 still shows where the slider is.
            const float kr = th * 1.3f;
            const float kx = tx0 + (tx1 - tx0) * t;
            overlay->AddRect(vec2(kx - kr,cy - kr),vec2(kx + kr,cy + kr),kr,text);
            overlay->AddText(pct,vec2(v.x1,baseline),ts,text,UI_ALIGN_RIGHT);
        }
    }
}

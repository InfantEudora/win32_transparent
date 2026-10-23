#include "ChasmHud.h"
#include "ChasmVersion.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <stdio.h>

/*
    The goods' colours and the goods strip (ChasmHud.h). Laid out at s = 1 for a window 810 high, as the
    cards are: the sizes below are the card's own - its text, its line height - so a strip sits among
    the card's lines as one of them.
*/

float HudUiScale(float window_h, float ui_size){
    float k = std::max(1.0f,window_h) / HUD_UI_HEIGHT;
    return std::max(0.5f,sqrtf(k) * ui_size);
}

float HudMenuScale(float window_h, float ui_size){
    return HudUiScale(window_h,ui_size) * HUD_UI_HEIGHT / std::max(1.0f,window_h);
}

float DrawHudBuild(UIOverlay* o, float screen_w, float screen_h, float s){
    std::string text = std::string("CHASM ") + ChasmVersion() + "   -   " + ChasmGitHash() + "   -   " + ChasmBuildDate();
#ifdef DEBUG
    text += "   -   DEBUG BUILD";
#endif
    const float size = 15.0f * s;
    vec2 m = o->MeasureText(text.c_str(),size);
    vec2 p1(screen_w - 16.0f * s,screen_h - 14.0f * s);
    vec2 p0(p1.x - m.x - 28.0f * s,p1.y - 30.0f * s);
    o->AddRect(p0,p1,9.0f * s,HUD_PANEL);
    o->AddText(text.c_str(),vec2(p1.x - 14.0f * s,p0.y + 20.5f * s),size,HUD_TEXT,UI_ALIGN_RIGHT);
    return p0.y;
}

uint32_t HudGoodColour(int good){
    switch (good){
        case GOOD_WOOD:         return UIColor(206,160,110);    //the top bar's WOOD
        case GOOD_WATER:        return UIColor(140,190,240);    //...its WATER
        case GOOD_WHEAT:        return UIColor(236,202,92);     //...its FOOD
        case GOOD_GREENS:       return UIColor(150,206,104);
        case GOOD_BEANS:        return UIColor(200,132,168);
        case GOOD_PROVISIONS:   return UIColor(226,212,166);    //hardtack: paler than wheat, lighter than wood
        case GOOD_FRUIT:        return UIColor(226,104,84);     //berries: a red, apart from the beans' mauve (foraging_plan.md)
        //Materials browns and greys, inputs a family of their own (goods_plan.md): rope a greyer tan than wood,
        //hemp a sage green, apart from the greens' yellow-green. Stone a cool grey; floatstone the purple-blue of its
        //crystal (stone_plan.md, the user's), nothing like the stone it is dug from. Steam a warm white, the
        //canisters' cloud (steam_plan.md), paler than any food; canvas the grey-buff of undyed sailcloth, between
        //rope's tan and steam's white.
        case GOOD_ROPE:         return UIColor(176,160,128);
        case GOOD_CANVAS:       return UIColor(214,200,164);
        case GOOD_STONE:        return UIColor(176,182,190);
        case GOOD_FLOATSTONE:   return UIColor(146,128,236);
        case GOOD_HEMP:         return UIColor(108,170,150);
        case GOOD_STEAM:        return UIColor(232,226,214);
        default:                return UIColor(170,170,164);
    }
}

std::array<int,GOOD_COUNT> HudGoodsOrder(){
    //By category (goods_plan.md) - materials, the foods, water, inputs - each in the goods' own order: wood
    //first as ever, and rope beside it.
    static const int categories[GOOD_CATEGORY_COUNT] = {GOOD_CATEGORY_MATERIALS,GOOD_CATEGORY_FOOD,GOOD_CATEGORY_WATER,
                                                        GOOD_CATEGORY_INPUTS};
    std::array<int,GOOD_COUNT> order = {};
    int n = 0;
    for (int c : categories){
        for (int g = 0; g < GOOD_COUNT; g++){
            if (GoodCategory(g) == c){
                order[n++] = g;
            }
        }
    }
    return order;
}

int HudGoodsGroup(int good){
    switch (GoodCategory(good)){
        case GOOD_CATEGORY_MATERIALS:   return HUD_GROUP_MATERIALS;
        case GOOD_CATEGORY_FOOD:
        case GOOD_CATEGORY_WATER:       return HUD_GROUP_FOOD_WATER;
        default:                        return HUD_GROUP_INPUTS;
    }
}

const char* HudGoodsGroupName(int group){
    switch (group){
        case HUD_GROUP_MATERIALS:   return "Materials";
        case HUD_GROUP_FOOD_WATER:  return "Food and water";
        default:                    return "Inputs";
    }
}

GoodsMask HudGoodsGroupMask(int group){
    GoodsMask mask = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        mask |= (HudGoodsGroup(g) == group) ? (GoodsMask)GOOD_BIT(g) : (GoodsMask)0;
    }
    return mask;
}

namespace {

const float TEXT_SIZE   = 14.0f;    //the card's text
const float HEAD_H      = 20.0f;    //the header: a card's text line
const float STRIP_TOP   = 22.0f;    //the strip, from the top of the line
const float STRIP_H     = 9.0f;     //thick enough to show an outline - a card's bars are 4
const float KEY_TOP     = 33.0f;    //the key's first row
const float KEY_H       = 18.0f;
const float BOTTOM      = 4.0f;
const float GAP         = 2.0f;     //between two goods' pieces
const float SWATCH      = 9.0f;
const float KEY_GAP     = 14.0f;    //between two goods in the key
const float MIN_PIECE   = 3.0f;     //a good there is any of is never thinner than this

#define STYLE_SOLID     0
#define STYLE_HOLLOW    1
#define STYLE_STRIPED   2

std::string Header(const HudStock& st){
    return st.whose.empty() ? st.label : st.label + " - " + st.whose;
}

std::string Figure(const HudStock& st){
    if (!st.figure.empty()){
        return st.figure;
    }
    int total = 0;
    for (int n : st.goods){
        total += n;
    }
    char buf[32];
    if (st.room > 0){
        snprintf(buf,sizeof(buf),"%i of %i",total,st.room);
    }else{
        snprintf(buf,sizeof(buf),"%i",total);
    }
    return buf;
}

std::string KeyText(const HudStock& st, int g){
    char buf[64];
    int r = std::min(st.reserved[g],st.goods[g]);
    if (r > 0){
        snprintf(buf,sizeof(buf),"%i %s (%i reserved)",st.goods[g],GoodName(g),r);
    }else{
        snprintf(buf,sizeof(buf),"%i %s",st.goods[g],GoodName(g));
    }
    return buf;
}

//Each key entry's width, swatch and all, in the strip's order - the goods there are any of.
void KeyItems(const UIOverlay* o, const HudStock& st, float s, std::vector<std::pair<int,float>>& out){
    for (int g : HudGoodsOrder()){
        if (st.goods[g] > 0){
            out.push_back(std::make_pair(g,(SWATCH + 6.0f) * s + o->MeasureText(KeyText(st,g).c_str(),TEXT_SIZE * s).x));
        }
    }
}

//How many rows the key takes at `width`: entries flow left to right and wrap.
int KeyRows(const std::vector<std::pair<int,float>>& items, float width, float s){
    int rows = items.empty() ? 0 : 1;
    float x = 0.0f;
    for (const auto& it : items){
        if (x > 0.0f && x + it.second > width){
            rows++;
            x = 0.0f;
        }
        x += it.second + KEY_GAP * s;
    }
    return rows;
}

/*
    Stripes: lines at 45 degrees up to the right, clipped to the box less half their thickness, so their
    round caps stay inside it.
*/
void Stripes(UIOverlay* o, vec2 a, vec2 b, float s, uint32_t colour){
    const float thick = 1.5f * s;
    const float pitch = 4.5f * s;
    float e = thick * 0.5f;
    float h = b.y - a.y;
    if (h <= 2.0f * e || b.x - a.x <= 2.0f * e){
        return;
    }
    //A stripe from (c, bottom) to (c + h, top); t runs along it from 0 to 1.
    for (float c = a.x - h; c < b.x; c += pitch){
        float t0 = std::max(e / h,(a.x + e - c) / h);
        float t1 = std::min(1.0f - e / h,(b.x - e - c) / h);
        if (t0 < t1){
            o->AddLine(vec2(c + h * t0,b.y - h * t0),vec2(c + h * t1,b.y - h * t1),thick,colour);
        }
    }
}

void Piece(UIOverlay* o, vec2 a, vec2 b, float s, uint32_t colour, int style){
    const float radius = 1.5f * s;
    if (style == STYLE_SOLID){
        o->AddRect(a,b,radius,HudFaded(colour,235));
    }else if (style == STYLE_HOLLOW){
        //An outline centred on its edge: drawn half its thickness in, so it keeps to the piece.
        float t = 1.5f * s;
        o->AddRect(a,b,radius,HudFaded(colour,60));
        o->AddRectOutline(a + vec2(t * 0.5f,t * 0.5f),b - vec2(t * 0.5f,t * 0.5f),radius,t,HudFaded(colour,235));
    }else{
        o->AddRect(a,b,radius,HudFaded(colour,50));
        Stripes(o,a,b,s,HudFaded(colour,235));
    }
}

} //namespace

float HudStockWidth(const UIOverlay* o, const HudStock& st, float s){
    float head = o->MeasureText(Header(st).c_str(),TEXT_SIZE * s).x + 16.0f * s + o->MeasureText(Figure(st).c_str(),TEXT_SIZE * s).x;
    std::vector<std::pair<int,float>> items;
    KeyItems(o,st,s,items);
    float key = 0.0f;
    for (const auto& it : items){
        key += it.second + (key > 0.0f ? KEY_GAP * s : 0.0f);
    }
    return std::max(head,key);
}

float HudStockHeight(const UIOverlay* o, const HudStock& st, float width, float s){
    std::vector<std::pair<int,float>> items;
    KeyItems(o,st,s,items);
    int rows = KeyRows(items,width,s);
    return (rows ? KEY_TOP + rows * KEY_H : STRIP_TOP + STRIP_H) * s + BOTTOM * s;
}

float DrawHudStock(UIOverlay* o, const HudStock& st, float x, float top, float width, float s){
    //The header, its figure at the right.
    float base = top + (HEAD_H - 5.0f) * s;
    o->AddText(st.label.c_str(),vec2(x,base),TEXT_SIZE * s,HUD_TEXT,UI_ALIGN_LEFT);
    if (!st.whose.empty()){
        float at = x + o->MeasureText(st.label.c_str(),TEXT_SIZE * s).x;
        o->AddText((" - " + st.whose).c_str(),vec2(at,base),TEXT_SIZE * s,HUD_TEXT_DIM,UI_ALIGN_LEFT);
    }
    o->AddText(Figure(st).c_str(),vec2(x + width,base),TEXT_SIZE * s,HUD_TEXT_DIM,UI_ALIGN_RIGHT);

    //The strip: over its room - the track showing what is empty - or, with none, shared out over all of it.
    float y0 = top + STRIP_TOP * s;
    float y1 = y0 + STRIP_H * s;
    int total = 0;
    for (int n : st.goods){
        total += n;
    }
    float scale = (float)std::max(1,std::max(st.room,total));
    if (st.room > 0){
        o->AddRect(vec2(x,y0),vec2(x + width,y1),1.5f * s,HudFaded(HUD_TEXT,34));
    }
    std::array<int,GOOD_COUNT> order = HudGoodsOrder();
    int last = -1;
    for (int g : order){
        last = (st.goods[g] > 0) ? g : last;
    }
    int style = st.f_load ? STYLE_STRIPED : st.f_kept ? STYLE_HOLLOW : STYLE_SOLID;
    int cum = 0;
    float end = x;      //where the last piece ended: a tiny good's minimum width pushes the next one on
    for (int g : order){
        int n = st.goods[g];
        if (n <= 0){
            continue;
        }
        float a = std::max(end,x + width * (cum / scale));
        cum += n;
        float b = x + width * (cum / scale) - ((g == last) ? 0.0f : GAP * s);
        b = std::min(x + width,std::max(b,a + MIN_PIECE * s));
        if (b <= a){
            break;
        }
        //What is spoken for at the end of its piece, striped.
        int r = std::min(std::max(st.reserved[g],0),n);
        float split = b - (b - a) * ((float)r / n);
        uint32_t colour = HudGoodColour(g);
        if (split > a + 0.5f){
            Piece(o,vec2(a,y0),vec2(split,y1),s,colour,style);
        }
        if (r > 0){
            Piece(o,vec2(std::max(a,std::min(split,b - MIN_PIECE * s)),y0),vec2(b,y1),s,colour,STYLE_STRIPED);
        }
        end = b + ((g == last) ? 0.0f : GAP * s);
    }

    //The key: a swatch in the strip's own fill, and what it is.
    std::vector<std::pair<int,float>> items;
    KeyItems(o,st,s,items);
    float kx = 0.0f;
    float row_top = top + KEY_TOP * s;
    for (const auto& it : items){
        if (kx > 0.0f && kx + it.second > width){
            kx = 0.0f;
            row_top += KEY_H * s;
        }
        float kb = row_top + (KEY_H - 4.0f) * s;
        vec2 sw0(x + kx,kb - 10.0f * s);
        Piece(o,sw0,sw0 + vec2(SWATCH * s,SWATCH * s),s,HudGoodColour(it.first),style);
        o->AddText(KeyText(st,it.first).c_str(),vec2(x + kx + (SWATCH + 6.0f) * s,kb),TEXT_SIZE * s,HUD_TEXT_DIM,UI_ALIGN_LEFT);
        kx += it.second + KEY_GAP * s;
    }
    return HudStockHeight(o,st,width,s);
}

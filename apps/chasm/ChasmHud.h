#ifndef _CHASM_HUD_H_
#define _CHASM_HUD_H_

#include <stdint.h>
#include <array>
#include <string>
#include "UIOverlay.h"
#include "Goods.h"

/*
    The game's own UI colours - the overlay's, in every build (not the debug panels'). Shared by the
    date and totals (ApplicationChasmTime.cpp) and play mode's cards and pins (ApplicationChasmSelect.cpp),
    so they read as one interface.
*/
#define HUD_PANEL       UIColor( 22, 28, 32,178)    //dark and see-through, so the map shows behind it
#define HUD_TEXT        UIColor(238,236,226,255)
#define HUD_TEXT_DIM    UIColor(238,236,226,150)
#define HUD_MARK        UIColor(255,255,255,255)
#define HUD_AMBER       UIColor(236,202,92,255)     //what wants attention: a site, someone without a house
#define HUD_SELECT      UIColor(255,158,40,255)     //Blender's selection orange: the selection's own colour
#define HUD_WATER       UIColor(104,178,240,255)    //the thirst mark's drop
#define HUD_DANGER      UIColor(226,72,56,255)      //what is costing a life now: someone dry

inline uint32_t HudFaded(uint32_t c, uint8_t alpha){
    return (c & 0x00FFFFFFu) | ((uint32_t)alpha << 24);
}

/*
    THE INTERFACE'S SIZE (the user's, 2026-10-16): every HUD size is laid out at s = 1 for a window HUD_UI_HEIGHT
    high, which is the window Chasm opens in. It grows with the SQUARE ROOT of the height past that - full screen at
    1440 is 1.33, not 1.78, so a bigger screen shows more of the colony rather than the same panels larger - times
    the player's UI size setting (`ui_size`, Settings). Never under 0.5.
*/
#define HUD_UI_HEIGHT           810.0f
#define HUD_UI_SIZES            6
const float HUD_UI_SIZE_STEPS[HUD_UI_SIZES] = {0.75f,0.85f,1.0f,1.15f,1.3f,1.5f};   //the setting's choices
float HudUiScale(float window_h, float ui_size);
//The same for a UIMenu, whose layout is in screen heights: its UIMenuLayout::scale.
float HudMenuScale(float window_h, float ui_size);

/*
    THE BUILD (ChasmVersion.h; the user's, 2026-10-16), in the bottom right corner of the title and the pause menu:
    the version, the commit and the day it was built - and a debug build says so - on a panel, so it reads over any
    picture and a tester's screenshot names what it was taken in. Returns the panel's top.
*/
float DrawHudBuild(UIOverlay* o, float screen_w, float screen_h, float s);

/*
    THE GOODS' COLOURS (the user's, 2026-10-09): the top bar's wood, food and water, with the food's gold
    for wheat and a colour of its own for every other food, so a card tells them apart. Anything not named
    here - a good added later - is a plain grey until it is given one. Strips and keys show the goods by
    category (HudGoodsOrder, goods_plan.md): the materials - wood first - then the foods, water, the inputs.
*/
uint32_t HudGoodColour(int good);
std::array<int,GOOD_COUNT> HudGoodsOrder();

/*
    THE GOODS' GROUPS on a card (goods_plan.md, "The view"): a stock strip each where a stock holds more than one,
    and a row of a store's "Takes" chips each - MATERIALS, FOOD AND WATER together as a house's pantry holds them,
    and INPUTS. In HudGoodsOrder's order.
*/
#define HUD_GROUP_MATERIALS     0
#define HUD_GROUP_FOOD_WATER    1
#define HUD_GROUP_INPUTS        2
#define HUD_GROUP_COUNT         3
int HudGoodsGroup(int good);
const char* HudGoodsGroupName(int group);
GoodsMask HudGoodsGroupMask(int group);

/*
    A GOODS STRIP: one stock - what a building holds, or a part of it - as a line on a card. A header
    ("Pantry - the Veen family's", and "12 of 16" at the right), a strip over its room cut into a piece per
    good in the good's colour, and under it a key naming each good and how many.

    The fill says WHO MAY TAKE IT, the question the card is there to answer: SOLID, anyone's - a store, a
    camp's supplies, a woodpile, a house nobody lives in; HOLLOW, kept - a lived-in house's own, a camp's
    larder, a field's harvest for its land's family, a site's; STRIPED, of either, what somebody is already
    on his way to take (Economy::GoodFree's reservations, EconomyReserved). Without a room the goods share
    the whole strip.
*/
struct HudStock{
    std::string label;                          //"Pantry", "Holds", "Harvest"
    std::string whose;                          //"the Veen family's", "anyone's" - empty for none
    std::array<int,GOOD_COUNT> goods = {};
    std::array<int,GOOD_COUNT> reserved = {};   //of `goods`, what is spoken for
    int room = 0;                               //0: none
    bool f_kept = false;
    std::string figure;                         //at the header's right in place of "12 of 16": a group's part of a stock
};
//Its natural width, the key on one line; its height at `width`, the key wrapping to fit; drawn from `top`
//(the top of its line, as a card counts them) at `width`, returning that height.
float HudStockWidth(const UIOverlay* o, const HudStock& st, float s);
float HudStockHeight(const UIOverlay* o, const HudStock& st, float width, float s);
float DrawHudStock(UIOverlay* o, const HudStock& st, float x, float top, float width, float s);

#endif

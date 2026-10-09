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
    THE GOODS' COLOURS (the user's, 2026-10-09): the top bar's wood, food and water, with the food's gold
    for wheat and a colour of its own for every other food, so a card tells them apart. Anything not named
    here - a good added later - is a plain grey until it is given one. Strips and keys show the goods in
    the top bar's order (HudGoodsOrder): wood, the foods, water, then the rest.
*/
uint32_t HudGoodColour(int good);
std::array<int,GOOD_COUNT> HudGoodsOrder();

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
};
//Its natural width, the key on one line; its height at `width`, the key wrapping to fit; drawn from `top`
//(the top of its line, as a card counts them) at `width`, returning that height.
float HudStockWidth(const UIOverlay* o, const HudStock& st, float s);
float HudStockHeight(const UIOverlay* o, const HudStock& st, float width, float s);
float DrawHudStock(UIOverlay* o, const HudStock& st, float x, float top, float width, float s);

#endif

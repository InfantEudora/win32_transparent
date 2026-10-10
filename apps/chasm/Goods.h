#ifndef _CHASM_GOODS_H_
#define _CHASM_GOODS_H_

#include <stdint.h>
#include <string>

/*
    THE GOODS (docs/economy_plan.md): wood, water, and food by its crop - three foods, because what
    keeps is the point of the three (gameplay_plan.md, "Goods and food") - PROVISIONS, made from a
    wheat and a beans for posted people (defence_plan.md), and HEMP and the ROPE made from it (goods_plan.md).
    Never renumber: saves name a store's stock and what it takes by these.

    A MASK of goods is a GoodsMask, one bit a good - what a store takes (ZoneBuilding::allow). 16 bits
    (the user's, 2026-10-09): steam, floatstone, coal and leather are coming.
    PROVISIONS ARE NOT IN GOODS_FOOD, on purpose: everything villagers do with food - fetching it home, a
    house's pantry, fair shares, a home's days of food - then leaves them alone, and they count only where
    GOODS_EATEN is used instead: a posted person's needs and pack, and a villager's last resort.
*/
#define GOOD_WOOD       0
#define GOOD_WATER      1
#define GOOD_WHEAT      2       //the foods: one per crop, in ZONE_CROP_* order
#define GOOD_GREENS     3
#define GOOD_BEANS      4
#define GOOD_PROVISIONS 5       //a soldier's food: a wheat and a beans (defence_plan.md)
#define GOOD_HEMP       6       //a crop that is not food: made into rope at a ropewalk (goods_plan.md)
#define GOOD_ROPE       7       //a building material: the winch's, later the zeppelin's
#define GOOD_COUNT      8

/*
    A good's CATEGORY (goods_plan.md, the user's 2026-10-10): FOOD is eaten and WATER drunk; a MATERIAL is
    built with, once; an INPUT is used up, continuously - burned, or made into something. A good used both ways
    has one home category, by what it is mostly for: wood builds, and is burned in a woodshed, and is a
    material. Only FOOD's goods stand in for one another, so only FOOD is ever summed.
*/
#define GOOD_CATEGORY_FOOD      0
#define GOOD_CATEGORY_WATER     1
#define GOOD_CATEGORY_MATERIALS 2
#define GOOD_CATEGORY_INPUTS    3
#define GOOD_CATEGORY_COUNT     4

typedef uint16_t GoodsMask;
#define GOOD_BIT(g)     ((GoodsMask)(1u << (g)))
#define GOODS_ALL       ((GoodsMask)((1u << GOOD_COUNT) - 1))
#define GOODS_FOOD      ((GoodsMask)(GOOD_BIT(GOOD_WHEAT) | GOOD_BIT(GOOD_GREENS) | GOOD_BIT(GOOD_BEANS)))
#define GOODS_EATEN     ((GoodsMask)(GOODS_FOOD | GOOD_BIT(GOOD_PROVISIONS)))  //every food, provisions too
//What a field yields, whatever is sown on it: the foods, and hemp (goods_plan.md).
#define GOODS_CROPS     ((GoodsMask)(GOODS_FOOD | GOOD_BIT(GOOD_HEMP)))

const char* GoodName(int good);
int GoodByName(const std::string& name);    //-1 if none
//The good a crop (ZONE_CROP_*, Zones.h) yields, or -1. By table: hemp came after provisions, so the crops' goods
//are no longer one run.
inline int GoodOfCrop(int crop){
    static const int goods[] = {GOOD_WHEAT,GOOD_GREENS,GOOD_BEANS,GOOD_HEMP};
    return (crop >= 0 && crop < (int)(sizeof(goods) / sizeof(goods[0]))) ? goods[crop] : -1;
}
//GOOD_CATEGORY_*, or -1 for no good.
inline int GoodCategory(int good){
    switch (good){
        case GOOD_WHEAT:
        case GOOD_GREENS:
        case GOOD_BEANS:
        case GOOD_PROVISIONS:   return GOOD_CATEGORY_FOOD;
        case GOOD_WATER:        return GOOD_CATEGORY_WATER;
        case GOOD_WOOD:
        case GOOD_ROPE:         return GOOD_CATEGORY_MATERIALS;
        case GOOD_HEMP:         return GOOD_CATEGORY_INPUTS;
        default:                return -1;
    }
}
//A food, provisions too.
inline bool GoodIsFood(int good){ return GoodCategory(good) == GOOD_CATEGORY_FOOD; }

#endif

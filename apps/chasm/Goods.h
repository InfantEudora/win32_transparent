#ifndef _CHASM_GOODS_H_
#define _CHASM_GOODS_H_

#include <stdint.h>
#include <string>

/*
    THE GOODS (docs/economy_plan.md): wood, water, and food by its crop - three foods, because what
    keeps is the point of the three (gameplay_plan.md, "Goods and food"). Never renumber: saves name a
    store's stock and what it takes by these.

    A MASK of goods is a byte, one bit a good - what a store takes (ZoneBuilding::allow).
*/
#define GOOD_WOOD       0
#define GOOD_WATER      1
#define GOOD_WHEAT      2       //the foods: one per crop, in ZONE_CROP_* order
#define GOOD_GREENS     3
#define GOOD_BEANS      4
#define GOOD_COUNT      5

#define GOOD_BIT(g)     ((uint8_t)(1u << (g)))
#define GOODS_ALL       ((uint8_t)((1u << GOOD_COUNT) - 1))
#define GOODS_FOOD      ((uint8_t)(GOOD_BIT(GOOD_WHEAT) | GOOD_BIT(GOOD_GREENS) | GOOD_BIT(GOOD_BEANS)))

const char* GoodName(int good);
int GoodByName(const std::string& name);    //-1 if none
inline int GoodOfCrop(int crop){ return GOOD_WHEAT + crop; }
inline bool GoodIsFood(int good){ return good >= GOOD_WHEAT && good < GOOD_COUNT; }

#endif

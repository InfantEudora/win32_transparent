#ifndef _CHASM_GOODS_H_
#define _CHASM_GOODS_H_

#include <stdint.h>
#include <string>

/*
    THE GOODS (docs/economy_plan.md): wood, water, and food by its crop - three foods, because what
    keeps is the point of the three (gameplay_plan.md, "Goods and food") - and PROVISIONS, made from a
    wheat and a beans for posted people (defence_plan.md). Never renumber: saves name a store's stock and
    what it takes by these.

    A MASK of goods is a GoodsMask, one bit a good - what a store takes (ZoneBuilding::allow). 16 bits
    (the user's, 2026-10-09): steam, floatstone, coal, rope and leather are coming.
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
#define GOOD_COUNT      6

typedef uint16_t GoodsMask;
#define GOOD_BIT(g)     ((GoodsMask)(1u << (g)))
#define GOODS_ALL       ((GoodsMask)((1u << GOOD_COUNT) - 1))
#define GOODS_FOOD      ((GoodsMask)(GOOD_BIT(GOOD_WHEAT) | GOOD_BIT(GOOD_GREENS) | GOOD_BIT(GOOD_BEANS)))
#define GOODS_EATEN     ((GoodsMask)(GOODS_FOOD | GOOD_BIT(GOOD_PROVISIONS)))  //every food, provisions too

const char* GoodName(int good);
int GoodByName(const std::string& name);    //-1 if none
inline int GoodOfCrop(int crop){ return GOOD_WHEAT + crop; }
//A food, provisions too - the next NON-food good appended will have to change this.
inline bool GoodIsFood(int good){ return good >= GOOD_WHEAT && good < GOOD_COUNT; }

#endif

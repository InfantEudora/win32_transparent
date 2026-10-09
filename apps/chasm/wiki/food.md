---
title: Food
aliases: crop, crops, wheat, greens, beans, rotation
kind: goods
status: built
summary: Three crops (wheat, greens and beans), each grown on fields, eaten for health, and best grown in rotation.
plans: fields_plan.md > Decided; fields_plan.md > AUTO by food; needs_plan.md > Current tuning
code: Economy.cpp: crop_days crop_yield crop_after EconomyFoodWorth EconomyCropYield EconomyAutoCrop
verified: 2026-10-09
---

| Crop | Grows for | Harvest per unit of field area | Worth eaten |
|---|---|---|---|
| Wheat | {crop_days[ZONE_CROP_WHEAT]:#days} | {crop_yield[ZONE_CROP_WHEAT]} | between the other two |
| Greens | {crop_days[ZONE_CROP_GREENS]:#days} | {crop_yield[ZONE_CROP_GREENS]} | the least |
| Beans | {crop_days[ZONE_CROP_BEANS]:#days} | {crop_yield[ZONE_CROP_BEANS]} | the most |

Growing days are at an average farmer's pace ([[Skills]]). What a unit is worth is the [[Health|health]]
it gives (`EconomyFoodWorth`).

## Rotation

A harvest also depends on what grew on the [[Field|field]] before it. A field's first crop gets its full
yield; after that, in percent:

| After... | wheat | greens | beans |
|---|---|---|---|
| wheat | {crop_after[ZONE_CROP_WHEAT][ZONE_CROP_WHEAT]} | {crop_after[ZONE_CROP_WHEAT][ZONE_CROP_GREENS]} | {crop_after[ZONE_CROP_WHEAT][ZONE_CROP_BEANS]} |
| greens | {crop_after[ZONE_CROP_GREENS][ZONE_CROP_WHEAT]} | {crop_after[ZONE_CROP_GREENS][ZONE_CROP_GREENS]} | {crop_after[ZONE_CROP_GREENS][ZONE_CROP_BEANS]} |
| beans | {crop_after[ZONE_CROP_BEANS][ZONE_CROP_WHEAT]} | {crop_after[ZONE_CROP_BEANS][ZONE_CROP_GREENS]} | {crop_after[ZONE_CROP_BEANS][ZONE_CROP_BEANS]} |

Repeating a crop wears the soil down; beans feed it for whatever follows. A crop lost to the frost
doesn't count as a harvest.

**AUTO** sows whichever crop gives the most health per growing day after what grew before
(`EconomyAutoCrop`), among those that will still ripen before the frost.

## Where it is

Food is in [[Store|stores]], houses' larders, the [[Camp|camp's]] supplies and larder, and fields'
harvests waiting to be carried in. The settlers bring {ECONOMY_START_WHEAT} wheat and
{ECONOMY_START_BEANS} beans. People eat the food that keeps worst first: greens, then wheat, then beans.

## Planned

- [planned] Spoilage: greens rotting when winter comes, wheat a little each season, beans not at all.
- [planned] Provisions: a food made from wheat and beans for people on [[Posts|posts]].
- [idea] Early gathering: food from the wild for the first days.
- [idea] Fruit (keeps a short while), and animals for meat and leather.

---
title: Food
aliases: crop, crops, wheat, greens, beans, rotation
kind: goods
status: built
summary: Three crops (wheat, greens and beans), each grown on fields, eaten for health, and best grown in rotation - and fruit from the wild. Lying out, it rots.
plans: fields_plan.md > Decided; fields_plan.md > AUTO by food; needs_plan.md > Current tuning; spoilage_plan.md > Decided (the user, 2026-10-24); spoilage_plan.md > As built (2026-10-24)
code: Economy.cpp: crop_days crop_yield crop_after EconomyFoodWorth EconomyCropYield EconomyAutoCrop EconomySpoil EconomyRotPerMille
verified: 2026-10-24
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

Food is in [[Store|stores]], houses' pantries, the [[Camp|camp's]] supplies and pantry, and fields'
harvests waiting to be carried in - and [[Fruit|fruit]], picked from the wild. The settlers bring {ECONOMY_START_WHEAT} wheat and
{ECONOMY_START_BEANS} beans. People eat the food that keeps worst first: [[Fruit|fruit]], then greens, then wheat, then beans -
and [[Provisions|provisions]] last of all, a food made from a wheat and a beans for people on posts.

## Lying out, it rots

A harvest waiting on its [[Field|field]] and food in a [[Crate|crate]] lie out. What came there keeps for
{ECONOMY_ROT_GRACE_DAYS:#days}; after that, at every dawn, a share of each food rots, rounded up: fruit
{= ECONOMY_ROT_FRUIT / 10}%, greens {= ECONOMY_ROT_GREENS / 10}%, wheat {= ECONOMY_ROT_WHEAT / 10}%, beans
{= ECONOMY_ROT_BEANS / 10}% and [[Provisions|provisions]] {= ECONOMY_ROT_PROVISIONS / 10}%. Whatever is taken goes from
what has lain longest. Food in a [[Store|store]], a house or the [[Camp|camp]] keeps. A field's or crate's card says
how its food keeps, the [[Statistics|statistics]] count what rotted, and NEEDS ATTENTION says when a field's harvest
rots faster than its stores have room to take it in: HARVEST ROTTING, NO STORE ROOM.

## In the top bar

FOOD at the top is the foods summed - wheat, greens, beans and fruit - since any of them feeds someone; it counts
what lies in [[Store|stores]] and the [[Camp|camp's]] supplies - of the [[Settlement|colony]] in view, once there
is more than one. Hovering it lists each food on its own, and
[[Provisions|provisions]] beside them, which are the posted people's and not in the sum - and, also apart, the food
lying out on fields and in crates.

## Planned

- [planned] Keeping in a store: fruit and greens rotting when winter comes, wheat a little each season, beans not at all.
- [idea] Animals for meat and leather.

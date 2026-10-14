---
title: Fruit
aliases: foraging, forage, picker, pickers, berries, bush, bushes, fruit tree, fruit trees, pick fruit, draw water, picking by hand
kind: goods
status: built
summary: Food from the wild. Bushes and a few oaks bear it and grow it back after picking; a camp's picker brings it in, and someone sent out can pick or draw water by hand.
plans: foraging_plan.md > Decided (the user, 2026-10-12); foraging_plan.md > As built (2026-10-12)
code: Economy.cpp: FruitAtDawn PlantsCovered PickFrom GoPick Forage ForageWant ForageUnit BankPlot AssignJobs EconomyFoodWorth; Economy.h: WORKER_JOB_PICKER FORAGE_GONE; Forest.h: ForestBears FOREST_FRUIT_TREE_CHANCE
verified: 2026-10-14
---

A [[Food|food]] that is not grown on a [[Field|field]]: it keeps no better than greens, and it is **eaten
first**, before any other food in a pantry or a pack. Worth as much as a wheat (`EconomyFoodWorth`).

## The plants

Both kinds of **bush** bear it, and about one **oak** in {= 1 / FOREST_FRUIT_TREE_CHANCE} is a fruit tree,
chosen with the map. A ripe bush holds {FORAGE_BUSH_FRUIT} fruit, a ripe fruit tree {FORAGE_TREE_FRUIT}.
A new map's plants are all ripe, and ripe plants show their fruit.

- **Picked**, a plant gives what the picker has room for and keeps the rest. Once it is **bare**, it is
  ripe again after {FORAGE_REGROW_DAYS:#days} of growing weather.
- **The frost**: at a dawn in winter or under the snow, the fruit on every plant is lost, and nothing grows
  back until the weather turns, as on a field.
- **Built over** - a building's storey, a garden or lot, a built field - a plant is gone for good, even
  if what covered it comes down.
- A [[Woodcutter]] passes fruit trees by, felling one only when it stands in a road's, wall's or site's way.

## The camp's picker

A [[Camp|camp]] with a plant standing in its [[Settlement|settlement's]] land has **{ECONOMY_CAMP_PICKERS:#picker}**,
filled after its water carrier, by [[Skills|husbandry]]: from those living in the camp first, then from anyone
in the colony no other workplace needs, so picking goes on after everyone has moved into houses (see
[[Camp|camp]], "Its work"). While the camp's supplies hold less than
{ECONOMY_CAMP_FRUIT_HOLD} fruit, the picker goes to the nearest ripe plant of the settlement, picks at
{FORAGE_PICK_SECONDS:seconds} a fruit at their pace, and goes on to the next within
{FORAGE_ROUND_REACH} until they carry {ECONOMY_LOAD}, then takes it into the supplies. Nothing takes the
place over: picking fades as fields and houses cover the bushes.

## Picking and drawing by hand

With someone selected, a **right click on a ripe plant** offers *Pick fruit* or *Post here*, and **on
water** *Draw water*. The person is [[Posts|posted]] there, picks the plant into their pack as far as a
scout's food ({NEEDS_SCOUT_FOOD:days}), or draws water at the edge beside it at the camp's carrier's pace
up to a scout's water ({NEEDS_SCOUT_WATER}), a unit at a time, and then stands there. A group sent to
pick spreads over the nearest ripe plants. Only on their own settlement's land or land nobody holds, never
under the snow. So an expedition can live off the land without pitching a tent ([[Founding]]).

**Nobody picks for themselves.** The [[Hunger|hungry]] go to a store, the camp's supplies or a field's
harvest, past ripe plants; a scout out of food with none of those in reach starves beside a bush unless
sent to pick it.

## Planned

- [idea] Fishing at a river camp, if bushes alone are too thin.
- [idea] The hungry eating at a ripe plant, as at a field's harvest - and perhaps the posted drinking at
  the water's edge. Left for now as a big change ([foraging_plan.md](../docs/foraging_plan.md), Open).

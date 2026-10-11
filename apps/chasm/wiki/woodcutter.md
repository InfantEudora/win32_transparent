---
title: Woodcutter
aliases: woodcutter's hut, woodcutters' huts, hut, woodpile
kind: people
status: built
summary: The only one who fells trees. Works from a hut or from the camp, fells what is in the way first, and carries the wood to a store, or into the hut and onto its lot.
plans: economy_plan.md > The woodcutter: logs first; woodcutting_plan.md > Decided (the user, 2026-10-11); woodcutting_plan.md > As built (2026-10-11); yards_plan.md > Noted
code: Economy.cpp: TickWorker FindTree WoodNear TreesInTheWay FetchFromWorkplace FindStore Felled AssignJobs EconomySkillFactor; Economy.h: WORKER_JOB_WOODCUTTER
tuning: ECONOMY_PICK_SECONDS
verified: 2026-10-12
---

A job at a **woodcutter's hut**, which the player paints, or at the [[Camp|camp]], which has one
woodcutter's place of its own until a hut stands in its [[Settlement|settlement]]: the hut takes it over. **Only a woodcutter fells a tree**: a [[Roads and walls|road]], a wall or a
[[Construction|site]] with a tree in its way waits for one. A hut's card can **stop work**: it fells nothing
then, its woodcutter goes free, and the camp's woodcutter does not come back.

## What a log is worth

The bigger the shop, the easier the work:

| Felled from | Wood a log |
|---|---|
| the camp, with no hut | {ECONOMY_CAMP_LOG_WOOD} |
| a hut | {ECONOMY_WOOD_PER_TREE} |
| a hut two storeys high on every plot | {ECONOMY_LOFT_LOG_WOOD} |

## Its room

A hut keeps wood **indoors**, {ECONOMY_HUT_ROOM} for every unit of its floor area past the first
{ECONOMY_HUT_WORKSHOP}, the floor he works in: a big hut is a cheap store for wood, a small one keeps none
and needs a lot. Each standing **lot** of its [[Yard|yard]] holds {ECONOMY_LOT_ROOM} more per unit of its
area, piled outside. The camp's woodcutter puts his wood in with its supplies, up to {ECONOMY_CAMP_WOODPILE}.
A hut with room for not even a log fells nothing, and NEEDS ATTENTION says so. The price tag says what a hut
would keep before it is placed.

## Strength

- **Who gets the job** ([[Skills]]): at a hut, the best strength ({ECONOMY_SKILL_DISTANCE} world units a
  point) less how far they live from it ([[Person|jobs]]); at the camp, the strongest. It is kept.
- **Felling**: pace is 0.6 + 0.08 a point (`EconomySkillFactor`): about 1.5 times as long at strength 1 as
  at 5, and 0.7 times as long at 10.
- **Carrying**: as anyone with a load, {ECONOMY_CARRY_STRENGTH} faster a point over 5, slower under. Nothing
  else changes with it, so the strongest bring in some 20-30% more wood a day than the weakest.

## A round

Every round starts and ends at the woodcutter's [[House|house]], or tent.

1. **What waits at the hut first.** If a [[Store]] will take wood, a log's worth is fetched from the hut
   and delivered. The camp's supplies stay where they are.
2. **Otherwise wood** within {ECONOMY_WOODCUTTER_REACH} world units' **walk** of the hut or camp - a tree across a
   river is as far as the way round - a tree **in the way** of a planned road, wall or site first, then a log
   lying, then the nearest standing [[Tree]] by the walk. Only while a store, the hut or its lot has room for
   it. Felling takes {ECONOMY_FELL_SECONDS:#seconds} at strength 5 (see Strength, above).
3. **A second log**, if one lies within {ECONOMY_LOG_PAIR_REACH} and there is room for both: at most
   {ECONOMY_WOODCUTTER_LOGS:#logs} a trip.
4. **Deliver** to the nearest store that takes wood and has room, or else into the hut and onto its lot.
   If neither has room, the woodcutter waits, holding the wood.
5. **Home**, then the next round.

A site whose last tree is felled goes up at once, if all it takes is there.

## Before the round

At home, before any of this, the woodcutter sees to a pressing [[Thirst|thirst]] or [[Health|hunger]]
that the house can't meet. They may also be the one sent to [[Fetching home|fetch]] for the house, but
only if no jobless family member is home to go instead.

## Planned

- [idea] More woodcutters at a bigger hut, rather than better logs (woodcutting_plan.md, "Later").

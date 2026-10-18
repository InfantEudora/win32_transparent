---
title: Stonecutter
aliases: stonecutter's hut, stonecutters' huts, stonecutters, rock, rocks, boulder, boulders, break a rock, breaking rocks
kind: people
status: built
summary: Breaks the rocks round his hut for stone - a boulder on a building site first - and leaves their stone as rubble, carrying a load of it to a store.
plans: stone_plan.md > 3. Clearing rocks: the stonecutter's hut; stone_plan.md > As built: step 3 (2026-10-14)
code: Zones.h: ZONE_KIND_STONECUTTER; Economy.h: WORKER_JOB_STONECUTTER ECONOMY_STONECUTTER_REACH ECONOMY_BREAK_SECONDS ECONOMY_STONECUTTER_STONE CRATE_BROKEN; Economy.cpp: FindRock BreakRock SiteTreesStanding StoreyGoods; Forest.h: PROP_BOULDER ForestRockStone FOREST_BOULDER_CHANCE
verified: 2026-10-18
---

A job at a **stonecutter's hut**, which the player drags out like a woodcutter's, at most {ZONE_BUILDING_MAX_PLOTS:#plots}: one storey, built with wood and
**{ECONOMY_STONECUTTER_STONE} [[Stone|stone]]** a plot. Its one place goes to whoever scores best on strength
against the walk from home ([[Person|jobs]]), and its card can **stop work**. **Only a stonecutter breaks a rock.**

## Rocks and boulders

The rocks scattered over the map are his. About {= FOREST_BOULDER_CHANCE * 100}% of the open land's rocks are
**boulders**, bigger than any other rock. A rock gives stone by its size (`ForestRockStone`): a small one 2, the
largest rock 4, a cluster 6, a boulder 12.

- **A boulder blocks building**: a [[Construction|site]] with one on it - a building, a garden or lot, a field -
  rises only once it is broken, as a site with a [[Tree|tree]] waits for a [[Woodcutter|woodcutter]]. Its card says
  so. Other rocks block nothing.
- A rock under something built is as good as gone: nobody breaks it.

## A round

Every round starts at his home, while a [[Store|store]] of his [[Settlement|settlement]] has room for stone. Within
{ECONOMY_STONECUTTER_REACH} world units' **walk** of his hut:

1. **A boulder on a site** of his settlement's, first.
2. **Else rubble lying**: any pile of stone, a [[Crate|crate]] - less what others are already lifting.
3. **Else the nearest rock** standing.

Breaking takes {ECONOMY_BREAK_SECONDS:#seconds} a stone of it at strength 5, quicker when stronger. The rock is gone
for good, and its stone lies where it stood as **rubble** - beside a site, on the nearest open plot. He lifts a load
of it there and then and carries it to the nearest store that takes it; the rest lies for him, for sites and for
the [[Builder|builders]], as any crate does. Its card says "Broken from a rock".

A store next to the hut takes only stone. The hut keeps none itself.

## Planned

- [idea] A hut that keeps stone indoors, as a woodcutter's keeps wood.

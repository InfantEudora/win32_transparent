---
title: Water
aliases: river, rivers, pool, pools, pond, ponds
kind: goods
status: built
summary: Drawn from rivers and pools by water collectors. Everyone drinks it, and nothing else quenches thirst.
plans: economy_plan.md > Production; needs_plan.md > Current tuning; water_tools_plan.md > As built: step 4, the tools
code: Economy.h: NEEDS_WATER_WORTH ECONOMY_WATER_ROOM; Economy.cpp: Consume EconomyCollectorRoom; GridWater.h: ChasmPlaceWater ChasmPondFits
verified: 2026-10-18
---

- **Where it comes from**: [[Water collector|water collectors]] on the banks of rivers and pools, while
  they have a carrier and no snow lies on them; [[Condenser|condensers]], from steam, snow or not. The settlers bring {ECONOMY_START_WATER}. On a test map,
  a debug [[Well|well]].
- **Where it goes**: carriers take it to a [[Store]] that takes water, {ECONOMY_LOAD} at a time. Houses
  [[Fetching home|fetch]] it home, and anyone with a pressing thirst drinks at a store, the camp or a
  collector.
- **What it's worth**: a unit puts {NEEDS_WATER_WORTH:days} back on the water bar ([[Thirst]]). Water
  is drunk before any food is eaten.
- **In winter** collectors under the snow stop filling ([[Seasons]]), so homes in the north live on what
  was stored.
- People on a [[Posts|post]] carry {NEEDS_PACK_WATER} units in their pack.

The river's wet bank and stones can't be walked or built on. Bridges cross rivers - and in winter the
[[Posts|posted]] cross the ice ([[Seasons]]); nothing crosses the chasm on foot.

**Placed by hand** (debug, for test maps): the panel's River and Pond tools, or `chasm_generate`'s `water`, add
rivers and ponds to the map's settings - each a new map. A placed river is a river like any other, ending where it
was drawn; a pond is still, clear water pressed into the plateau, a pool's rules round it.

---
title: Water
aliases: river, rivers, pool, pools
kind: goods
status: built
summary: Drawn from rivers and pools by water collectors. Everyone drinks it, and nothing else quenches thirst.
plans: economy_plan.md > Production; needs_plan.md > Current tuning
code: Economy.h: NEEDS_WATER_WORTH ECONOMY_WATER_ROOM; Economy.cpp: Consume EconomyCollectorRoom
verified: 2026-10-12
---

- **Where it comes from**: [[Water collector|water collectors]] on the banks of rivers and pools, while
  they have a carrier and no snow lies on them. The settlers bring {ECONOMY_START_WATER}.
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

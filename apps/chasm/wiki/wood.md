---
title: Wood
aliases: timber
kind: goods
status: built
summary: Comes from trees, felled by woodcutters alone. It builds everything that is built, and burns as firewood in winter.
plans: economy_plan.md > Production; construction_plan.md > As built; woodcutting_plan.md > Decided (the user, 2026-10-11)
code: Economy.h: ECONOMY_WOOD_PER_TREE ECONOMY_BUILD_WOOD_PER_AREA; Economy.cpp: EconomyStoreyCost EconomyGroundCost
verified: 2026-10-11
---

## Getting it

- [[Woodcutter|Woodcutters]] fell [[Tree|trees]]: a log is worth {ECONOMY_CAMP_LOG_WOOD} from the camp,
  {ECONOMY_WOOD_PER_TREE} from a hut and {ECONOMY_LOFT_LOG_WOOD} from a hut two storeys high.
- The settlers bring {ECONOMY_START_WOOD}.
- It is kept in [[Store|stores]], the [[Camp|camp's]] supplies, woodcutters' huts and lots, and houses'
  woodsheds.

## Using it

| Use | Wood |
|---|---|
| A building's storey | {ECONOMY_BUILD_WOOD_PER_AREA} per unit of floor area, more or less by kind ([[Construction]]) |
| A garden's or lot's plot | {ECONOMY_GROUND_WOOD_PER_AREA} per unit of area |
| A [[Field|field]] | {ECONOMY_FIELD_WOOD_PER_CELL} a cell |
| A wall plot | {ECONOMY_WALL_WOOD} ([[Roads and walls]]) |
| A road | none, only work |
| Firewood under the snow | {ECONOMY_FIREWOOD_BURN} per plot-storey of a home, each dawn ([[Cold]]) |

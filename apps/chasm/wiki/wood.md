---
title: Wood
aliases: timber
kind: goods
status: built
summary: Comes from trees, felled by woodcutters and by people clearing roads. It builds everything that is built, and burns as firewood in winter.
plans: economy_plan.md > Production; construction_plan.md > As built
code: Economy.h: ECONOMY_WOOD_PER_TREE ECONOMY_BUILD_WOOD_PER_AREA; Economy.cpp: EconomyStoreyCost EconomyGroundCost
verified: 2026-10-09
---

## Getting it

- [[Woodcutter|Woodcutters]] fell [[Tree|trees]], {ECONOMY_WOOD_PER_TREE} wood each, and fetch the logs
  that road and wall clearing leaves lying.
- The settlers bring {ECONOMY_START_WOOD}.
- It is kept in [[Store|stores]], the [[Camp|camp's]] supplies, woodcutters' woodpiles, and houses'
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

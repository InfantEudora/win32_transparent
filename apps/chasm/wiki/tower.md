---
title: Guard tower
aliases: tower, towers, guard towers, guard, guards, ladder
kind: buildings
status: partial
summary: A two-storey tower of logs in its junction's shape, a guard for each edge past two, built with water and provisions that become its stock - its guards' pantry, entered only by its ladder.
plans: defence_plan.md > The guard tower; defence_plan.md > As built: step 6, the guard tower; defence_plan.md > As built: the tower tuned; defence_plan.md > Build order
code: Zones.cpp: ZoneCanBuild ZoneTowerGuards ZoneTowerCorners ZoneTowerLadderOk ZoneTowerDefaultLadder ZoneIsLadderPlot; Walkers.cpp: EdgeOpen; Economy.cpp: AssignJobs Needs SeekSupplyOf TowerStoreyCost StoreyKept EconomyPostPoint EconomyOnTower; Economy.h: ECONOMY_TOWER_WOOD ECONOMY_TOWER_WATER ECONOMY_TOWER_PROVISIONS WORKER_JOB_GUARD
verified: 2026-10-10
---

Tool **T**. One plot, always both storeys: it is painted whole, and taking a storey off takes it all. It
stands by a building's rules, and also **in a wall's line**: painted on a [[Roads and walls|wall]] plot,
planned or standing, it takes that plot over, and the palisade runs up to its sides. Never on a road.

## Its junction

It takes its **junction's shape**: a triangle, a square, a pentagon or a hexagon, its size its plot's. The
junction decides its **guards**, its edges less two: 1 on a 3-junction, 2 on a 4, 3 on a 5, 4 on a 6. Nearly
every plot is a 4; the ghost's shape shows the rarer ones.

## Building it

So much a guard place: {ECONOMY_TOWER_WOOD} wood, {ECONOMY_TOWER_WATER} water and
{ECONOMY_TOWER_PROVISIONS} [[Provisions|provisions]], half with each storey - so no shape is cheaper for what
it holds. What a storey takes of water and provisions it **keeps** as its stock. Provisions come only from
builders, so no builders' hut, no tower. Placed in debug, it stands full.

## Its ladder is its door

Walkers get onto a tower only from the plot its ladder stands toward, and off only to it. Placed, the ladder
takes the open side nearest the camp - usually a wall's inside; with no open side the tower is refused. The
card's **Move ladder** turns it to the next open side. Nothing is built, and no wall drawn, on the ladder's
plot.

## Its guards

- Filled like any workplace's - by **archery** first, what the archery range trained, then by
  [[Skills|precision]].
- A guard lives as someone [[Posts|posted]] does: up the tower from home with his pack, never home while he has
  the job, at a place of his own on the platform.
- **The tower is his pantry**: he eats and drinks from its stock as at home; his pack is only for a kind the
  tower has none of. Both empty, a pressing need sends him to the nearest supply and back up.
- The tower pulled down, its guards lose the job and go home.

Only posted people draw on it: villagers pass towers by, and no carrier, family or builder takes from one.

## Planned

- [planned] Builders keeping towers full, ahead of their building: a tower below its full stock is to them a
  site that wants water and provisions.
- [planned] A right click on a tower posts the selected person there, if it has a free place.
- [planned] A **gatehouse**: two towers either side of a palisade gate joined by a walkway between their decks,
  with guard places of its own, reached by the towers' ladders. Towers are never arched over a road
  ([[Roads and walls|arches]]); this is what a tower pair over a gate becomes instead.

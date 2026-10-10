---
title: Guard tower
aliases: tower, towers, guard towers, guard, guards
kind: buildings
status: partial
summary: A two-storey tower of logs for two guards, built with water and provisions that become its stock - its guards' pantry, and nobody else's.
plans: defence_plan.md > The guard tower; defence_plan.md > As built: step 6, the guard tower; defence_plan.md > Build order
code: Economy.cpp: AssignJobs Needs SeekSupplyOf StoreyGoods StoreyKept EconomyPostPoint EconomyOnTower; Economy.h: ECONOMY_TOWER_GUARDS ECONOMY_TOWER_WATER ECONOMY_TOWER_PROVISIONS WORKER_JOB_GUARD; Zones.cpp: ZoneCanBuild
verified: 2026-10-10
---

Tool **T**. One plot, always both storeys: it is painted whole, and taking a storey off takes it all. It
stands by a building's rules, and also **in a wall's line**: painted on a [[Roads and walls|wall]] plot,
planned or standing, it takes that plot over, and the palisade runs up to its sides. A building stops
walkers, so the wall stays shut. Never on a road, so never on a gate.

## Building it

Besides its wood, it takes {ECONOMY_TOWER_WATER} water and {ECONOMY_TOWER_PROVISIONS}
[[Provisions|provisions]], half with each storey. What a storey takes of those it **keeps** as its stock
instead of using it up, so a finished tower holds them all. Provisions come only from builders, so no
builders' hut, no tower. Placed in debug, it stands full.

## Its guards

- {ECONOMY_TOWER_GUARDS} places once it stands whole, filled like any workplace's - by **archery** first,
  what the archery range trained, and then by [[Skills|precision]].
- A guard lives as someone [[Posts|posted]] does: out to the tower from home with his pack, and never home
  while he has the job. Each stands at his own place on the platform.
- **The tower is his pantry**: on it he eats and drinks from its stock as at home, a whole unit whenever it
  fits. His pack is only for a kind the tower has none of, so it waits while the tower has any.
- With the tower empty and his pack gone, a pressing need sends him to the nearest supply and back up.
- The tower pulled down, its guards lose the job and go home.

## Nobody else's

Only posted people draw on it. A villager in need passes towers by, even with no other water anywhere, and
no carrier, family or builder takes from one. Nobody delivers to one yet either.

## Planned

- [planned] Builders keeping towers full, ahead of their building: a tower below its full stock is to them a
  site that wants water and provisions.
- [planned] A right click on a tower posts the selected person there, if it has a free place.

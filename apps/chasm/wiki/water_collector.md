---
title: Water collector
aliases: collector, water carrier
kind: buildings
status: built
summary: A shed at the water's edge that fills with water while it has a carrier, who takes it to a store a load at a time.
plans: economy_plan.md > Production; bridge_plan.md > As built; woodcutting_plan.md > The collector's hold (the user's, 2026-10-11); stopping_work_plan.md > Decided (the user, 2026-10-12)
code: Zones.h: ZONE_KIND_WATER ZoneKindMaxStoreys ZoneKindStoppable; Economy.cpp: Tick FetchFromWorkplace SeekSupply EconomyCollectorRoom; Economy.h: WORKER_JOB_WATER
verified: 2026-10-18
---

The player drags it along the **last dry plots at the edge of a river's or a pool's wet bank**, at most {ZONE_BUILDING_MAX_PLOTS:#plots}, and its pipe
reaches over the stones to the water. It is one storey high.

## Filling

It fills by one unit of [[Water|water]] every {ECONOMY_WATER_SECONDS:#seconds}, but only **while it has
a carrier**, and **not while snow lies on it**. The carrier's skill doesn't change how fast it fills. A
carrier sent to a [[Posts|post]] stops it filling.

It holds, uncarried, **a load ({ECONOMY_LOAD}), and {ECONOMY_WATER_ROOM} more for every unit of its floor
area past the first {ECONOMY_WATER_PUMP}**, the floor its pump takes, the way a [[Woodcutter|woodcutter's
hut]] keeps wood. So a bigger collector is a bigger buffer: it fills on while no store takes water, and
the thirsty have more to drink there. The price tag says what it would hold before it is placed.

## The carrier

The carrier's job goes to whoever scores best on ingenuity against the walk from home ([[Person|jobs]]).
Once a full load ({ECONOMY_LOAD}) is waiting and a [[Store]] has room for it, the carrier walks over from
home, picks the load up, takes it to the nearest store that takes water, and goes home again. A store
beside the collector takes only water.

Its card can **stop work** - with water plenty in store, say. Then it takes no carrier, so it **fills
no more**, and its carrier is free for other work. Once a collector stands in a settlement, worked or
stopped, the [[Camp|camp]] there draws no water of its own.

## Drinking at the source

Anyone whose [[Thirst|thirst]] is pressing may come straight to a collector for a drink. At a collector
that is filling, they wait for the next unit rather than walk on.

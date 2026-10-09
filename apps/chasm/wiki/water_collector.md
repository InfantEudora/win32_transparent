---
title: Water collector
aliases: collector, water carrier
kind: buildings
status: built
summary: A shed at the water's edge that fills with water while it has a carrier, who takes it to a store a load at a time.
plans: economy_plan.md > Production; bridge_plan.md > As built
code: Zones.h: ZONE_KIND_WATER ZoneKindMaxStoreys; Economy.cpp: Tick FetchFromWorkplace SeekSupply; Economy.h: WORKER_JOB_WATER
verified: 2026-10-09
---

The player paints it on the **last dry plot at the edge of a river's or a pool's wet bank**, and its pipe
reaches over the stones to the water. It is one storey high.

## Filling

It fills by one unit of [[Water|water]] every {ECONOMY_WATER_SECONDS:#seconds}, up to
{ECONOMY_WATER_HOLD}, but only **while it has a carrier**, and **not while snow lies on it**. The
carrier's skill doesn't change how fast it fills. A carrier sent to a [[Posts|post]] stops it filling.

## The carrier

The carrier's job goes to whoever scores best on ingenuity against the walk from home ([[Person|jobs]]).
Once a full load ({ECONOMY_LOAD}) is waiting and a [[Store]] has room for it, the carrier walks over from
home, picks the load up, takes it to the nearest store that takes water, and goes home again. A store
beside the collector takes only water.

## Drinking at the source

Anyone whose [[Thirst|thirst]] is pressing may come straight to a collector for a drink. At a collector
that is filling, they wait for the next unit rather than walk on.

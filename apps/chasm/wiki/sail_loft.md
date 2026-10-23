---
title: Sail loft
aliases: sail lofts, sailmaker, sailmakers, tenter frame
kind: buildings
status: built
summary: Sailmakers sew canvas of hemp there, for a zeppelin's envelope - the ropewalk's twin, but its plots give it hands rather than speed.
plans: steam_plan.md > Hemp canvas and the sail loft; steam_plan.md > As built: step 4 (2026-10-23)
code: Zones.h: ZONE_KIND_SAIL_LOFT; Economy.h: WORKER_JOB_SAILMAKER ECONOMY_CANVAS_HEMP ECONOMY_CANVAS_SECONDS ECONOMY_SAIL_LOFT_PILE EconomyHempWork; Economy.cpp: MakeOfHemp AtHempWorkshop OthersMaking FetchFromWorkplace; BuildingMesh.cpp: BuildSailLoftYard TenterFrame
verified: 2026-10-23
---

Where [[Canvas|canvas]] is sewn, from [[Hemp|hemp]]. Its tool (**I**) comes with the chasm's
[[Discoveries|discovery]], with the [[Steam collector|steam collector]]'s, since canvas is a zeppelin's. It is
dragged over plots like any building, one storey high, at most {ZONE_BUILDING_MAX_PLOTS:#plots}, and built as a
[[Construction|site]].

## Its sailmakers

A sailmaker for every {ECONOMY_PLOTS_PER_WORKER:#plots} standing, by **precision** ([[Skills]]): unlike the
[[Ropewalk|ropewalk]], whose length is its speed, a bigger loft takes more hands. Each goes from home, in the
ropemaker's order:

1. With canvas waiting while its colony's stores hold less than it keeps (below), a load of it to a
   [[Store|store]] that takes it.
2. With {ECONOMY_CANVAS_HEMP} hemp at the loft, there to sew: {ECONOMY_CANVAS_SECONDS:#seconds} a canvas at
   an average pace, up to {ECONOMY_LOAD} a round.
3. Otherwise a load of hemp from a store, the [[Camp|camp's]] supplies, an empty house or a [[Crate|crate]].

They sew none past {ECONOMY_SAIL_LOFT_PILE} waiting. Several share the loft without getting in each other's
way: before one sets off, what the others are sewing or on their way to sew takes room in the pile, the hemp
they are bringing counts as come, and only one walks for a load the stores want. Its card can **stop work**.

## Hemp, wanted twice

A ropewalk and a sail loft each fetch what their own pile has room for, whoever comes first, and neither
counts what the other wants. While both have room they share the hemp; once one's pile is full, the rest goes
to the other. A colony that wants both grows more hemp, on fields that would otherwise grow food.

## Its stock

The canvas waiting there is **anyone's**, as a ropewalk's rope is. Its card sets how much canvas its colony
keeps in its stores - 0, 5, 10, 20 or 40, each [[Settlement|colony]] its own - with **make more, once** beside
it. The hemp there is the sailmakers' alone. A store beside a sail loft takes hemp and canvas.

## How it looks

Tarred planks under slate, with a window on every wall for the sewing. In its yard, on the open side most
toward the camera as a ropewalk's, a **tenter frame** faces the camera: posts and rails with a canvas
stretched on it, its widths sewn together. Beside it, a bolt of canvas for every {SAIL_CANVAS_A_BOLT} waiting,
and a sheaf of hemp leant on the wall for every canvas's worth on hand. With neither, the frame stands bare:
the loft is out of hemp.

## Planned

- [planned] Its canvas built into a zeppelin at the dock (`steam_plan.md`, step 5).

---
title: Archery range
aliases: range, ranges, archery ranges, archery, archer, archers, trainee, trainees
kind: buildings
status: built
summary: Painted on coarse cells like a field. A trainee a cell trains there full time, all year, and their archery grows toward what their precision allows.
plans: range_plan.md > Decided; range_plan.md > As built; range_plan.md > Later
code: Economy.cpp: GoTrain EconomyCellCost AssignJobs; Economy.h: WORKER_JOB_ARCHER EconomyArcheryCap ARCHERY_FULL; Zones.h: ZONE_KIND_RANGE ZoneKindOnCells
tuning: ARCHERY_PER_TICK
verified: 2026-10-10
---

Archers are the first people the colony trains, and they train here. A range is painted over **coarse
cells**, each on flat ground, as a [[Field|field]] is, and buildings keep a plot away from it. Standing, it
is fenced, trodden earth with a target at each cell's north side and hay bales behind: every range shoots
north.

## Building it

Placed in play it is a [[Construction|building site]], staked out, at {ECONOMY_RANGE_WOOD_PER_CELL}
[[Wood|wood]] and {ECONOMY_RANGE_WHEAT_PER_CELL} wheat a cell (the bales' straw, [[Food]]), every cell built
at once. The price is high on purpose: a range is not an early building.

## Trainees

**A trainee for each built cell**, filled like any workplace ([[Person|jobs]]): by **precision**
([[Skills]]) against the walk from home. Training is the trainee's whole job.

From home, each walks to a cell of their own (the built cells are shared out by id, one each), stands on its
line facing the target and trains for {ECONOMY_TRAIN_SECONDS:#seconds}, then goes home, and out again. Arrows
gather in the target through the stint. Training goes on all year, in winter and under the snow.

## Archery

Everyone has an **archery** score, from nothing up to what their precision allows: a tenth of the whole for
each point of precision (`EconomyArcheryCap`). Each tick on the line adds to it at the trainee's precision
pace. It is kept when they stop training.

A [[Guard tower|guard tower's]] places go to the best archers first, then by precision.

## On screen

The range's card counts its cells, a trainee a cell. A trainee's badge shows a bow, a person's card their
archery against their cap, and the overview's ARCHERS row how many places are filled. A standing range with
nobody training is listed in NEEDS ATTENTION as NOBODY AT THE RANGE.

## Planned

- [planned] Trainees who keep their trade and train part of their time - a change to jobs of its own.
- [idea] Shooting at something, and what archery does there.
- [idea] An order buildings come in, perhaps a tech tree; until then a range's price says it comes later.

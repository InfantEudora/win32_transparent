---
title: Stone
aliases: stones, rubble, rubble pile, rubble piles, pile of rubble, piles of rubble
kind: goods
status: partial
summary: A building material - the stonecutter's hut and the mine are built with it. It comes as rubble fallen from the chasm's walls, and from the rocks a stonecutter breaks; both lie as piles, used and carried off like any crate.
plans: stone_plan.md > Decided (the user, 2026-10-14); stone_plan.md > 1. Stone, and rubble at the foot of the wall; stone_plan.md > As built: step 1 (2026-10-14); stone_plan.md > As built: step 2 (2026-10-14); stone_plan.md > As built: step 3 (2026-10-14); stone_plan.md > As built: the third round (2026-10-15); stone_plan.md > As built: step 5 (2026-10-15); building_size_plan.md > Decided (the user, 2026-10-18)
code: Goods.h: GOOD_STONE GOOD_FLOATSTONE; Economy.h: CRATE_RUBBLE CRATE_BROKEN ECONOMY_STONECUTTER_STONE ECONOMY_HIGH_STOREY ECONOMY_HIGH_STONE_PER_AREA; Economy.cpp: PlaceRubble BreakRock EconomyCrateGround
verified: 2026-10-18
---

A **material**, as [[Wood|wood]] and [[Rope|rope]] are: something built with, once. A
[[Stonecutter|stonecutter's hut]] takes {ECONOMY_STONECUTTER_STONE} a plot, with its wood, a [[Mine|mine]] {ECONOMY_MINE_STONE}, and every storey from
storey {ECONOMY_HIGH_STOREY} up - a [[House|house's]] or a [[Store|store's]] - {ECONOMY_HIGH_STONE_PER_AREA} a unit of its floor area ([[Construction]]). Every [[Store|store]]
takes it, and STONE shows in the top bar once the stores hold some.

## Where it comes from

- **Rubble fallen from the chasm's walls**: piles laid out with a new colony from the world's seed, so a map
  always has the same ones - the colony's first stone, before it has a stonecutter.
- **Rocks broken by a [[Stonecutter|stonecutter]]**: each rock's stone is left as a pile where it stood.
- **A [[Mine|mine]]** at the mountain's foot or on an outcrop: without end, carried to a store a load at a time.

Fallen rubble lies **only down on the [[Terrain|balconies]]**, at the foot of their walls - none on the rim - on
about one plot in {= 1 / RUBBLE_WALL_CHANCE} there, {RUBBLE_WALL_STONE_MIN} to {RUBBLE_WALL_STONE_MAX} stone a pile,
against the wall. So the first stone needs a [[Winch|winch]] down, and with it the stonecutter's hut. Where a
floatstone seam runs, the piles along its foot are its crystal instead ([[Floatstone|floatstone]]). Only on open
ground, never under the mountain; the islands have none: they wait for the zeppelins.

## A pile of rubble

**A pile of rubble is a [[Crate|crate]]** holding stone, and is used the way crates are:

- a [[Construction|building site]] takes stone off a pile as it would from a store;
- the [[Builder|builders]] carry the piles on their [[Settlement|settlement's]] land to a store, as the last
  of their errands - a pile further out stays where it lies, unless the player puts it **PICK UP** on its
  card: then a builder, or anyone without a job, fetches it before building, from anywhere they can walk to and
  back without water ([[Crate|PICK UP]]);
- a stonecutter takes the piles within his reach first, before he breaks another rock;
- nothing is built over one, and a pile is gone once it is empty.

Clicked, its card says "Rubble", what it holds, and that it fell from the chasm's walls or was broken from a
rock. It is drawn as a pile of broken stones, not as a crate.

## Planned

- [idea] Walls and other buildings of stone.

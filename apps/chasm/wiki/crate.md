---
title: Crate
aliases: crates, find, finds
kind: goods
status: partial
summary: Goods lying on the ground - the load of someone sent to a post - a store villagers take from, and builders carry off to a store.
plans: crates_plan.md > Decided; crates_plan.md > As built: step 1; crates_plan.md > As built: step 2; crates_plan.md > As built: step 3
code: Economy.h: EconomyCrate EconomyCrateGround EconomyCrateInTheWay ECONOMY_STORE_CRATE; Economy.cpp: Order DropLoad AddToCrate LoseCoveredCrates SpawnCrate MoveCrate SourceStock; ApplicationChasmSelect.cpp: CrateCard PLAY_PICK_CRATE
verified: 2026-10-11
---

A crate is goods lying on the ground. One appears when the player sends someone carrying a load to a
[[Posts|post]]: the load is put down the moment the order is given. And one is left where a [[Camp|camp]] stood
when its last tent goes, holding its supplies and pantry ("A camp's goods").

## Where the load goes

1. Standing at a [[Store]] that takes it, the load goes into the store, as far as the store has room.
2. Otherwise onto the plot they stand on, if that is open ground; else onto the nearest open plot within
   {ECONOMY_CRATE_RINGS} steps, stepping only across ground people walk over (open ground, the
   [[Camp|camp]] among its tents, a standing [[Bridge|bridge]]) and on their own level.

**Open ground** is a dry plot off the mountain with nothing on it: no building, no garden, lot or wall, no
[[Field|field]] over it. A [[Road|road]] is open ground. **One crate to a plot**: a load put down where a
crate lies goes into it.

## Taking from it

A crate is a store that villagers only take from: nothing is ever delivered to one, and it is gone once
empty. People take from it wherever they would take from a store - goods for
[[Construction|building sites]], what a house [[Fetching home|fetches]], and a pressing [[Thirst|thirst]]
or hunger - standing beside it, by the same nearest-first rules.

## Moving it

The [[Builder|builders]], as caretakers, carry crates to a store - the **last** of their errands, after
building and road work - a load ({ECONOMY_LOAD}) at a time, and only to a store that takes the whole load.
Nobody without a job moves one, so with no builders' hut a crate is used where it lies.

## Building over it

In play mode nothing may be built over a crate - a building, a garden, a lot, a wall, a field or a range:
"a crate lies there", and the ghost shows red. A road may run under it. A debug placement goes ahead, and
the crate is lost.

## Seeing it

A small wooden crate, the same whatever it holds. **Clicked** in play mode, it is outlined and its card
says what it holds (striped where someone is already on the way to take it), who dropped it and when - or
when it was found - and everyone on the way to it and what for, pinned on the map. In debug mode the panel
shows the crate on the selected plot and can put one down there; `chasm_economy` lists every crate.

## Planned

- [proposed] A FETCH FIRST button on that card, and a badge over each crate on the map.
- [planned] Anyone holding a load that nobody takes puts it down as a crate, and so do the dead.
- [planned] A few crates placed on the map from the seed, under the clouds, found by exploring.

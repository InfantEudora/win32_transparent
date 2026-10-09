---
title: Crate
aliases: crates, find, finds
kind: goods
status: partial
summary: Goods left lying on the ground - today, the load of someone sent to a post. Nothing takes from a crate yet.
plans: crates_plan.md > Decided; crates_plan.md > As built: step 1
code: Economy.h: EconomyCrate EconomyCrateGround; Economy.cpp: Order DropLoad AddToCrate LoseCoveredCrates SpawnCrate
verified: 2026-10-09
---

A crate is goods left lying on the ground. Today one appears when the player sends someone who is
carrying a load to a [[Posts|post]]: the load is put down the moment the order is given, before they set
off.

## Where the load goes

1. Standing at a [[Store]] that takes it, the load goes into the store, as far as the store has room.
2. Otherwise onto the plot they stand on, at their feet, if that is open ground.
3. Otherwise onto the nearest open plot within {ECONOMY_CRATE_RINGS} steps, stepping only across ground
   people walk over (open ground, the [[Camp|camp]] among its tents, a standing [[Bridge|bridge]]) and
   on their own level - so never behind a [[Wall|wall]], past a house or down a cliff.

**Open ground** is a dry plot off the mountain with nothing on it: no building (the camp and a bridge
count), no garden, lot or wall, planned or built, and no [[Field|field]] over it. A [[Road|road]] is open
ground.

**One crate to a plot**: a load put down where a crate lies goes into that crate.

A crate keeps whatever it is given, with no room limit, and nothing in it spoils. A crate whose plot is
built over is lost; only the debug tools can do that today.

## Seeing it

A small wooden crate, the same whatever it holds. In debug mode the panel shows the crate on the selected
plot and can put one down there; `chasm_economy` lists every crate, and its `crate` op puts one down.

## Planned

- [planned] Villagers take from a crate wherever they take from a store - wood for building sites, what a
  house fetches, a pressing need - and nobody ever delivers to one. It is gone when empty.
- [planned] The caretakers ([[Builder|builders]]) move crates to a store, the last of their errands.
- [planned] Building over a crate is refused in play mode, with a message.
- [planned] A card for a crate when it is clicked.
- [proposed] A FETCH FIRST button on that card, and a badge over each crate on the map.
- [planned] Anyone holding a load that nobody takes puts it down as a crate, and so do the dead.
- [planned] A few crates placed on the map from the seed, under the clouds, found by exploring.

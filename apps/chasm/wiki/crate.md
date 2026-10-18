---
title: Crate
aliases: crates, find, finds
kind: goods
status: partial
summary: Goods lying on the ground - the load of someone sent to a post, or one nobody would take - a store villagers take from, and builders carry off to a store.
plans: crates_plan.md > Decided; crates_plan.md > As built: step 1; crates_plan.md > As built: step 2; crates_plan.md > As built: step 3; crates_plan.md > As built: step 5, dropping instead of holding (2026-10-18); stone_plan.md > As built: step 1 (2026-10-14); stone_plan.md > As built: step 2 (2026-10-14); stone_plan.md > As built: PICK UP by anyone free (2026-10-18)
code: Economy.h: EconomyCrate EconomyCrateGround EconomyCrateInTheWay ECONOMY_STORE_CRATE CRATE_RUBBLE CRATE_SITE ECONOMY_PICK_UP_WATER; Economy.cpp: Order DropLoad AddToCrate LoseCoveredCrates SpawnCrate MoveCrate SetPickUp SourceStock PlaceRubble Derive; ApplicationChasmSelect.cpp: CrateCard PLAY_PICK_CRATE
verified: 2026-10-18
---

A crate is goods lying on the ground. One appears when the player sends someone carrying a load to a [[Posts|post]]:
the load is put down the moment the order is given. **Nobody holds a load nothing takes**: with no [[Store|store]] or
woodpile for it, it is put down where they stand - a [[Construction|site's]] carriers' when it is cancelled, a
farmer's when the house is full, the dead's. One is left where a [[Camp|camp]] stood when its last tent goes, holding
its supplies and pantry ("A camp's goods"), and where a [[Construction|site]] was taken off, holding what was brought
to it ("A site's goods"). **Rubble** is a crate too: piles of [[Stone|stone]] laid out with a new colony at the foot
of the balconies' walls, or left where a [[Stonecutter|stonecutter]] broke a rock - drawn as broken stones, not a
crate - and along a seam's foot, piles of its [[Floatstone|floatstone]], drawn as broken crystals: "Floatstone".

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
They fetch only crates on their own [[Settlement|settlement's]] land; nobody without a job moves one unasked.

**PICK UP**, a chip on the card, puts a crate ahead of building for **anyone free**: a builder after the guard
towers and provisions, and anyone without a job. From **anywhere** they can walk to, as the order is the player's -
though to a store in their own settlement. Pressed again, it goes back in the queue. With nobody free - no builders,
everyone in a job or on a post - the card says so; a crate under the clouds has none.

But **not further than they can walk without water**, with a wide margin: a crate whose walk there and on to the
store - by the way they would go, a [[Winch|winch]] and roads counted - takes more than
{= ECONOMY_PICK_UP_WATER * NEEDS_WATER_FULL / CALENDAR_DAY_TICKS:#days} is turned down, and its card says it is too
far to fetch. A store nearer to it brings it in reach.

## Building over it

In play mode nothing may be built over a crate - a building, a garden, a lot, a wall, a field or a range: "a crate
lies there", and the ghost shows red. A road may run under it. A debug placement goes ahead, and the crate is lost.

## Seeing it

A small wooden crate, the same whatever it holds. **Clicked** in play mode, it is outlined and its card
says what it holds (striped where someone is already on the way to take it), who dropped it and when - or
when it was found - and everyone on the way to it and what for, pinned on the map. In debug mode the panel
shows the crate on the selected plot and can put one down there; `chasm_economy` lists every crate.

## Planned

- [proposed] A badge over each crate on the map.
- [idea] PICK UP for many at once - a box over a balcony's piles.
- [planned] A few crates placed on the map from the seed, under the clouds, found by exploring.

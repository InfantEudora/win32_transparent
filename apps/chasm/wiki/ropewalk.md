---
title: Ropewalk
aliases: ropewalks, ropemaker, ropemakers
kind: buildings
status: built
summary: A long low shed where a ropemaker makes rope from hemp. It works from 2 plots, and a 3rd and 4th make it faster.
plans: goods_plan.md > Decided; goods_plan.md > The ropewalk; goods_plan.md > Step 3: the ropewalk; goods_plan.md > The ropewalk's props; goods_plan.md > Kept in stock, and made to order
code: Zones.h: ZONE_KIND_ROPEWALK ZONE_ROPEWALK_MAX_PLOTS ZoneKindMaxPlots; Economy.h: ECONOMY_ROPE_HEMP ECONOMY_ROPE_SECONDS ECONOMY_ROPEWALK_MIN_PLOTS ECONOMY_ROPEWALK_PILE ECONOMY_KEPT_START; Economy.cpp: MakeOfHemp AtHempWorkshop EconomyRopewalkPace WorkplaceMakes FetchFromWorkplace KeptShort; BuildingMesh.cpp: BuildRopewalkYard
verified: 2026-10-23
---

Where [[Rope|rope]] is made, from [[Hemp|hemp]]. It is dragged over plots like any building (key K), one
storey high, at most {ZONE_ROPEWALK_MAX_PLOTS:#plots} - a longer drag goes on with another - and built as a [[Construction|site]].

## Its length

- **It works from {ECONOMY_ROPEWALK_MIN_PLOTS:#plots}.** A click lays one plot, so a one-plot
  ropewalk can be placed, but it takes no ropemaker: its card says it is too short, and NEEDS ATTENTION
  lists it. A ropewalk still being built is not too short, only unfinished.
- **Longer is faster**, for the same one ropemaker: rope is made at x1 on 2 plots, x1.5 on 3 and x2 on 4.
  Only plots that stand count.

## The ropemaker

One place, filled like any workplace, by **precision** ([[Skills]]). From home, in this order:

1. With rope waiting at the ropewalk while the stores hold less than the colony keeps (below), and a
   [[Store|store]] that takes it, he carries what they lack there, up to {ECONOMY_LOAD} at a time.
2. With {ECONOMY_ROPE_HEMP} hemp at the ropewalk, he goes there and makes rope:
   {ECONOMY_ROPE_SECONDS:#seconds} a rope at an average pace and on 2 plots, up to {ECONOMY_LOAD} a round.
3. Otherwise he fetches a load of hemp from a store, the [[Camp|camp's]] supplies, an empty house or a
   [[Crate|crate]].

He makes none past {ECONOMY_ROPEWALK_PILE} waiting there. Unlike [[Provisions|provisions]], he makes rope
whether or not anything wants it - the pile is the bound - so a ropewalk with hemp keeps its pile full. A
[[Sail loft|sail loft]] wants hemp too, by the same round: each fetches what its own pile has room for.
Its card can **stop work**: it takes no ropemaker then, and makes nothing, until it is set working again.

## Kept in the stores

Its card sets **how much rope its colony keeps** in its stores and camp's supplies: 0, 5, 10, 20 or 40 - each
[[Settlement|colony]] its own number, shown on every ropewalk of it. Only that much goes to its stores; the rest
waits at the ropewalk, up to its pile, and then he stops. It starts at {ECONOMY_KEPT_START}, so by default no rope goes to a store and
the stores never fill with it: sites take theirs from the pile.

**Make more, once**: 5 or 10 more than the stores hold now, or than the kept number if that is higher, as for
provisions - with how far it has got, and Cancel, on the card.

## Its stock

The rope waiting there is **anyone's**: carriers for a building site take it as from a woodcutter's
[[Woodcutter|woodpile]]. The hemp there is the ropemaker's alone. A store beside a ropewalk takes hemp and
rope, so a farmer's hemp ends up next to it.

## How it looks

Plaster under thatch, and beside it what makes it a ropewalk, all on open ground:

- **The walk**: three strands of rope on T-posts, waist-high, along each two of its plots side by side - so
  a longer ropewalk has a longer walk, and a one-plot one has none. It runs on the side toward the camera
  where both sides are open.
- **The yard**, on the open side most toward the camera: a big spinning wheel, a **coil** of rope for every
  2 rope waiting there, stacked, and a **sheaf** of hemp leant on the wall for every rope's worth of hemp on
  hand. So a full pile and a ropewalk out of hemp both show from afar.

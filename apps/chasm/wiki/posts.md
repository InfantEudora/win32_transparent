---
title: Posts
aliases: post, order, orders, pack, posted
kind: people
status: partial
summary: The player can order a grown-up to stand on a plot. It becomes their job while it lasts, and they live off a pack they take along.
plans: defence_plan.md > As built: step 1, orders; defence_plan.md > As built: step 2, the pack; defence_plan.md > As built: step 5, provisions; defence_plan.md > As built: step 6, the guard tower; defence_plan.md > Build order; defence_plan.md > Scouting, provisioned first
code: Economy.cpp: Order TakePost GoToPost FillPack Unpack Needs Gather GatherSupplies; Economy.h: EconomyCanOrder NEEDS_PACK_WATER NEEDS_SCOUT_WATER NEEDS_SCOUT_FOOD ECONOMY_GATHER_STOPS
verified: 2026-10-11
---

## Ordering

A [[Guard tower|guard]] is posted by his job instead: his post is his tower, and he lives on it as below,
with the tower as his pantry. An order to him comes first and suspends the job like any other.

With a person selected, a right click on a plot sends them there; their card releases them. An order is
refused for a child, onto a plot with a building, a wall or a shut gate on it (a camp and a standing
bridge count as open ground), and onto a plot they can't walk to. A right click on a [[Guard tower|guard
tower]] instead makes them one of its guards, by hand.

## On the post

- They drop what they were doing at once. A load they carried is put down as a [[Crate|crate]]; a storey
  they were building stands with what it has.
- They stand on the post: no going home, no errands, no building.
- A job they had is **kept** but stands idle, so a [[Water collector|collector]] doesn't fill and their
  share of a [[Field|field]] doesn't grow. On release they go back to it. Someone jobless gets no job until
  released.
- Their needs work as anyone's: a pressing one sends them to the nearest supply, their own house counting
  as one, then back to the post. Any food will do, [[Provisions|provisions]] too, nearest first. If cut
  off, they wait and try again.

## The pack

They take water ({NEEDS_PACK_WATER} units) and food (worth {NEEDS_PACK_FOOD:days} of
[[Health|health]]) with them, filled from home when they set out and topped up at every supply trip -
provisions first where there are any, since one is the whole pack's food. They eat from it as at home, so a
pressing need only comes once it's empty. Released, they live on it on the way home too - a scout called back
from far out walks home on what he carried. Home, what is left goes into the pantry, but provisions, which stay
packed: no home holds them.

## Scouting: gathering supplies first

A posted person's card has a **Gather supplies first** chip under Release. Pressed, they first fill their pack as
full as they can carry - {NEEDS_SCOUT_WATER} water and food worth {NEEDS_SCOUT_FOOD:days} of health, about two weeks
out - and then go on to the post. Whether just ordered, on the way or standing there.

- **From wherever a pressing need would send them**: their home, a [[Store|store]], the [[Camp|camp's]] supplies, a
  [[Water collector|collector]], a [[Field|field's]] harvest or a [[Crate|crate]] - the nearest first, and
  [[Provisions|provisions]] first where there are any. Someone setting out from home fills it there.
- **A second stop** ({ECONOMY_GATHER_STOPS} places in all) for what the first lacked - water at the camp, say, then
  provisions at a store. With something but not everything to be had, they take what there is.
- **With nothing to be had**, they go on to the post as they were.
- The chip is lit while they are at it, and their card says "gathering supplies". Pressed again, they go straight
  on - turning for the post at once, unless a pressing need takes them to the supply anyway.

Provisions are in the stores only if the colony keeps some: see [[Provisions|provisions]].

## Planned

- [planned] **Scouting's messages** (the user's): with nothing to take when told to gather, a message says so;
  when a scout's supplies run out, another - they stay until released or until a need is pressing, as now. A
  mission is just a post, usually given to an [[Archery range|archer]].

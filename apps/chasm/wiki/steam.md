---
title: Steam
aliases: canister, canisters
kind: goods
status: partial
summary: Caught over the chasm by steam collectors and carried in canisters; an input, for filling a zeppelin's envelope.
plans: steam_plan.md > Decided (the user, 2026-10-21); steam_plan.md > Steam, the good
code: Goods.h: GOOD_STEAM; ChasmHud.cpp: HudGoodColour
verified: 2026-10-21
---

An **input** - used up rather than built with: a [[Condenser|condenser]] turns it into [[Water|water]], and it will fill a
zeppelin's envelope.

- **Where it comes from**: [[Steam collector|steam collectors]] at the edge of the chasm, more the lower they stand.
- **Where it goes**: carriers take it to a [[Store|store]], {ECONOMY_LOAD} at a time. Every store takes it unless
  told not to on its card; a store beside a steam collector takes nothing else.
- **It keeps**: steam in a store stays there, as stone does.
- Carried and stored in **canisters** - how it is held, not a good of its own: there is nothing to make first.

The top bar shows it among the inputs once the colony holds some.

## Planned

- [planned] Filling a zeppelin's envelope, the last thing its site waits for (`steam_plan.md`).
- [planned] A boiler making it from water and wood.
- [idea] Steam that leaks from a store, or condenses back into water in winter's cold.

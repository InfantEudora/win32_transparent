---
title: Provisions
aliases: provision, ration, rations
kind: goods
status: partial
summary: A soldier's food, made by builders from a wheat and a beans, and only as many as something wants; villagers eat it only when nothing else is in reach.
plans: defence_plan.md > Provisions; defence_plan.md > As built: step 5, provisions
code: Economy.cpp: EconomyFoodWorth ProvisionsLacking MakeProvisions AtWorkshop SeekSupply FillPack Consume; Economy.h: ECONOMY_PROVISION_SECONDS; Goods.h: GOODS_EATEN
verified: 2026-10-09
---

A [[Food|food]] of its own: one is made from a wheat and a beans and is worth the two together in
[[Health|health]] (`EconomyFoodWorth`). It is kept apart from the crops on purpose: nobody fetches it home
and no house keeps it.

## Who eats it

- **Someone on a [[Posts|post]]**: any food, the nearest place first, provisions included; and provisions
  go into the pack first, where one is the whole pack's food.
- **A villager**: only as a last resort. A hungry villager looks for crops first; only if no place he can
  get to has any does he go for provisions. With wheat across the map, he walks past them to the wheat.
- **Wherever anyone eats**, provisions go last: greens, wheat, beans, then provisions.
- **Nobody eats at a [[Construction|builders' hut]]**: what lies there is for the sites.

## Making them

Only **builders** make them, and only as many as are **wanted**: what building sites still lack of them,
less what lies made in a [[Store|store]], the [[Camp|camp's]] supplies, an empty house, a builders' hut or
a [[Crate|crate]], and what is being made. Before his building work, a builder with some wanted:

1. with a wheat and a beans at his hut, goes there and makes them, {ECONOMY_PROVISION_SECONDS:#seconds}
   each at his pace ([[Skills|strength]]), up to a load a round;
2. else fetches a load of the scarcer of wheat and beans to his hut, from a store, the camp or a crate -
   only as many as are still wanted, and none at all if the other half can't be had.

Sites' carriers take finished provisions from the hut as from a store. When nothing wants what a hut holds
any more, a builder takes it to a store that takes it. Stores take provisions by default; their card has a
chip for it.

No building wants provisions yet, so none are made in play today.

## Planned

- [planned] The guard tower, built with provisions and water and stocked with them for its guards
  ([[Posts]]).
- [planned] Builders keeping the towers full, ahead of making provisions for sites.
- [planned] Keeping long, when spoilage comes.

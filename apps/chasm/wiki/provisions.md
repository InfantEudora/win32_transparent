---
title: Provisions
aliases: provision, ration, rations
kind: goods
status: partial
summary: A soldier's food, made by builders from a wheat and a beans - as many as something wants, and as many as the colony keeps; villagers eat it only when nothing else is in reach.
plans: defence_plan.md > Provisions; defence_plan.md > As built: step 5, provisions; defence_plan.md > As built: step 6, the guard tower; defence_plan.md > As built: step 7, the towers kept; goods_plan.md > Kept in stock, and made to order; settlements_plan.md > As built: kept stock by colony
code: Economy.cpp: EconomyFoodWorth ProvisionsLacking MakeProvisions AtWorkshop SeekSupply FillPack Consume SetKept OrderMore KeptShort; Economy.h: ECONOMY_PROVISION_SECONDS ECONOMY_KEPT_START GOODS_MADE EconomyKeptLevel EconomyKept; Goods.h: GOODS_EATEN
verified: 2026-10-24
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

Only **builders** make them, and only as many as their colony **wants**: what its building sites, guard towers and
moored zeppelins' cabins ([[Passengers]]) still lack of them, and what it keeps (below), less what lies made in a [[Store|store]], the [[Camp|camp's]] supplies,
an empty house, a builders' hut or a [[Crate|crate]], and what is being made. Before his building work, a builder
with some wanted:

1. with a wheat and a beans at his hut, goes there and makes them, {ECONOMY_PROVISION_SECONDS:#seconds}
   each at his pace ([[Skills|strength]]), up to a load a round;
2. else fetches a load of the scarcer of wheat and beans to his hut, from a store, the camp or a crate -
   only as many as are still wanted, and none at all if the other half can't be had.

Sites' carriers take finished provisions from the hut as from a store. What a hut holds beyond what the
sites still lack - the kept ones - a builder takes to a store that takes it, and its wheat and beans too once
nothing wants any. Stores take provisions by default; their card has a chip for it.

## Kept in the stores

A builders' hut's card sets **how many its colony keeps**: 0, 5, 10, 20 or 40 provisions in its stores and
camp's supplies - each [[Settlement|colony]] its own number, so every hut of one colony shows the same. Its
builders make them whenever its stores hold fewer, so what a posted person takes along is made again; another
colony's stores never count, as its people can't take from them. It starts at {ECONOMY_KEPT_START} in every
colony, a new one too: none are made unasked, since each costs a wheat and a beans.

**Make more, once**: 5 or 10 more than the stores hold now, or than the kept number if that is higher. The card
shows how far the order has got, with a Cancel chip; once the stores hold that many it is done, and the kept
number counts again. A second order while one runs adds to it.

What wants them today is the [[Guard tower|guard tower]]: its site is built with them and keeps them as
its guards' food, and a tower that stands is kept full of them by the builders. Nobody but someone posted
eats from a tower.

## Planned

- [planned] Keeping long, when spoilage comes.

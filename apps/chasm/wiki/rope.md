---
title: Rope
aliases: ropes
kind: goods
status: built
summary: A building material made from hemp at a ropewalk. A winch is built with it, and later the zeppelin will be.
plans: goods_plan.md > Decided; goods_plan.md > Rope as a cost; goods_plan.md > Step 3: the ropewalk; goods_plan.md > Step 4: rope as a cost; goods_plan.md > Kept in stock, and made to order
code: Goods.h: GOOD_ROPE GoodCategory; Economy.h: ECONOMY_WINCH_ROPE; Economy.cpp: StoreyGoods EconomyRopeChain EconomyGoodHeld
verified: 2026-10-10
---

A **material**: something built with, once, as [[Wood|wood]] is. It is made at a [[Ropewalk]] from
[[Hemp|hemp]], {ECONOMY_ROPE_HEMP} hemp a rope, and the colony starts with none.

It waits at the ropewalk for whoever needs it, and goes to a [[Store|store]] only as far as the colony keeps
some there - a number set on the ropewalk's card, none to start with. Every store takes rope by default, and its
card has a chip for it. Nobody eats it, and no house keeps it.

## What is built with it

- A [[Winch|winch]]: {ECONOMY_WINCH_ROPE} rope a storey, with its wood. Since the colony starts with none, a
  winch site stands staked out, its wood brought, until rope has been made - its card says what the colony
  still lacks to make some, and NEEDS ATTENTION lists it as WINCH: NEEDS ROPE, pointing at the first thing
  missing: GROW HEMP (a click opens a field's card, to choose hemp on it - or takes up the field tool with no
  field), then BUILD A ROPEWALK. Once a field grows hemp and a ropewalk is placed, the row goes: the rope is
  coming, and the ropewalk's own rows say if it cannot work.
- The zeppelin, later.

ROPE shows in the top bar, next to WOOD, once the colony holds some in its stores or a site still wants it -
so a winch site waiting for rope shows ROPE 0, and a colony that has never needed any is not shown it.

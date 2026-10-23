---
title: Canvas
aliases: canvases, sailcloth, bolt of canvas, bolts of canvas
kind: goods
status: built
summary: A building material sewn from hemp at a sail loft, for a zeppelin's envelope. Made to the loft's pile, and kept in the stores only as far as its colony says.
plans: steam_plan.md > Decided (the user, 2026-10-21); steam_plan.md > Hemp canvas and the sail loft
code: Goods.h: GOOD_CANVAS GoodCategory; Economy.h: GOODS_MADE ECONOMY_CANVAS_HEMP EconomyMadeAt; ChasmHud.cpp: HudGoodColour
verified: 2026-10-23
---

A **material**: something built with, once, as [[Rope|rope]] is. It is sewn at a [[Sail loft|sail loft]] from
[[Hemp|hemp]], {ECONOMY_CANVAS_HEMP} hemp a canvas, and the colony starts with none. Undyed, a pale buff.

It waits at the loft for whoever needs it, up to {ECONOMY_SAIL_LOFT_PILE}, and goes to a [[Store|store]] only
as far as its colony keeps some there: a number set on the loft's card, each [[Settlement|colony]] its own,
none to start with, with an order to **make more, once** beside it. Every store takes canvas by default, and
its card has a chip for it. Nobody eats it, and no house keeps it.

It shows in the top bar among the materials once the colony holds some in its stores.

A [[Zeppelin|zeppelin's]] envelope is sewn of it: {ECONOMY_ZEPPELIN_CANVAS} for its fourth stage - what replaced
leather as the zeppelin's material (the user, 2026-10-21).

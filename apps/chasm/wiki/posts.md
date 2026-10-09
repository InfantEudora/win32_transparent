---
title: Posts
aliases: post, order, orders, pack, posted, guard
kind: people
status: partial
summary: The player can order a grown-up to stand on a plot. It becomes their job while it lasts, and they live off a pack they take along.
plans: defence_plan.md > As built: step 1, orders; defence_plan.md > As built: step 2, the pack; defence_plan.md > As built: step 5, provisions; defence_plan.md > Build order
code: Economy.cpp: Order TakePost GoToPost FillPack Unpack; Economy.h: EconomyCanOrder NEEDS_PACK_WATER
verified: 2026-10-09
---

## Ordering

With a person selected, a right click on a plot sends them there; their card releases them. An order is
refused for a child, onto a plot with a building, a wall or a shut gate on it (a camp and a standing
bridge count as open ground), and onto a plot they can't walk to.

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
pressing need only comes once it's empty. Back home after release, what is left goes into the pantry, but
provisions, which stay packed: no home holds them.

## Planned

- [planned] Guard towers, stocked with provisions and water for posted people; a right click on a tower
  posts the selected person there.
- [planned] Builders as caretakers keeping the towers full (they already make [[Provisions|provisions]]).

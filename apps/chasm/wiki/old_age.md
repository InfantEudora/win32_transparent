---
title: Old age
aliases: old, the old, retired, grew old, died of old age
kind: people
status: built
summary: At 36 a person stops working, runs the house's errands and walks near home, and dies of age at home some years on.
plans: lifeline_plan.md > Old; lifeline_plan.md > As built: step 4, old age (2026-10-21)
code: Economy.h: LIFE_OLD_TICKS LIFE_DIES_FROM LIFE_DIES_SPAN WORKER_JOB_OLD NEEDS_DIED_OLD_AGE EVENT_GREW_OLD EconomyLifespan EconomyCanOrder; Economy.cpp: AssignJobs Needs FetchHome GoPlay GoesWith
verified: 2026-10-21
---

At {LIFE_OLD_TICKS:years} a [[Person|person]] is **old** and stops working ([[Lifelines]]): their place goes to someone
else on that tick, and whatever they were doing they go home - a load on them to a [[Store|store]] first, a
[[Guard tower|guard]] down from the tower, someone sent out released from their [[Posts|post]]. The notice line says
so.

## What the old do

- **The house's errands**, like anyone without a job - and ahead of those with one, who leave them to whoever is home
  and free ([[Fetching home]]).
- **A short walk** near home now and then, as the children play.
- **Nothing else**: no work, no [[Construction|building sites]], no [[Road|roads]]. They eat and drink a whole share.

The old are never sent out - an order is refused, "too old" - and never fight back or shoot, though a
[[Zombie|zombie]] still bites them. They do not pair off, nor have children. A household that moves to another
[[Settlement|settlement]] leaves its old behind; its children go with their parents.

## Dying of it

Everyone has a day to die of old age: {LIFE_DIES_FROM:years}, and up to {LIFE_DIES_SPAN:years} on, from a hash of the
world and the person - never shown on a card. They die the first time they are home once that day has come, never
out on an errand: indoors, so there is no body, only the angel rising over the roof. The colony records the cause as
old age.

## Seeing it

The settlers come at 24 to 32, so in a game nobody grows old for the first few hours. The save `old_age_test` has it
sooner: a woodcutter turns old a day and a half in, an old woman lives on, and an old man dies three days in.
`apps/chasm/tools/chasm_old_age_test.py` checks it all from that save.

## Planned

- [planned] An old Expert teaching at the [[Archery range|archery range]]: trainees there gain archery faster.
- [proposed] The old going along with their household when it moves, as children do.

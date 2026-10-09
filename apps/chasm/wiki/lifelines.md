---
title: Lifelines
aliases: age, ages, birth, births, baby, babies, child, children, couple, couples, expecting
kind: people
status: partial
summary: People are born, grow up a stage at a time, and couples have children, with one chance a year each.
plans: lifeline_plan.md > The stages; lifeline_plan.md > As built: step 1; lifeline_plan.md > As built: step 2, births
code: Economy.h: EconomyStageAt EconomyCoupleDay WORKER_JOB_CARING; Economy.cpp: Lifelines ChancesOfChildren MakeSettlers
verified: 2026-10-09
---

Every [[Person|person]] has a birthday and is a year older on it each game year ([[Seasons]]). The
settlers arrive as three families, each a couple with grown children and some with a child; one settler
mother is already expecting.

## Stages

| Stage | Until age | What they do |
|---|---|---|
| Baby | {LIFE_BABY_TICKS:years} | Indoors in its mother's care. Does nothing, and never goes for food or water itself: its family's [[Fetching home|fetching]] feeds it |
| Child | {LIFE_CHILD_TICKS:years} | Goes out to play near home now and then. No job, no errands, no building |
| Young adult | {LIFE_YOUNG_TICKS:years} | Works like anyone |
| Adult | | Works, and may have children |

When a child grows up, jobs are handed out again on that tick, so a free place is taken at once.

## Births

A couple has **one chance a year**, at dawn on the couple's own day of the year. It only comes when:

- they live in a [[House|house]] of their own (not the camp), and it was warm that day ([[Cold]]);
- neither is short of food or water, both are adults, and neither is {LIFE_OLD_TICKS:years} or older;
- she is not already expecting or caring, and has had fewer than {LIFE_CHILDREN_MOST} children, the dead
  counted.

The chance is {LIFE_BIRTH_ROOM}% with a place to spare in the house, {LIFE_BIRTH_FULL}% in a full one.
When it comes, she is **expecting** for {LIFE_EXPECTING_TICKS:seasons} and works on. The child is born
the first time she is home after it is due.

For the baby's first year the mother is **caring**: her job goes to someone else at once, and she stays
home, but she runs the house's errands like anyone without a job. On the baby's first birthday, or if it
dies, she is free for a job again.

## Planned

- [planned] Couples: grown people of two families pairing up and moving into a free house as a new family.
- [planned] Old age: stopping work, going for walks, still running errands, and dying of it.
- [planned] Crowding: a house holding more than its room costs everyone in it. Births are what cause it.
- Skills passed on from the parents, and growing with work, are on [[Skills]].

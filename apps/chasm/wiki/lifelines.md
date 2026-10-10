---
title: Lifelines
aliases: age, ages, birth, births, baby, babies, child, children, couple, couples, expecting
kind: people
status: partial
summary: People are born, grow up a stage at a time, and couples have children, with one chance a year each.
plans: lifeline_plan.md > The stages; lifeline_plan.md > As built: shares; lifeline_plan.md > As built: step 1; lifeline_plan.md > As built: step 2, births; lifeline_plan.md > As built: step 3, couples
code: Economy.h: EconomyStageAt EconomyCoupleDay WORKER_JOB_CARING LIFE_COUPLE_ROOM EconomyShare; Economy.cpp: Lifelines ChancesOfChildren MakeSettlers HouseFamilies EconomyRelated EconomyCoupleSurname
verified: 2026-10-10
---

Every [[Person|person]] has a birthday and is a year older on it each game year ([[Seasons]]). The
settlers arrive as three families, each a couple with grown children and some with a child; one settler
mother is already expecting.

## Stages

| Stage | Until age | Eats and drinks | What they do |
|---|---|---|---|
| Baby | {LIFE_BABY_TICKS:years} | {LIFE_SHARE_BABY} quarters of an adult's | Indoors in its mother's care. Does nothing, and never goes for food or water itself: its family's [[Fetching home|fetching]] feeds it |
| Child | {LIFE_CHILD_TICKS:years} | {LIFE_SHARE_CHILD} quarters | Goes out to play near home now and then. No job, no errands, no building |
| Young adult | {LIFE_YOUNG_TICKS:years} | a whole share | Works like anyone |
| Adult | | a whole share | Works, and may have children |

A house's fetching and its fair share of food count each mouth at its share, so a family with a baby fetches
for half a person more, not a whole one. Its room counts heads: a baby still takes a place.

When a child grows up, jobs are handed out again on that tick, so a free place is taken at once.

## Couples

A **couple** forms when two grown people move into a free [[House|house]] together and start a new family
there. Families that need a house get first pick; any house still empty that holds {LIFE_COUPLE_ROOM} goes
to a couple:

- an adult man and an adult woman, both single, from different families;
- not related: neither is the other's parent or grandparent, and they share no parent or grandparent;
- the eldest single adult first, with the eldest who fits, into the smallest empty house both can walk to.

This is checked whenever a house is finished, someone dies, or someone turns adult, so an empty house is
taken the moment a pair exists. Everyone keeps their own surname. A couple's children take one of the two,
and the house goes by that name ("the Wever family's"). Someone whose partner has died stays single.

So a new house is a new couple, and a new couple brings children.

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

- [planned] Old age: stopping work, going for walks, still running errands, and dying of it.
- [planned] Crowding: a house holding more than its room costs everyone in it. Births are what cause it.
- Skills passed on from the parents, and growing with work, are on [[Skills]].

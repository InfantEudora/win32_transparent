---
title: Thirst
aliases: water bar, dry
kind: needs
status: built
summary: A water bar that runs dry in {NEEDS_WATER_FULL:days}. Once dry, a person loses health much faster and food stops helping.
plans: needs_plan.md > Decided; needs_plan.md > As built: pressing needs; needs_plan.md > Current tuning
code: Economy.h: EconomyPressing EconomyDry; Economy.cpp: Needs Consume SeekSupply HomeShort; ApplicationChasmSelect.cpp: THIRST_MARK_WATER DrawPersonBadge
verified: 2026-10-09
---

Every [[Person|person]] has a water bar. It is full at {NEEDS_WATER_FULL:days} and drops by one every tick.

## Drinking

A unit of [[Water|water]] puts {NEEDS_WATER_WORTH:days} back on the bar, and is drunk only when all of
it fits, so nothing is wasted. Water comes before food.

- **At home**, from the house's own stock, or the camp's pantry and then its supplies, whenever the person is standing at home.
- **Away, when thirst is pressing**: with less than {NEEDS_PRESSING_WATER:days} left and none at home,
  the person finishes what they are doing, takes on no new work, and goes to the nearest place with water
  to spare: a [[Store]], the [[Camp]], a [[Water collector]] or a [[Crate|crate]], but never a house somebody lives in. A drink
  away from home takes {ECONOMY_MEAL_SECONDS:#seconds}.

## Dry

With the bar empty a person is **dry**. [[Health]] then drains faster, by {NEEDS_DRY_DRAIN} more each
tick on top of the usual one, and food no longer raises it, so eating can't outrun thirst. A person who
dies while dry dies **of thirst**.

## What keeps it from happening

- A house holding less than {ECONOMY_HOME_DAYS:#days} of what its family drinks sends someone to
  [[Fetching home|fetch]] a load. Water goes first when it is the scarcer.
- A [[Water collector]] fills while it has a carrier, but not while snow lies on it.

## On screen

Once thirst is pressing, the person's badge shows a drop instead of the job icon; the ring turns red when
they are dry. A house with thirsty people inside shows a drop on its roof.

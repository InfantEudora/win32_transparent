---
title: Health
aliases: hunger, food bar
kind: needs
status: built
summary: Health is the food meter. It lasts {NEEDS_HEALTH_FULL:days} from full, food puts it back, and at zero the person dies.
plans: needs_plan.md > Decided; needs_plan.md > The model; needs_plan.md > Current tuning
code: Economy.h: NEEDS_HEALTH_FULL EconomyFoodWorth EconomyPressing; Economy.cpp: Needs Consume EatAndDrink SeekSupply; ApplicationChasmSelect.cpp: BADGE_LOW_HEALTH
verified: 2026-10-09
---

Every [[Person|person]] has a health bar, and there is no separate hunger bar. Health drops by one every
tick, and food is what puts it back. From full, it lasts {NEEDS_HEALTH_FULL:days} with nothing to eat.

## What drains it

- One every tick, always.
- {NEEDS_DRY_DRAIN} more each tick while **dry** ([[Thirst]]).
- {NEEDS_COLD_DRAIN} more each tick while **cold**, which happens when a home under the snow had no
  firewood to burn at dawn ([[Cold]]).

## Eating

[[Food]] comes in whole units, and a food is worth the health it gives: **beans the most, wheat less,
greens the least** (`EconomyFoodWorth`). A unit is eaten only when all of it fits under full, so nothing
is wasted. Of the foods that fit, the one that keeps worst goes first: greens, then wheat, then beans.

- **At home**, from the house's own stock, or the camp's larder and then its supplies.
- **Away, when hunger is pressing**: with health below {NEEDS_PRESSING_HEALTH:days} and no food at home,
  the person finishes what they are doing and goes to the nearest [[Store]], the [[Camp]] or a
  [[Field|field's]] harvest. A meal away from home takes {ECONOMY_MEAL_SECONDS:#seconds}.
- **Not while dry.** Food does nothing for someone who is dry, so they look for water instead.
- **On a [[Posts|post]]**, from the pack they took with them.

## Death

At zero health a person dies. The colony records the cause: **thirst** if they were dry, **cold** if they
were cold, and **hunger** otherwise. Their family keeps the house, and their job is given out again on
the same tick. Nobody arrives to replace them, so starving is the first way to lose.

## On screen

The ring around a person's badge shows their health. It turns amber below
{BADGE_LOW_HEALTH:days}.

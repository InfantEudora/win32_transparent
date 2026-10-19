---
title: Health
aliases: hunger, food bar
kind: needs
status: built
summary: Health is the food meter. It lasts {NEEDS_HEALTH_FULL:days} from full, food puts it back, and at zero the person dies.
plans: needs_plan.md > Decided; needs_plan.md > The model; needs_plan.md > Current tuning; needs_plan.md > As built: step 5, deaths told; zombies_plan.md > Step 2: meals, and a fight
code: Economy.h: NEEDS_HEALTH_FULL EconomyFoodWorth EconomyPressing EconomyMealOn NEEDS_DIED_ZOMBIE; Economy.cpp: Needs Consume NextMeal MealTick EatAndDrink EatingAtHome SeekSupply; Zombies.cpp: ZombieNear Fights; ApplicationChasmSelect.cpp: BADGE_LOW_HEALTH
verified: 2026-10-19
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

**A unit takes {ECONOMY_MEAL_SECONDS:#seconds} to eat**, whatever it is: taken from the stock as the meal starts,
its health coming in a tick at a time, and the next unit only once it is done. The person's card shows it -
"Eating beans" and a bar. **No meal starts with a [[Zombie|zombie]] within {NEEDS_MEAL_SAFE}** of them, and one
under way waits while it is - unless they are indoors, or up on a tower, where nothing reaches them.

- **At home**, from the house's own stock, or the camp's pantry and then its supplies - and they eat their fill
  of it before they go out again, unit by unit.
- **Away, when hunger is pressing**: with health below {NEEDS_PRESSING_HEALTH:days} and no food at home,
  the person finishes what they are doing and goes to the nearest [[Store]], the [[Camp]], a
  [[Field|field's]] harvest or a [[Crate|crate]], and stays there for their meals, unit by unit, while more fits.
  With a zombie near, they wait there for it to go.
- **Not while dry.** Food does nothing for someone who is dry, so they look for water instead.
- **On a [[Posts|post]]**, from the pack they took with them.

## Death

A [[Zombie|zombie's]] bite takes {ZOMBIE_BITE:days} of health, so a fight is fought with whatever health a
person has: a hungry one goes down sooner.

At zero health a person dies, and falls ([[The fallen]]). The colony records the cause: **a zombie** if a bite took the last of it,
whatever there was to eat; else **thirst** if they were dry, **cold** if they were cold, and **hunger**
otherwise. Their family keeps the house, and their job is given out again on the same tick. Nobody arrives
to replace them, so starving is the first way to lose.

In play, a death is told in red at the bottom of the screen for a few seconds, for example "JELLE DE WIT
DIED OF THIRST", or "WAS KILLED BY A ZOMBIE". When nobody is left, "THE COLONY IS LOST" stays on screen.

## On screen

The ring around a person's badge shows their health. It turns amber below
{BADGE_LOW_HEALTH:days}.

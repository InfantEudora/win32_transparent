---
title: Exploration
aliases: clouds, explored, sight, fog
kind: world
status: built
summary: The map starts under clouds. People clear them by looking around as they go, and nothing can be placed under them in play.
plans: play_mode_plan.md
code: Exploration.h: EXPLORATION_CELL SIGHT_RANGE; Exploration.cpp; Economy.cpp: Order
verified: 2026-10-10
---

A new map starts with nothing explored. The settlers at the [[Camp|camp]] look around on the first tick,
and from then on **everyone out of doors looks around** as they go, clearing the clouds wherever they can
see. Explored ground stays explored.

## What they see

Sight is a **line of sight** over the ground, not a circle:

- On level ground they see {SIGHT_RANGE} world units, and further from higher up: {SIGHT_RANGE_PER_HEIGHT}
  more for every unit they stand above what they look at, up to {SIGHT_RANGE_MAX}.
- A hill or a step of [[Terrain|terrain]] looks out over lower ground; a cliff hides what is on top of it
  from below.
- From the rim they see across the chasm from its very edge, and from a balcony at the balcony's height.

Nothing is cleared around what is placed or built: a road planned at the clouds' edge shows what lies past
it only once its builders walk out and look.

## The rule

A building, field, road or anything else placed **in play** under unexplored ground is refused: "under
the clouds". Debug tools are not held to it.

An **order** is not held to it: a right click can send someone onto a plot under the clouds, and they
clear it as they walk. That is the only way to scout on purpose ([[Posts]]).

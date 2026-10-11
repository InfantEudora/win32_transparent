---
title: Tree
aliases: forest, log, logs, stump
kind: world
status: built
summary: The forest is the colony's only source of new wood. A felled tree leaves a log, then a stump, and nothing grows back yet.
plans: economy_plan.md > Production; seasons_plan.md; woodcutting_plan.md > Decided (the user, 2026-10-11)
code: Economy.h: PROP_STATE_STANDING PROP_STATE_LOG PROP_STATE_STUMP; Economy.cpp: WoodNear RoadTrees EconomyPropCovered EconomyTreeSite; Forest.cpp
verified: 2026-10-11
---

The forest grows from the world's seed along with the rest of the map. Every tree in it has a state of
its own, saved with the game:

- **Standing**: can be felled.
- **Log**: felled, with its log lying where it fell.
- **Stump**: the log has been taken away. This is permanent, for now.

A tree's log is worth what its woodcutter's workplace makes of it: {ECONOMY_CAMP_LOG_WOOD} to
{ECONOMY_LOFT_LOG_WOOD} [[Wood|wood]] ([[Woodcutter]]).

## Who fells them

Only a [[Woodcutter]]: first a tree in the way of a planned [[Road]] or [[Wall]], or on a building, field,
garden or lot still being built, then a log lying near the hut, then the nearest standing tree. So a
forest is cleared only as fast as its woodcutters work, and building at its edge is quicker than in it.

A tree under something the player has **built** (a building's standing storey, a garden or lot, a field)
is as good as gone: nobody fells it or waits for it. Under a [[Construction|site]] it stands until it is
felled, and the site waits for it.

## Through the year

Broadleaf trees change colour through the [[Seasons|seasons]] one tree at a time, and stand bare in
winter. This is only a look: a tree gives the same wood all year.

## Planned

- [idea] Trees growing back, and a forester who plants them.

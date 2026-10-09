---
title: Tree
aliases: forest, log, logs, stump
kind: world
status: built
summary: The forest is the colony's only source of new wood. A felled tree leaves a log, then a stump, and nothing grows back yet.
plans: economy_plan.md > Production; seasons_plan.md
code: Economy.h: PROP_STATE_STANDING PROP_STATE_LOG PROP_STATE_STUMP; Economy.cpp: WoodNear RoadTrees EconomyPropCovered; Forest.cpp
verified: 2026-10-09
---

The forest grows from the world's seed along with the rest of the map. Every tree in it has a state of
its own, saved with the game:

- **Standing**: can be felled.
- **Log**: felled, with its log lying where it fell.
- **Stump**: the log has been taken away. This is permanent, for now.

A tree is worth {ECONOMY_WOOD_PER_TREE} [[Wood|wood]].

## Who fells them

- A [[Woodcutter]], for the wood: logs lying near the hut first, then the nearest standing tree.
- Anyone clearing a planned [[Road]] or [[Wall]], to make way. They leave the log lying for a woodcutter
  to fetch.

A tree under something the player has painted (a building, a garden or lot, a field) is as good as gone:
nobody fells it or waits for it.

## Through the year

Broadleaf trees change colour through the [[Seasons|seasons]] one tree at a time, and stand bare in
winter. This is only a look: a tree gives the same wood all year.

## Planned

- [idea] Trees growing back, and a forester who plants them.

---
title: Roads and walls
aliases: road, roads, wall, walls, palisade, gate, gates, gate door
kind: buildings
status: built
summary: Drawn as lines and built by people without a job. Roads cost only work and double walking speed; walls cost wood too; a road through a wall is a gate whose door can be shut.
plans: line_works_plan.md > As built; defence_plan.md > As built: step 3, gate doors
code: Economy.cpp: FindRoadWork LineJobs RoadTrees RaiseWall; Walkers.h: WALKER_SPEED_ROAD; Zones.h: ZoneShutToWalkers
verified: 2026-10-09
---

Roads and walls are drawn as a line in play, and each plot of the line is a site until it's built. A
road never shares a plot with a building, and stops at the edge of a garden.

## Building them

When someone without a job finds no wood to carry, they go out to the nearest piece of a planned line that
nobody else is working on:

1. **Trees in the way first**, felled at their pace ([[Skills|strength]]) and left lying as logs for a
   [[Woodcutter]] to fetch.
2. **A road plot** is then laid: {ECONOMY_LAY_SECONDS:#seconds} at an average pace, and no wood.
3. **A wall plot** waits until its trees are gone, then takes {ECONOMY_WALL_WOOD} [[Wood|wood]], carried
   like any [[Construction|site's]], and goes up when it is all there.

After each piece they take the next one along the line within {ECONOMY_ROAD_REACH} world units, up to
{ECONOMY_ROAD_STINT} pieces a trip, then go home. Lines share one queue with building sites, by walk and
placing order, but a line can't be marked Build first.

## Walking

People walk {WALKER_SPEED_ROAD} world units a second on a road, {WALKER_SPEED_GROUND} on open ground,
{WALKER_SPEED_FIELD} through a [[Field|field]] and {WALKER_SPEED_SWAMP} in the swamp. Where the way is
clear they walk straight; otherwise they follow the grid, choosing the quickest way.

## Walls and gates

- Nobody walks onto or through a standing wall plot.
- A road crossing a wall makes a **gate**, the only way through.
- A gate's door can be **shut** from its card. Then nobody walks through, the colony's own people
  included. Everyone walking re-plans, and whoever is cut off waits until it opens. Nobody can be ordered
  onto a shut gate ([[Posts]]).

---
title: Roads and walls
aliases: road, roads, wall, walls, palisade, gate, gates, gate door
kind: buildings
status: built
summary: Drawn as lines, cleared by woodcutters and built by people without a job. Roads cost only work and double walking speed; walls cost wood too; a road through a wall is a gate whose door can be shut.
plans: line_works_plan.md > As built; zombies_plan.md > Step 4: walls break; zombies_plan.md > As built: step 4d, the look; woodcutting_plan.md > Decided (the user, 2026-10-11); defence_plan.md > As built: step 3, gate doors; arches_plan.md > As built: step 1, the owner; arches_plan.md > As built: step 2, the floor; arches_plan.md > As built: step 3, built as a site; arches_plan.md > As built: step 4, losing it
code: Economy.cpp: FindRoadWork LineJobs RoadTrees RaiseWall; Walkers.h: WALKER_SPEED_ROAD; Zones.h: ZoneShutToWalkers ZoneArchStoreys ZoneArchOwner ZoneKindHasBody; RoadMesh.cpp: RoadCellFilled EdgeDressing EdgeBesideFill CellInTown BuildCobbles HarmedLog BuildGate; ZoneMesh.h: ZoneHarmLook
verified: 2026-10-23
---

Roads and walls are drawn as a line in play, and each plot of the line is a site until it's built. A
road never shares a plot with a building, and stops at the edge of a garden. Roads laid side by side make one
closed area - a cell with laid road on all four corners is road all over - and small dark stones and tufts of grass lie here and there along a road's edges. A road between grass is worn lighter down its middle, and a closed area beside a building is cobbled.

## Building them

1. **Trees in the way** are felled by a [[Woodcutter]] alone, the first he goes for within his reach.
   A line with a tree no woodcutter reaches waits, and NEEDS ATTENTION says so.
2. **A road plot** with nothing of its way standing is then laid by someone without a job who finds no
   wood to carry: {ECONOMY_LAY_SECONDS:#seconds} at an average pace, and no wood. After each plot they take
   the next along the line within {ECONOMY_ROAD_REACH} world units, up to {ECONOMY_ROAD_STINT} a trip.
3. **A wall plot** waits until its trees are gone, then takes {ECONOMY_WALL_WOOD} [[Wood|wood]], carried
   like any [[Construction|site's]], and goes up when it is all there.

Lines share one queue with building sites, by walk and placing order, but a line can't be marked Build first.

## Walking

People walk {WALKER_SPEED_ROAD} world units a second on a road, {WALKER_SPEED_GROUND} on open ground,
{WALKER_SPEED_FIELD} through a [[Field|field]] and {WALKER_SPEED_SWAMP} in the swamp. Where the way is
clear they walk straight; otherwise they follow the grid, choosing the quickest way.

## Walls and gates

- Nobody walks onto or through a standing wall plot.
- A road crossing a wall makes a **gate**, the only way through. Never under a built arch (see Arches).
- A gate's door can be **shut** from its card. Then nobody walks through, the colony's own people
  included. Everyone walking re-plans, and whoever is cut off waits until it opens. Nobody can be ordered
  onto a shut gate ([[Posts]]).
- [[Zombie|Zombies]] **break** them: a palisade plot in {ZOMBIE_BREAK_WALL:#seconds} of hitting, its logs leaning away and
  dropping as it goes, a shut gate's door in {ZOMBIE_BREAK_GATE:#seconds}, giving on its hinges. A broken plot lies in
  fallen logs and stumps, a wall site again until built; a broken door hangs askew and won't shut.
- Each built wall or gate plot grows its [[Settlement|settlement's]] land {ECONOMY_SETTLEMENT_WALL} units of walking
  round itself, so a wall carries the border with it. A road doesn't.

## Arches

A road between buildings of the **same kind** is built over: their upper storeys bridge it over a passage a storey high,
at most three such road plots in a row, never round an open corner. Its floor is its owner's storey over the passage -
one on a [[Terrace|terrace]] a storey up bridges it with its ground floor - up to the lowest side's eaves. The owner is the
building on both sides, else the one with most plots beside it (the older on a tie): drawn in its look, selected with
it. A house never bridges into a store, nothing bridges to a guard tower, and no arch goes over a gate.

Its storeys over the passage are that building's **floor**: a [[House|house]]'s places, pantry room and
firewood, a [[Store|store]]'s room, a builders' hut's builders. They are **built** like the building's own
storeys ([[Construction]]): once both sides stand to its height, its floor is part of the building's site, at
what a storey of that building costs there, with **scaffolding over the road** until it stands. The road stays
open under it. So a storey added beside a road can bring an arch's floor after it.

**Losing it.** If a side loses a storey, or the road under it is erased, the arch's floor comes down with it at
once, and its wood is lost, as with any storey taken off. The building keeps its goods, even if it now holds more
than its room. A house can be left [[House|crowded]]. In play the message line says "THE ARCH OVER THE ROAD CAME
DOWN WITH IT". Building more never takes an arch down: an arch with a floor built stays even when tall buildings
make its row longer than three, and only new arches are held to that. A wall can't be drawn across a road with an
arch standing over it, so the wall has a gap there until the arch is taken down first.
- [planned] A gatehouse between two [[Tower|guard towers]] over a gate.

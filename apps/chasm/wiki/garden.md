---
title: Garden
aliases: gardens, hanging garden, hanging gardens, cypress, cypresses
kind: buildings
status: built
summary: A yard of lawn behind a hedged stone wall. It keeps the broadleaf trees it was painted over, is planted with flowers, bushes and cypresses, and hangs down a terrace's wall.
plans: gardens_plan.md > Decided (the user, 2026-10-26); gardens_plan.md > The design; gardens_plan.md > As built (2026-10-26)
code: Economy.h: EconomyGardenKeeps EconomyPropSite EconomyTreesOn; Forest.h: ForestGardenKeeps; Gardens.h: GardenStands GardenQuadPlants GardenQuadCypress; MeshBuild.h: MeshCypress; TerraceMesh.cpp: BuildTerraceFace TERRACE_GARDEN_DRAPE; BoundaryMesh.cpp: BuildBoundaryCell
verified: 2026-10-26
---

A garden is one of the two kinds of [[Yard|yard]]: who owns it, how it is built and its wall and gate are on that
page. This one is about what grows in it.

## The trees it keeps

- **A garden keeps** its broadleaf trees - oaks, willows, palms - and its bushes, flowers and ferns, from the moment it
  is painted. Its site does not wait for them, and no [[Woodcutter|woodcutter]] fells them while it is there - not even
  one already on his way.
- **Its pines are felled first**, as on any [[Construction|site]]: only they show on its card and price tag.
- **Erase the garden** and its trees are ordinary trees again.
- **No fruit** once it stands: nobody goes into a walled garden to pick it ([[Fruit]]).
- **On a [[Terrace|terrace]]** it keeps nothing: the terrace was built up over whatever grew there.

A [[Tree|tree's]] stump or log lying on it is hidden once it stands, as under any building.

## Its planting

Drawn, never saved: decided by its place, like the forest.

- **Flowers** along the inside of its walls, in spring and summer - ferns in the swamp, shrubs in the desert.
- **A bush or a shrub** here and there once it is more than one plot - small, clipped ones - clear of a tree it kept.
- **A cypress** in the middle of a cell the garden fills all round, at most one a large square, unless a tree it kept
  stands on one of its plots.

## A hanging garden

On a terrace its lawn ends at the capstones, and the retaining wall is its wall: no garden wall stands on top of it,
nor along the foot of a terrace's wall when the garden is below it. A clipped hedge runs behind the capstones,
creepers with flowers hang down every face under it - as far as {TERRACE_GARDEN_DRAPE} of the wall - and a cypress
stands at about every other outer corner.

## Planned

- [idea] What a garden does: food for its family - from the fruit trees and bushes it kept - or comfort.

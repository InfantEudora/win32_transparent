# Gardens: kept trees, planting, and gardens on terraces

The user's, 2026-10-26. A garden (`yards_plan.md`) has been a flat lawn behind a stone wall with a hedge on it. Three
things asked for: a garden painted over a tree should keep the tree rather than wait for a woodcutter; a large garden
should have plants in it; and a garden on a terrace looked as if it hung over the edge. Talked through the same day.

## Decided (the user, 2026-10-26)

- **A garden keeps its broadleaf trees; pines are felled as now.** No felling first for an oak, a willow or a palm, and
  never felled while the garden is there.
- **Plants in larger gardens**: flowers, bushes, and a **cypress** as the feature of a large garden.
- **On a terrace, the "hanging garden" look**: no garden wall along the terrace's own edge, a clipped hedge behind the
  capstones instead, creepers with flowers down every face under it, and **cypresses** at its outer corners.

## How it stands today

- A garden site with a standing tree on one of its plots is a TREE SITE (`EconomyTreeSite`): it rises only once a
  woodcutter has felled it (`SiteTreesStanding`), and woodcutters go for it first.
- Once the garden stands its plots are COVERED (`EconomyPropCovered`): the view hides every prop on them (`PropHidden`),
  woodcutters pass them by, and a fruiting plant there is gone for good (`PlantsCovered`).
- The forest puts at most ONE tree, bush or rock on a plot, within `PROP_JITTER` (0.6) of the plot's vertex, plus up to
  two pieces of ground cover. So a kept tree's trunk stands clear of a garden wall, which runs along the plot's outline
  about a unit from its vertex; its crown may lean over it.
- A garden is drawn as `PAL_BUSH` quarters laid to the cell's edge midpoints (`GroundQuarter`, ZoneMesh.cpp), walled
  along the spokes from those midpoints (`BuildBoundaryCell`), on the higher of the two sides' ground. A terrace's top and
  its retaining wall follow `BuildingWallCrossing` instead - the house n-gon. So at a terrace's edge the lawn reaches out
  past the top, and the garden's wall stands on the capstones flush with the face: two walls one on the other.

## The design

### 1. Kept trees (rules, then the view)

- **What a garden keeps** (`EconomyGardenKeeps`, the kinds in `ForestGardenKeeps`): a prop on a plot whose ground is
  GARDEN - a site or standing - if it is a broadleaf tree (the oaks, the willow, the palm) or a garden plant (the bushes,
  flowers, fern, shrub). Pines (`PROP_PINE_*`, the snow pine), grass, twigs, mushrooms, reeds, rocks and boulders are
  dealt with as now. Never on a TERRACE: it was built up over whatever grew there before a garden could be painted on it.
- **Not a tree site**: a kept tree does not hold its garden's site up, and no woodcutter is sent for it.
- **Covered from the moment the garden is painted**, site or standing: so nobody fells it for wood meanwhile. This is
  the "do not fell" mark, and it is the zone's - no new state, nothing saved. Erase the garden and it is an ordinary
  tree again.
- **Drawn**: the view keeps showing what a garden keeps. Everything else on a standing garden is hidden as now.
- **Fruit goes, as now, once the garden stands** (`PlantsCovered`). Proposed was to keep it on the tree for the garden's
  own use later, but pickers choose a plant by its fruit alone (`GoPick`, `picker_places`), not by what covers it -
  they would set out for fruit behind a garden's wall. Left for "what a garden does" (`yards_plan.md`, Later).
- **A felling under way stops**: a woodcutter who set out for a tree before a garden was painted over it leaves it
  standing and goes home (the check where the felling finishes).
- A rules change: a recording that paints a garden over a tree replays differently from here on.

### 2. Planting (the view only)

Generated, like the forest: decided by a hash of the place, never saved. Decided **per fine quad, from that quad's four
corner plots alone** - a plot's change rebuilds exactly the chunks of the quads round it (`Zones::Touch`), so a garden's
planting can never be left stale in a chunk that was not rebuilt. "Large" falls out of that: a quad whose four corners
are all garden is the inside of a garden at least two plots by two.

In each garden plot's QUARTER of a quad (a standing garden plot, no building on it):

- **A flower border** along each side of the quarter its garden is walled on - a clump or two of the forest's flowers,
  set in from the wall. In spring and summer only, as the forest's are.
- **A bush or a shrub** in a quarter's middle where the quarter has another garden plot beside it in the quad: the
  garden is at least two plots. At most ONE a quad, in about half of them, and at 0.6 of the forest's size - clipped.
  A bush in every other such quarter, at the forest's size, filled a four-plot garden with seven (seen 2026-10-26).
- Nothing in a quarter beside a **gate** (where the path comes in), nor within reach of the kept tree or bush on its plot.
- Biome: the swamp's gardens take ferns for flowers; the desert's shrubs for bushes.

And in a quad whose four corners are all standing garden: **a cypress** at the quad's middle - unless one of the four
plots has a broadleaf tree or bush of its own, whose crown would meet it. Only in ONE of each coarse cell's four quads,
chosen by a hash of the cell: one in every fully-garden quad made a big garden an orchard of them (seen 2026-10-26 on a
24-plot terrace garden). Still the quad's own decision, so nothing goes stale.

The flowers, bushes and shrubs are the forest's own meshes, drawn in the forest's per-chunk sets (`RebuildPropChunk`),
so they sway and turn with the seasons as the wild ones do. The CYPRESS is built in code - a tall faceted spindle,
`MeshCypress` - since a squeezed pine reads as a pine: drawn with the zones' chunks (ZoneMesh.cpp, TerraceMesh.cpp).

### 3. The hanging garden (the view only)

Where a garden stands on a standing terrace:

- **Its lawn follows the terrace's own outline** (`BuildingWallCrossing`), so it ends at the capstones.
- **No garden wall where a retaining wall stands between two plots** (`ZoneTerraceStep` other than open) - either way
  round: the retaining wall is the garden's wall, from above or from below. Elsewhere on the terrace its wall is as
  ever, along the crossings rather than the midpoints so it meets the lawn.
- **A clipped hedge** along the top, just behind the capstones (TerraceMesh.cpp, under the face whose high side is the
  garden), following the faces' mitres round the corners.
- **Creepers on every face under it**, not three in ten, hanging further - up to about half the wall - with flowers
  (`PAL_ACCENT`) among the leaves: the garden spilling down its wall.
- **A cypress at its outer corners**, set in behind the hedge - at about every other one, so a zig-zag edge is not a row
  of them.

Walking is unchanged: the retaining wall already shuts that edge (`ZoneTerraceStep`).

## Build order

1. Kept trees: `EconomyGardenKeeps`, the site and covered rules, `PlantsCovered`, `PropHidden`.
2. Planting: the per-quad layout (Gardens.cpp), its plants in `RebuildPropChunk`, `MeshCypress` and the feature cypress.
3. The hanging garden: the lawn's outline, the wall skipped, the hedge, the creepers, the corner cypresses.

Each checked in the game on a test copy. Because the rules and the forest's view (`Economy.*`, `ApplicationChasm.cpp`)
were held by other work on 2026-10-26, step 3 - all view, in files of its own - was built first.

## As built (2026-10-26)

All three steps.

- **Rules** (Economy.*): `EconomyGardenKeeps`, `EconomyPropSite` (in place of `EconomyTreeSite` wherever a prop is
  asked about - the site list, the woodcutter's choice and his walk over the ice, `Felled`, `SiteTreesStanding`; the
  boulders' as they were), the prop's `EconomyPropCovered` (`RoadTrees`; the woodcutter's choice asks it of a standing
  tree only, so a log lying on a new garden is still fetched), the felling's own check, and `EconomyTreesOn`'s
  `f_garden` for the garden site's card (`GroundSiteCard`) and the price tag (ApplicationChasmGhost.cpp).
- **The view**: `PropHidden` keeps what a garden keeps in sight, and `RebuildPropChunk` hides a stump or log a garden
  covers; the planting (`GardenQuadPlants`) goes into the forest's sets there. `ForestData::plot_prop` and
  `ForestGardenKeeps` (Forest.h). Gardens.h/.cpp: the per-quad layout and the cypress. `MeshCypress` (MeshBuild.h).
  The hanging garden: `GroundQuarter` (ZoneMesh.cpp), `BuildBoundaryCell` (BoundaryMesh.cpp), `BuildTerraceFace`
  (TerraceMesh.cpp).
- **Checked** (debug build, a test copy on 8771, `saves/terraces_test.json`):
  - The hanging garden: every plot of the save's top-3 terrace painted garden (debug, standing at once): the lawn to
    the capstones, no garden wall along its edges, the hedge behind them round the zig-zag's corners, every face under
    it hung with flowers; five cypresses in 24 plots, one at an outer corner.
  - A play garden over a pine (plot 8218): its card "1 tree to fell first". One over an oak (31873, a house painted
    beside it): no such line. One over two fruit bushes (31856, 31858): built in under 9,000 ticks, the bushes in it.
  - A standing garden over the oak (31873 and three plots beside it): the oak drawn inside its wall, still standing
    and bare 27,000 ticks later in winter.
  - Not seen felled: the pine, whose colony's one woodcutter works 67 units away - out of her reach, as before.
- **Tuning seen and changed**: one cypress in every fully-garden quad made an orchard - now one a coarse cell; a bush
  in every other quarter at the forest's size filled a four-plot garden with seven - now one a quad at most, at 0.6
  of the size. Drape flowers 0.075 wide were invisible - now 0.14.
- **Not checked by a run**: flowers in a garden in spring and summer (the save stood in autumn, then winter), and the
  felling's own check - a woodcutter on his way when a garden is painted over his tree.

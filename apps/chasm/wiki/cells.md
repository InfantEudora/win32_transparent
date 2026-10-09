---
title: Cells and plots
aliases: cell, cells, plot, plots, grid, coarse cell, coarse cells, fine cell, fine cells, floor area, area, storey, storeys
kind: world
status: built
summary: The ground is an irregular grid of four-sided cells. Buildings are painted on plots and fields on coarse cells, and their area decides what they hold, house and grow.
plans: grid_plan.md > 1. How the grid is made; grid_plan.md > 2. Plots and cells
code: Grid.h: GridSettings; GridPick.h: PlotArea CoarseArea; Zones.cpp: ZoneBuildingFigures
measured: PLOT = 3.3; PLOT_SMALL = 1.9; PLOT_LARGE = 4.5; CELL = 13.1
verified: 2026-10-09
---

## The grid

It is made the way Townscaper's is: a lattice of triangles, with random neighbouring pairs merged into
four-sided cells; every shape is split into four-sided **coarse cells** and relaxed toward squares, then
each is split again into four **fine cells** and relaxed again. It comes from the world's seed and is
never saved. The heights and kinds of ground on it are [[Terrain]].

- A **plot** is a corner of the fine grid with the ground around it: a quarter of each fine cell meeting
  there, nearly always four. Plots are what the player clicks and paints: [[House|houses]],
  [[Store|stores]], workshops, gardens, lots, [[Roads and walls|roads and walls]].
- A **coarse cell** is four fine cells. [[Field|Fields]] are painted on coarse cells.
- The cells are irregular, so their sizes differ. On buildable ground a plot averages about {=PLOT} area
  units, with most between {=PLOT_SMALL} and {=PLOT_LARGE}; a coarse cell averages about {=CELL}. The same
  painted shape can hold noticeably more or less depending on where it lies. (Measured 2026-10-09 on seed
  12's map of about 50,000 fine cells: plateau plots off the mountain, picked on a lattice with
  `chasm_pick` and weighted per plot.)

## What area buys

A building's **floor area** is the area of its plots times its standing storeys, and everything it holds
or houses goes by that. A field's harvest goes by the area of its built cells.

| | Per unit of area | An average plot-storey, or cell |
|---|---|---|
| [[Store]]: goods it holds | {ECONOMY_STORE_ROOM} | about {=ECONOMY_STORE_ROOM * PLOT} |
| [[House]]: food and water it keeps | {ECONOMY_HOUSE_ROOM} | about {=ECONOMY_HOUSE_ROOM * PLOT} |
| House: people | one per {ECONOMY_PERSON_AREA} | about {=PLOT / ECONOMY_PERSON_AREA} |
| [[Wood]] for a building's storey | {ECONOMY_BUILD_WOOD_PER_AREA} | about {=ECONOMY_BUILD_WOOD_PER_AREA * PLOT} |
| Wood for a garden's or lot's plot | {ECONOMY_GROUND_WOOD_PER_AREA} | about {=ECONOMY_GROUND_WOOD_PER_AREA * PLOT} |
| Wheat, a harvest | {crop_yield[ZONE_CROP_WHEAT]} | about {=crop_yield[ZONE_CROP_WHEAT] * CELL} a cell |
| Greens, a harvest | {crop_yield[ZONE_CROP_GREENS]} | about {=crop_yield[ZONE_CROP_GREENS] * CELL} a cell |
| Beans, a harvest | {crop_yield[ZONE_CROP_BEANS]} | about {=crop_yield[ZONE_CROP_BEANS] * CELL} a cell |

- A house holds **whole people only**: its floor area over {ECONOMY_PERSON_AREA}, rounded down, and never
  more than {ECONOMY_HOUSE_MOST}. A one-plot, one-storey house holds one or two, and a family of
  {ECONOMY_HOUSE_MOST} needs about {=ECONOMY_HOUSE_MOST * ECONOMY_PERSON_AREA / PLOT} plot-storeys.
- Harvests are at full yield. The crop before changes them ([[Food|rotation]]); a slow farmer only makes
  them take longer.
- A field costs {ECONOMY_FIELD_WOOD_PER_CELL} wood a cell whatever the cell's size, so large cells are the
  better buy.

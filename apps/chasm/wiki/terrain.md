---
title: Terrain
aliases: level, levels, plateau, island, islands, balcony, balconies, chasm, chasm floor, biome, biomes, mountain, swamp, desert, pocket, slope, wet, cliff, cliffs
kind: world
status: partial
summary: Ground at heights in steps (the plateau, balconies on the chasm's walls, islands in it, and the floor under the clouds), framed by biomes, with slope and water limiting what goes where.
plans: grid_plan.md > Columns; biomes_plan.md > Step 2: relief; gameplay_plan.md > Terraces; terraces_plan.md > Stage 1: foundations under buildings; water_tools_plan.md > Height painted by hand; water_tools_plan.md > Cliffs painted by hand
code: Terrain.h: TERRAIN_PLATEAU TERRAIN_BIOME_SWAMP Buildable TerrainCliffPlots; Zones.cpp: PlotIsBuildable ZONE_RISE_HOUSE ZONE_RISE_FOUNDATION ZoneLandSays ZoneNeedsFoundation BuildingFootHalves; Zones.h: ZoneKindTakesFoundation; BuildingMesh.cpp: FOOTING_DEPTH; ApplicationChasmGhost.cpp: UpdateSlopeGrid SLOPE_GRID_RADIUS; Walkers.h: WALKER_SLOPE_COST; GridWater.h: GRID_RELIEF_HIGHEST ChasmPlaceRelief GRID_CLIFF_DEEPEST ChasmPlaceCliffs
verified: 2026-10-24
---

One valley, split down the middle by the **chasm**. Every [[Cells and plots|plot]] has a height in whole
steps of {TERRAIN_STEP} world units, and its height decides its kind:

| Kind | Where | Built on |
|---|---|---|
| Plateau | the valley either side of the chasm | anything |
| Balcony | a ledge on the chasm's wall below the rim, reached by [[Winch|winch]] | anything but fields: its food comes down from the rim |
| Island | shards, columns and ledges standing in the chasm | reached by [[Flight|zeppelin]], which sets down by its edge for its [[Passengers|passengers]] to step off |
| Floor | under the clouds, {TERRAIN_FLOOR_STEPS} steps down | never |
| Bare | any flat patch smaller than {TERRAIN_MIN_PATCH} plots: crumbled rock | never |

## Biomes

- **Temperate**: most of the map.
- **Mountain**: the north. It seals the chasm off from the map's edge, and nothing walks on it or is built
  on it.
- **Pocket**: a meadow in the mountain, open and buildable, reached from one side of the chasm only.
- **Swamp**: the south, on one side. Pools between hummocks, and walking off a road is slow there
  ([[Roads and walls]]).
- **Desert**: the south, on the other side. Dunes and palms, with no rules of its own yet.

## Water and slope

- A river's water and its stone bank, out to {TERRAIN_WET_MARGIN} world units past the bank, can't be
  walked or built on. Only a [[Bridge|bridge]] crosses it, and only a [[Water collector|water collector]]
  stands at its edge.
- Ground rises gently in hills and steeply in the mountain's crags. A building's plot may rise at most
  {ZONE_RISE_HOUSE} across it, a road's {ZONE_RISE_ROAD}, a field's cell {ZONE_RISE_FIELD}. Nothing stands
  half over a cliff but a wall, which runs right up to the edge.
- **Foundations**: a building drawn as a house, not a collector, may stand on up to {ZONE_RISE_FOUNDATION} of
  rise (one storey: hills, not crags) on a stone foundation it gets by itself - its floor built up to the highest
  ground under its walls, nothing dug in, the plinth showing downhill. The ground storey then takes
  {ECONOMY_FOUNDATION_STONE} stone a unit of area and of rise, shown on the price tag. A [[Terrace|terrace]] levels it.
- **The slope grid** shows it, since gentle-looking ground often isn't: in play, with a tool in hand, every plot
  within {SLOPE_GRID_RADIUS} world units of the cursor - every cell, for a field or a range - is outlined green
  where the land lets that tool stand, amber where only on a foundation, red where not (too steep even so, by a
  cliff or the water), fading out. What is built there is the [[Construction|ghost]]'s to say. The water, the
  mountain and anything under the clouds get no grid, nor do the tools whose place is their own (collectors,
  winch, mine, bridge).
- Walking uphill is slower: speed divided by 1 + {WALKER_SLOPE_COST} x the grade.
- **Height painted by hand** (debug, for test maps): the panel's Height tool, or `chasm_generate`'s `relief`, raises
  or lowers the ground by up to {GRID_RELIEF_HIGHEST} - a hill a click, a ridge a drag - each a new map. A river
  drawn through it cuts it; lowering keeps off every drop.
- **Cliffs painted by hand** (debug, for test maps): the panel's Cliff tool, or `chasm_generate`'s `cliffs`, sets
  plots down by 1 to {GRID_CLIFF_DEEPEST} steps, drawn as a road is and up to {GRID_CLIFF_WIDEST} plots wide - each a
  new map. They are the chasm's own kind of ground: walls round them, a balcony or bare rock by size, crossed only by
  a [[Winch|winch]]. Only on the plateau's dry ground: a river keeps its own, crossing a painted terrace on its banks.

## Planned

- [planned] Floatstone seams on the islands (`flight_plan.md`, step 5).

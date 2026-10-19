---
title: Terrain
aliases: level, levels, plateau, island, islands, balcony, balconies, chasm, chasm floor, biome, biomes, mountain, swamp, desert, pocket, slope, wet
kind: world
status: partial
summary: Ground at heights in steps (the plateau, balconies on the chasm's walls, islands in it, and the floor under the clouds), framed by biomes, with slope and water limiting what goes where.
plans: grid_plan.md > Columns; biomes_plan.md > Step 2: relief; gameplay_plan.md > Terraces; water_tools_plan.md > Height painted by hand
code: Terrain.h: TERRAIN_PLATEAU TERRAIN_BIOME_SWAMP Buildable; Zones.cpp: PlotIsBuildable ZONE_RISE_HOUSE; Walkers.h: WALKER_SLOPE_COST; GridWater.h: GRID_RELIEF_HIGHEST ChasmPlaceRelief
verified: 2026-10-19
---

One valley, split down the middle by the **chasm**. Every [[Cells and plots|plot]] has a height in whole
steps of {TERRAIN_STEP} world units, and its height decides its kind:

| Kind | Where | Built on |
|---|---|---|
| Plateau | the valley either side of the chasm | anything |
| Balcony | a ledge on the chasm's wall below the rim, reached by [[Winch|winch]] | anything but fields: its food comes down from the rim |
| Island | shards, columns and ledges standing in the chasm | reached by zeppelin, not in the game yet |
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
  half over a cliff.
- Walking uphill is slower: speed divided by 1 + {WALKER_SLOPE_COST} x the grade.
- **Height painted by hand** (debug, for test maps): the panel's Height tool, or `chasm_generate`'s `relief`, raises
  or lowers the ground by up to {GRID_RELIEF_HIGHEST} - a hill a click, a ridge a drag - each a new map. A river
  drawn through it cuts it; lowering keeps off the chasm's rims.

## Planned

- [planned] Zeppelins to reach the islands.

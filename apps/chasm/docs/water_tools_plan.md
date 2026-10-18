# Wells and placed water: water for test maps

Talked through with the user 2026-10-18. The test maps (`test_maps_plan.md`) have no water: no river, so no
collector, and a test's people go dry in four days unless a store is stocked by hand. Two debug tools for it,
both for setting a scenario up:

- **A well**: a building of one plot holding water anyone may take, as much as the seed gives it; `set_stock`
  can make it endless.
- **Water placed by hand**: rivers and ponds drawn onto the map, as part of the map itself.

Step 1, the well, built and checked 2026-10-18 (as built under "The well"); steps 2-4, the water, not yet.

## Decided (the user, 2026-10-18)

- **A well starts with an amount from the seed**, not endless - so it can later become a spot of its own on the
  release game's map, there to be found and drawn dry. `set_stock` makes it endless, or sets another amount.
- **A pond is the swamp's pools**: ground pressed under the water, still water drawn on it, a narrow wet shore
  that buildings can stand close to - not a river's channel and its wide wet margin.
- **Placing water starts the map over.** Water is set at the start of a scenario, before anything is built, so
  a new map each time is fine.
- **On every kind of map**, the chasm's too: the code is the same, and on a chasm map it starts a new colony as
  on any other.

## The well

### What is there already

A supply is any building with a stock. Someone thirsty drinks at the nearest one with water to spare, barring a
few kinds (`SeekSupplyOf`: a builders' hut, a tower unless he is posted, a house somebody lives in); a house short
of water fetches it home from a list of kinds (`FetchFrom`: a store, a camp, a collector, a woodcutter's hut, a
house nobody lives in). So "anyone can take from it" needs no job and no errand of its own: a well is a building
with a stock that nobody delivers to.

### What it is

- **`ZONE_KIND_WELL`**, one plot (`ZoneKindOnePlot`), placed with a click as the mine and the tower are, on any
  plot a house could stand on. It needs no water near.
- **A debug tool**, `CHASM_TOOL_WELL`: from the panel only, no key, never in play, as `CHASM_TOOL_CASTLE` is.
  Debug paint stands at once. Erased like anything else.
- **Its water is its stock**, `stock[id][GOOD_WATER]`. A well new to the economy is filled in `FollowZones`, as a
  debug tower is, with `EconomyWellWater`: a hash of the map's seed and the well's plot, between
  `ECONOMY_WELL_WATER_MIN` and `ECONOMY_WELL_WATER_MAX`. The same plot on the same map always gives the same
  well, and a well generated with the map later gets its amount the same way.
- **Taken from as a store is.** The thirsty drink there with no change to `SeekSupplyOf`; houses fetch from it
  (`FetchFrom` gains the kind); the caretakers fill guard towers from it (`KeepTowers`' sources gain it - one
  line, and what a combat test's guards live on). No draw time: a load is picked up as at a store, and drinking
  already takes `ECONOMY_DRINK_SECONDS` a unit.
- **Not a store**: no carrier delivers to it, and the top bar's water does not count it (`EconomyStoredTotals`
  counts the stores and the camp).
- **Drawn dry**, it has nothing to offer and is passed over. It stands until erased; NEEDS ATTENTION says
  nothing, since it is a debug thing.
- **Snow does not stop it**, as it stops a collector.
- **The camp keeps its water carrier** beside a well, where a collector would take over: a well can run dry.

### Endless

- **`chasm_economy set_stock`** on a well: `contents {"water": N}` sets what is left, as on any building, and makes
  an endless well finite again; `"endless": true` makes it endless. A new value of `CHASM_CMD_STOCK`, recorded as
  `set_stock` is now.
- An endless well is put back to `ECONOMY_WELL_ENDLESS` every tick, so it is never short and the trace still
  holds its stock.
- **Saved**: the endless wells by id, written only when there is one, so a save without one is byte for byte
  as today. In the state hash.

### Seeing it

- A stand-in built in `BuildingMesh`, as the huts' and the mine's bodies are: a stone ring, two posts and a small
  roof. A modelled prop later, if wells stay.
- **Its card**: the water left, or "endless".
- `chasm_economy`'s stock reports say whether a well is endless.
- A wiki page, `wiki/well.md`, saying it is a debug tool for now.

### As built: step 1 (2026-10-18)

- **The kind**: `ZONE_KIND_WELL` 15 (`ZONE_KIND_COUNT` 16), one plot (`ZoneKindOnePlot`, which the stroke's join
  now asks instead of naming the one-plot kinds), one storey, placed where a house could stand. `Zones::Apply`
  refuses a well in a PLAY command, whatever sends it; the tool, `CHASM_TOOL_WELL`, is on the debug panel only and
  locked in play (`ToolsLocked`). `chasm_paint` takes kind `well`, `chasm_tool` takes `well`.
- **Its water**: filled in `FollowZones` as a debug tower is, by `EconomyWellWater` - a hash of the seed and the
  plot (`Mix`) into `ECONOMY_WELL_WATER_MIN`..`_MAX`. `FetchFrom` and `KeepTowers`' sources gain the kind;
  `SeekSupplyOf` needed nothing.
- **Endless**: `EconomyState::endless` by building id, put back to `ECONOMY_WELL_ENDLESS` in `Tick` right after
  `FollowZones`. `CHASM_CMD_STOCK`'s `value[3]` carries it (0 in every recording from before); `SetStock` now takes
  the zones, refuses any good but water on a well and `endless` on anything else. Saved as `"endless_wells"` only
  when there is one, and hashed only for those.
- **The look**: `BuildWell`, the ring `WELL_OFFSET` off the plot's point along a hashed yaw, so a person drinking
  stands beside it. Its card, its outline height (`SHAPE_WELL`), its names in the statistics and on the card.
- *Checked* (debug, a copy on port 8777, flat map seed 5 - its hash `dc71866ed8e62108` as before):
  - A well at 0,0 held 142. Three people put down dry walked to it and drank a unit each (142 to 139). With a house,
    a builders' hut and a tower painted, the three moved in and one became the builder: the house fetched water from
    the well, and the builder refilled the emptied tower from it (`to_fetch`, store 1).
  - Left 1 unit, two thirsty: one drank it, the other did not set out; the well stood at 0. Endless, it stayed at 40
    every 50 ticks while people drank and fetched there. Refused: wheat into a well, `endless` on a house, a well
    painted with `play` inside the settlement.
  - Saved, changed, loaded: the endless well back at 40, the world hash the same, a re-save byte for byte the save.
  - 1,500 ticks recorded (a finite amount set, a second well painted - 105 for its plot - two made thirsty, the second
    made endless), replayed twice: equal on all 1,501 ticks, ending where the run did.
  - `test_maps_plan.md` step 4, the saves: all 18 loaded in the build before (A, 8778) and this one (B), the same
    world hash in both, and the 36 files - saved at once and after 3,000 ticks - the same byte for byte.

## Water placed by hand

### What is there already

- **Water is terrain.** The layout's river lines (`ChasmLayout::rivers`) are built by `Terrain::BuildRivers` into
  a raster of how far past the water's edge each point is, and from it the channel, the banks and the per-vertex
  `wet`; `WaterMesh` lays the water on them. The swamp's pools are the other water: ground under
  `TERRAIN_WATER_Y` in the swamp is wet (`Terrain::Build`) and drawn as still water (`WaterMesh`, POOLS).
- **Everything else reads `wet`**: what may be built (`PlotIsBuildable`), the trees (`Forest.cpp`), the walkers
  (`Walkers.cpp`: no crossing but by a bridge, or the ice), where a collector stands (the last dry plots at a
  bank's or a pool's edge), the camp's water carrier, and the ice in winter. Water that gets into the terrain
  brings all of that with it.
- **The world is made from the settings alone, and a new world is a new colony.** `EnsureZonesWorld` resets the
  zones, the economy, the walkers and the exploration whenever the world changes; a load and a replay generate it
  again from the save's settings.

### The idea: placed water is part of the map, beside the kind

As `GridSettings::kind` is (`test_maps_plan.md`), so **`GridSettings::water`**: a short list of rivers and ponds.

- In `operator==`, and in the save's `"grid"` block **only when there is any**, so every save without placed
  water stays byte for byte as it is.
- **`Grid::Generate`** puts it into the layout once that is made - the chasm's, or a test kind's empty one: the
  rivers onto `layout.rivers`, the ponds into a new `layout.ponds`. The terrain reads the layout alone, as now.
- **`Grid::Hash`** already covers `layout.rivers`, and takes `layout.ponds` the same way. An empty list adds
  nothing, so every world's hash today is unchanged.

### Rivers

A placed river is a `GridRiverLine` - its points, source first, and a width - and from the layout on it is a
river like any other: channel, banks, wet plots, water with its streaks, bridges, collectors, ice. How it ends
follows from where it was drawn:

- **EDGE** where it ends at the map's edge, carried a little past it as `PlaceEdgeRiver` does; a source at the
  edge likewise comes in from past it.
- **JOIN** where it ends in another river's water: its last point put on that river's centre line, `joins`
  naming it.
- **SINK** anywhere else: a swallow hole where it stops - with no spring under it, which nothing has tried yet.
- On a chasm map, one that crosses a rim falls there, as every river does (`BuildRivers`). Untried where the
  drop is onto a balcony rather than the open floor.

This is the hand-made river that `test_maps_plan.md` step 6 left out of the title's town as untried. Once it
works, the town can have one.

### Ponds

A pond is a centre and a radius, the radius being its water's edge.

- **The ground pressed down**: a bowl in the relief, `POND_DEPTH` under the water in its middle, meeting the water
  at the radius and back up to the ground `POND_BANK` past it. The relief fades out round a pond as it does along
  a river (`RELIEF_RIVER_FADE`), so a pond on a hill still lies level. A test kind has no relief today; it gets
  a raster when it has a pond.
- **The swamp's two rules widened** from "in the swamp" to "in the swamp or a pond": ground under the water, or
  just above it (`SWAMP_SHORE`), is `wet` (`Terrain::Build`), and a cell under the water is drawn as a pool
  (`WaterMesh`). A per-vertex `Terrain::pond` says which vertices a pond reaches.
- **Only on the plateau.** A pond reaching a rim, a wall or the mountain is refused: the pools are drawn only on
  plateau cells.

### The tools

- **Two tools on the debug panel**, river and pond, never in play.
  - **River**: drawn as a road is - pressed, dragged and let go (`UpdateLineDrag`), at the panel's width; the
    preview is the line at that width.
  - **Pond**: pressed at its centre and dragged out to its radius; the preview is a circle.
- **On release** the water is added to the world's settings and the map generated again (`RegenerateGrid`). Not
  a recorded command: like Generate, it makes a new map. Refused while a recording runs.
- **The panel lists the water**, with a button to take each piece away (again a new map).
- **`chasm_generate`** takes `water` whole - `{"rivers":[{"points":[[x,z],...],"width":4}],
  "ponds":[{"x":0,"z":0,"radius":8}]}` - so a scenario script makes its map in one call, and **`chasm_grid`**
  reports it.

A 4,000-cell test map generates in about 20 ms in debug (grid 11.9, levels 2.0, mesh 4.2, forest 0.3, measured
for `test_maps_plan.md`), so a new map per pond is no wait.

### Why not keep the colony

The way to keep it would be the load path: the colony saved in memory, the water changed in its settings, and
loaded again. What stands in the way is the trees. They are known by their index in the forest, and water changes
which plots get a tree, so every felled tree, stump and fruit a save holds would land on another tree. Keeping
them means a forest placed without regard to placed water and filtered wherever it is drawn or used - every
reader of the forest. Not until a test needs it: a scenario made by a script is made again by running it.

## Steps

1. **The well**: the kind, the tool, the fill from the seed, `FetchFrom` and `KeepTowers`, endless by `set_stock`,
   saved, the card, the stand-in, the wiki page.
   *Done when*, on a flat map in debug: a well holds its seeded amount, the same each time on the same plot;
   people put down beside it drink there, a house fetches from it, and the caretakers fill a tower from it; a
   finite well runs dry and is passed over; an endless one stays full; a save and a load give the same, and a
   recording replays equal; and every save today loads and re-saves byte for byte as before (`test_maps_plan.md`
   step 4, the saves).
2. **Water in the settings, rivers first**: `GridSettings::water`, the save, `Grid::Generate`, `Grid::Hash`,
   `chasm_generate` and `chasm_grid`.
   *Done when* `chasm_generate` makes a flat map with a river from edge to edge and a stream that sinks inland:
   both drawn, their banks wet, a collector at one filling, a bridge across it; a save and a load give the same
   world hash; and the A/B in full - the seed sweep identical, every save byte for byte, the replay equal.
3. **Ponds**: `layout.ponds`, the bowl and the relief's fade, the two pool rules widened, the refusal off the
   plateau.
   *Done when* a flat map with two ponds draws still water with a shore, a collector at one fills, and a pond on a
   chasm map's hill lies level; the A/B again.
4. **The tools**: river and pond on the panel, their previews, the list.
   *Done when* a river and a pond drawn by hand make the same map as `chasm_generate` given the same water.

Step 1 and steps 2-4 do not depend on each other.

## Current tuning

| | |
|---|---|
| `ECONOMY_WELL_WATER_MIN`, `_MAX` | 100 and 200. A unit is two days of an adult's water, so ten people draw a well dry in 20 to 40 days |
| `ECONOMY_WELL_ENDLESS` | 40, the settlers' water: what an endless well is kept at |
| `POND_DEPTH` | about 1, as deep as the swamp's pools |
| `POND_BANK` | 3, the rise past the water's edge |
| A placed river's width | 4 from the panel, between a branch's 2.2-3.4 and a river's 5-7.5 |

## Later

- **A well as a spot on the release map**, generated with the map as the deposits are and holding its amount by
  the same seeded rule: found, built over, drawn dry. For the far side, say, which has no rivers.
- **Keeping the colony when water is placed** (above).

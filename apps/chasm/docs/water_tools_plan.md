# Wells and placed water: water for test maps

Talked through with the user 2026-10-18. The test maps (`test_maps_plan.md`) have no water: no river, so no
collector, and a test's people go dry in four days unless a store is stocked by hand. Two debug tools for it,
both for setting a scenario up:

- **A well**: a building of one plot holding water anyone may take, as much as the seed gives it; `set_stock`
  can make it endless.
- **Water placed by hand**: rivers and ponds drawn onto the map, as part of the map itself.

All four steps built and checked 2026-10-18 - the well, placed rivers, ponds and the panel's river and pond tools
(as built under each).

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

### As built: step 2, rivers (2026-10-18)

- **The settings**: `GridSettings::water`, a `GridWater` (`GridWater.h`) - each `GridPlacedRiver` its points and a
  width (`GRID_PLACED_RIVER_WIDTH` 4 unless given, held to 1.5..12). In `operator==`. The save's `"grid"` block
  holds it as `"water"` only when there is any, through `GridWaterToJson` / `GridWaterFromJson` (`ChasmSave.h`),
  which `chasm_generate` takes too; `chasm_grid` reports it under `settings`, and its `river_list` marks each
  river `placed`.
- **Into the layout**: `Grid::Generate` calls `ChasmPlaceRivers` (`ChasmLayout.cpp`) right after the layout is
  made - the chasm's or a test kind's empty one - so the placed rivers come after the layout's own and a chasm
  map's rivers keep their indices (their wander and lakes are seeded by index). Each is first made a course
  through its points, `GridSmoothLine` every `PLACED_RIVER_SPACING`; then its ends: a source within
  `PLACED_RIVER_EDGE` of the map's edge comes in from `PLACED_RIVER_PAST` beyond it; the first RIM it crosses makes
  it a FALL, its line ended `PLACED_RIVER_PAST` past the lip; else an end at the edge an EDGE river; else an end
  inside another river's water (its layout width) a JOIN, the last point put on that river's course; else a
  SINK. `Grid::Hash` already covered `layout.rivers`, so nothing else was needed for the world hash.
- **Nothing else changed**: `Terrain::BuildRivers` was written for hand-placed lines already (Chaikin's corners,
  ends kept), and everything past it - wet, banks, water, swallow hole, falls, bridges, collectors - reads it.
- *Checked* (debug, a copy on port 8777; A = today's tree without this step, built from a copy of the app folder
  with the step's edits undone and diffed to show nothing else):
  - Flat map seed 5 (4,000 cells, 152 x 83) with a river from the west edge to the east, a stream from the north
    edge stopping inland and a branch into the river: they came out `river_off_edge`, `stream` and `branch`
    (joins 0), 1,007 of 3,941 plots wet. From above: the river edge to edge, the branch's streaks blending in at
    the confluence, the stream ending in its swallow hole, trees off the banks.
  - A collector refused on the wet bank (3 plots), placed on the first dry one, staffed from a house nearby and
    full (5) after 1,500 ticks; a bridge across the river, 15.9 long over 9 wet plots.
  - Saved, a different map generated, loaded: the world hash back to `d2e40ff7a478d295`, a re-save byte for byte
    the save. A 1,500-tick recording on the river map, replayed twice from a chasm map: the river map remade each
    time (the same hash, its three placed rivers) and the traces equal on all 1,503 ticks.
  - On the default chasm map (seed 12, its hash `3782e0fcc63d838d` unchanged without water), a river drawn from the
    east edge westward fell at the rim: brink, sheet and streaks as the generated falls. **It fell onto a
    balcony**, the untried case, and the sheet passes through the balcony's deck: the generator never puts a fall
    there, and a placed river does not refuse it yet.
  - The A/B: `chasm_seed_sweep.py 1 60` identical line for line on A and B, no failures; all 18 saves loaded with
    the same world hash in both and the 36 files (at once, and after 3,000 ticks) the same byte for byte; 6,000
    ticks recorded on A from `village_year5`, six zombies dropped halfway, replayed on A and B - equal on all
    6,001 ticks.

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

### As built: step 3, ponds (2026-10-18)

- **The settings**: `GridWater::ponds`, each a `GridPlacedPond` - its middle and its radius to the water's edge
  (`GRID_PLACED_POND_RADIUS` 6 unless given, held to 2..30). In the JSON as `"ponds":[{"x","z","radius"}]`.
  `ChasmPlaceRivers` became `ChasmPlaceWater`: after the rivers, each pond that fits goes into `ChasmLayout::ponds`,
  hashed with the rivers (`Grid::Hash`); one that does not is left out and counted (`ponds_dropped`), and
  `chasm_grid` lists both (`pond_list`, `ponds_dropped`).
- **Fits** (`ChasmPondFits`, shared by the layout and the tool's preview): its middle and two rings out to
  `GRID_POND_BANK` plus `PLACED_POND_CLEAR` past its water inside the map, off the rift (`ChasmRiftDistance`), south
  of the mountain's foot and clear of every crack.
- **The ground**: in `BuildRelief`, after the rivers' fade, the relief fades out round each pond over
  `RELIEF_RIVER_FADE`, then the bowl is pressed in - `TERRAIN_POND_DEPTH` (1) under the water over its middle half,
  up to the water at its radius, back to the plateau over `GRID_POND_BANK` (3). A kind without hills gets a raster
  only when it has a pond, zero but for the bowls.
- **The water**: `Terrain::pond` marks the plateau plots within a pond's reach, and the swamp's two rules take "in
  the swamp or a pond" - wet under the water and `SWAMP_SHORE` above it (`pond_wet_count`), and a cell under the
  water drawn. **Drawn apart**, though (`WaterMeshData::ponds`, `water_ponds`, `SetWaterPondUniforms`): the first
  ponds came out the swamp's murky green under its haze; a pond is the rivers' clear blue at the pools' stillness
  (`WATER_POND_FLOW`), the same shader as a program of its own.
- *Checked* (debug, a copy on port 8777):
  - The flat map seed 5 with the three rivers and three ponds: two drawn, the third - against the east edge - left
    out; wet plots 1,007 to 1,098. A house refused in a pond ("too close to water"); a collector refused on its wet
    shore and placed on the first dry plot, 9 from the middle of a pond of 7, staffed from a house, full after
    1,500 ticks.
  - On the chasm map (seed 12) a pond of 7 on the plateau's highest temperate ground (11.9): its middle pressed to
    -1.57 and wet, the rings at 4 and 6 under the water, its shore at 8, the ground back at the plateau's level by
    10 and rising through the fade (about 1 at 14, 6 to 7 at 24 against 11 to 12 before) - the pond level in a
    hollow of the hill, reeds round it.
  - Saved, another map generated, loaded: the world hash the same, a re-save byte for byte the save. A 1,500-tick
    recording on the map with rivers and ponds, replayed twice from another map: rebuilt each time (its hash, three
    rivers, two ponds), the traces equal on all 1,503 ticks.

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

### As built: step 4, the tools (2026-10-18)

- **`CHASM_TOOL_RIVER` and `CHASM_TOOL_POND`**: "River (debug)" and "Pond (debug)" beside the well on the panel's Paint
  tools, locked in play (`ToolsLocked`; `chasm_tool` says "a debug tool, never in play mode"). `chasm_tool` takes
  `river` and `pond`, so a script drags them with `hover_x`/`hover_z` and `button`.
- **The drag** (`UpdateWaterDrag`, from `UpdatePaint`, the physics thread): not the road's plot chain after all but
  the cursor's own course - a point every `WATER_DRAG_STEP` (3) - since the terrain rounds a river's line itself; a
  pond's middle and the edge it is dragged to. On release `PlaceWater`: the world's settings with the water added,
  `RegenerateGrid` there and then, as the N key does; refused while `input_record` runs, a river of one point, a
  pond under `GRID_PLACED_POND_SMALLEST` or one that does not fit.
- **The preview** (the pick view): a river's middle and its two edges at the panel's width, a pond's water's edge and
  its bank, green - a pond red where it would not fit.
- **The panel** (`RenderWaterTools`, under either tool): the next river's width, each placed river (its points, its
  width, how it came to end) and pond (a note if it was left out), each with an x to take it away, "Take all the
  water away", and what the last placing came to. The Generate section's own settings keep the map's water, so
  Generate, the seed buttons and N do too, as `chasm_generate` does; New game and the title start from none.
- *Checked* (debug, a copy on port 8777, by `chasm_tool` drags):
  - A river dragged from the west edge to the east: placed as `river_off_edge` at the panel's 4, its 13 points kept;
    the preview its three lines across the map while held.
  - A pond dragged out to 7: placed, drawn the rivers' clear blue with a foam rim; one dragged against the east edge:
    its circles red, and refused on release. A pond dragged while recording: refused, the world hash the same.
  - In play mode `river`, `pond` and `well` all refused; in debug mode taken.
  - **Not seen**: the panel's water list. Its Paint section was scrolled out of the window, and ImGui cannot be
    scrolled or clicked by a script; its buttons call the same `PlaceWater` the tools do.
- **The A/B** (steps 3 and 4 together; A = today's tree without them, from a copy of the app folder with their edits
  undone, `diff -r` showing nothing else): `chasm_seed_sweep.py 1 60` identical on A and B - and to step 2's; all 18
  saves the same world hash in both and the 36 files byte for byte the same; 6,000 ticks recorded on A from
  `village_year5` with six zombies dropped, replayed on A and B - equal on all 6,001 ticks, ending on step 2's hash.

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
| `TERRAIN_POND_DEPTH` | 1, as deep as the swamp's pools |
| `GRID_POND_BANK` | 3, the rise past the water's edge |
| `GRID_PLACED_POND_RADIUS` | 6 unless given, held to 2..30 (`GRID_PLACED_POND_SMALLEST`, `_LARGEST`) |
| `GRID_PLACED_RIVER_WIDTH` | 4, the panel's to start with, between a branch's 2.2-3.4 and a river's 5-7.5; held to 1.5..12 |
| `WATER_DRAG_STEP` | 3: the river tool takes a point each time the cursor has gone this far |

## Later

- **A well as a spot on the release map**, generated with the map as the deposits are and holding its amount by
  the same seeded rule: found, built over, drawn dry. For the far side, say, which has no rivers.
- **Keeping the colony when water is placed** (above).

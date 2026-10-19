# Wells and placed water: water for test maps

Talked through with the user 2026-10-18. The test maps (`test_maps_plan.md`) have no water: no river, so no
collector, and a test's people go dry in four days unless a store is stocked by hand. Two debug tools for it,
both for setting a scenario up:

- **A well**: a building of one plot holding water anyone may take, as much as the seed gives it; `set_stock`
  can make it endless.
- **Water placed by hand**: rivers and ponds drawn onto the map, as part of the map itself.

All six steps built and checked - the well, placed rivers, ponds and the panel's river and pond tools on
2026-10-18, height and cliffs painted by hand on 2026-10-19 (as built under each).

## Decided (the user, 2026-10-18)

- **A well starts with an amount from the seed**, not endless - so it can later become a spot of its own on the
  release game's map, there to be found and drawn dry. `set_stock` makes it endless, or sets another amount.
- **A pond is the swamp's pools**: ground pressed under the water, still water drawn on it, a narrow wet shore
  that buildings can stand close to - not a river's channel and its wide wet margin.
- **Placing water starts the map over.** Water is set at the start of a scenario, before anything is built, so
  a new map each time is fine.
- **On every kind of map**, the chasm's too: the code is the same, and on a chasm map it starts a new colony as
  on any other.
- **Height painted by hand too** (2026-10-19, step 5): smooth hills only - the relief, raised and lowered - not
  cliffs, which are the chasm's levels and a far bigger thing. No flatten mode for now. A stroke's height is held
  to +-20 (the generated hills reach 14, the mountain about 50).
- **Cliffs after all** (2026-10-19, step 6), the user's idea: painted on PLOTS with the road's drag, which makes
  them the same kind of edit as a road rather than lines in the chasm's layout - the terrain is one column per plot
  already. **Down only**, 1 to 4 steps. **A winch is the only way across**, as at a balcony: no ramps or stairs, the
  rules unchanged. **A width slider**, 1 to 5 plots.

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

*Since step 5 the code below is `ApplicationChasmMapTools.cpp`'s: `UpdateWaterDrag` is `UpdateMapDrag`, `PlaceWater`
`RemakeMap`, `RenderWaterTools` `RenderMapTools`, `WATER_DRAG_STEP` `MAP_DRAG_STEP`.*

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

## Height painted by hand (step 5)

Height already matters to what a test tries: a higher post sees and shoots further and the ground between blocks a
shot (`Shooting.cpp`, the sight's raster); slopes slow walkers and shut them out past `WALKER_STEEPEST`; a house
wants its plot within `ZONE_RISE_HOUSE`. So a test map with hills has high ground, cover and choke points.

- **Strokes in the settings**, beside the water and for the same reasons - `GridSettings::relief`, each stroke its
  points, a radius and a signed height. A click is a hill (a hollow when lowered); a drag is a ridge (a valley)
  along its line, falling off smoothly to its radius. Strokes add up, so painting over a hill raises it more. In
  the save's `"grid"` block only when there are any; copied into the layout and hashed with the water.
- **In the relief, before the rivers flatten it**: a river drawn through a painted hill cuts its valley, and a pond
  still lies level. On the plateau only, as all relief is. A kind without hills gets a raster when it has a stroke.
- **Lowering keeps off the rims** of a chasm map: a deep valley at a rim would put the plateau's ground under the
  balcony below it and turn its wall inside out, so a stroke's lowering fades out near the rift. Raising does not -
  the generated hills reach the rims already.
- **The tool**: "Height (debug)" beside River and Pond, with the height and radius on the panel; its preview the
  line and its reach; on release the map is made again. The panel lists the strokes, each with an x, and has "undo
  the last stroke". `chasm_generate` takes `relief`.
- *Done when* hills and valleys painted on a flat map stand where drawn, at their heights; a river drawn through a
  hill cuts it; walkers and buildings keep to the slope rules on them; a save, a load and a replay give the same;
  and the A/B finds nothing changed on a map without strokes.

### As built: step 5 (2026-10-19)

- **The settings**: `GridSettings::relief`, a `GridRelief` of `GridReliefStroke`s (`GridWater.h`) - points, radius
  (`GRID_RELIEF_RADIUS` 12 unless given, held to 4..60) and height (6 unless given, held to `GRID_RELIEF_HIGHEST`
  20 either way). In `operator==`; the save's `"grid"` block holds `"relief":{"strokes":[...]}` only when there are
  any (`GridReliefToJson` / `FromJson`, which `chasm_generate`'s `relief` takes too); `chasm_grid` reports it.
  `ChasmPlaceRelief` copies the strokes, held to their limits, into `ChasmLayout::relief_strokes`, hashed there.
- **The relief** (`BuildRelief`): a raster whenever there are strokes; each stroke adds over the cells it reaches
  its height times `1 - SmoothStep(0, radius, distance to its line)`; lowering faded out within
  `RELIEF_PAINT_RIM_CLEAR` (16) of the rift. Then, after the generated relief's own river fade, the painted relief
  is faded along rivers too - but over `RELIEF_PAINT_RIVER_FADE` (8) past the wet margin, not the hills' 24: **found
  on the first try**, a ridge of 8 drawn across a river came out 1.3 high ten units from it, the river flattening
  most of a 150-unit map. With 8 the river cuts a valley through the ridge. Ponds after both, as before.
- **The tool**: `CHASM_TOOL_HEIGHT`, "Height (debug)" beside River and Pond, locked in play. The tools' code moved
  to `ApplicationChasmMapTools.cpp` and became the MAP tools: `UpdateMapDrag` (a click is a one-point stroke, a drag
  takes a point every `MAP_DRAG_STEP`), `RemakeMap` (the water and the relief together), `RenderMapTools` (under the
  height tool: height and radius, the strokes with an x each, "Undo the last stroke", "Take all the strokes away").
  The preview: the stroke's line and its reach at the radius, green raising, blue lowering. `chasm_tool` takes
  `width`, `height` and `radius` - the panel's sliders, for a script. The Generate section keeps the strokes as it
  keeps the water.
- *Checked* (debug, a copy on port 8777):
  - Flat map seed 5: a hill of 12 (radius 14) stood 11.7 at its top, 6.7 at half its radius, 0.15 at its foot; a
    valley of -6 at -5.7; a gentle hill of 6 (radius 20) at 6.0, 3.1 at half its radius; a ridge of 8 at 8.0 - and,
    with a river drawn across it, 6.6 ten units from the river, the river's cut through it about 0 (wet at the water).
  - A house refused on the steep hill, even at its top ("too steep": a 12-high peak of radius 14 drops about 1.4
    across a plot), taken on the gentle hill's top. A person ordered onto the steep hill's top: "no way there"; onto
    a plain on the same side, the steep hill's outer flank and the gentle hill: taken.
  - By `chasm_tool`: a click at height 10, radius 12 made a one-point hill (top 9.65); a drag at -5, radius 8 a
    five-point valley (-4.64), its preview blue while held. The user also painted strokes by hand in the test window.
  - On the default chasm map, a valley of -20 (radius 20) at a balcony's end of the rim: 2 units in from the rim
    9.12 to 9.08, 6 in 7.67 to 6.89, 12 in 4.31 to -1.0, at the stroke's middle -12 - the plateau at the rim kept.
  - Saved, another map generated, loaded: the same world hash, a re-save byte for byte the save. A recording on a
    map with rivers, three strokes and a collector, replayed twice from a chasm map: rebuilt each time, the traces
    equal on all 1,503 ticks.
  - **The A/B**: A = today's tree with step 5's map-facing edits undone in a copy - the relief, `operator==`, the
    hash, `ChasmPlaceRelief`'s call, the save's read and write - and the tools' code and the stroke type kept, so it
    builds; `diff -r` showing nothing else. `chasm_seed_sweep.py 1 60` identical on A and B and to the steps
    before; all 18 saves the same world hash and the 36 files byte for byte the same; the 6,000-tick replay from
    `village_year5` with zombies equal on all 6,001 ticks, on the same final hash as every run before.

## Cliffs painted by hand (step 6)

A cliff is not a line in the layout. Every plot already stands as a column at a whole number of steps
(`Terrain.h`, `grid_plan.md` "Columns"), and a wall stands wherever two neighbours differ; the cracked-mud map
(`MudCracks.h`) makes its whole terrain that way with no layout at all. So painting plots onto levels is an edit of
the road's kind, and nearly everything a cliff needs follows from the heights:

- **Walls**: the terrain mesh draws one, with its strata, turf and soil, wherever neighbouring plots differ.
- **Walking**: nobody crosses a step, zombies included (`Terrain::SameGround`), except by a winch.
- **Kinds** (`BuildKinds`): a lowered patch of `TERRAIN_MIN_PATCH` plots or more beside the plateau is a balcony, so
  a winch reaches it; a smaller one is bare rock.
- **Building**: a footprint stands on one height, so nothing straddles a cliff.
- **Combat**: sight and an arrow's reach go by the ground's height (`Shooting.cpp`), so a cliff's top sees and
  shoots further, and the cliff hides what is behind it.

What it needs:

- **Strokes in the settings**, beside the water and the relief: `GridSettings::cliffs`, each stroke the positions
  of the plots its drag crossed, a width in plots and a depth in steps. Positions rather than plot numbers, so a
  stroke outlives a new seed as the water does. Saved only when there are any, copied into the layout, hashed.
- **The plots a stroke takes** (`TerrainCliffPlots`): the plot nearest each point - on the grid it was painted on,
  its chain exactly - and past a width of one every plot within half the width of the line through them. The
  preview and the terrain ask the same function.
- **In the heights** (`Terrain::BuildHeights`), after every generated one and before they settle: each stroke sets
  its plots to its depth, a later stroke over an earlier one; depth 0 takes painted plots back up. Only plots the
  generator left at the plateau's height, dry, off the map's edge, the mountain and a pond's reach - so the chasm's
  own plots, its crumbling rim and its cracks stay, and a river keeps its ground: a terrace painted across one stops
  at its wet margin, and the river crosses on its own banks.
- **Kept from the pits' filling**: the terrain fills every sunken piece but the biggest, for the cracks' stray ends;
  a piece with a painted plot in it is there on purpose.
- **The road's drag**: plots joined along fine edges (`LineStepTo`). Two lowered plots meeting only at a corner are
  a saddle, which the terrain joins back up (`SettleHeights`) - a diagonal moat drawn plot by plot would leak there.
- **Lowering keeps off every drop**: step 5's guard measured from the rift; a valley painted beside a painted cliff,
  or a crack, could sink the plateau below the ground under it the same way. It now fades height painted down within
  `RELIEF_PAINT_RIM_CLEAR` of the foot of every drop, measured from the plots below the plateau beside it.
- **The tool**: "Cliff (debug)" beside Height, locked in play: a road's drag, its band drawn plot by plot - blue going
  down, green taken back up, red not taken - and before the press the brush under the cursor. Depth (0..4) and width
  (1..5) on the panel, the strokes listed with an x each, "Undo the last stroke". `chasm_generate` takes `cliffs`;
  `chasm_tool` takes `depth`, and `width` as plots with the cliff tool in hand.
- *Done when* cliffs painted on a flat map stand at their depths with walls round them, a one-plot trench stops
  walkers and a winch takes them down, a river keeps its ground through a painted terrace, nothing is filled back, a
  save, a load and a replay give the same, and the A/B finds nothing changed on a map without strokes.

### As built: step 6 (2026-10-19)

- **The settings**: `GridSettings::cliffs`, a `GridCliffs` of `GridCliffStroke`s (`GridWater.h`) - points, width
  (`GRID_CLIFF_WIDTH` 1 unless given, held to 1..`GRID_CLIFF_WIDEST` 5) and depth (`GRID_CLIFF_DEPTH` 1, held to
  0..`GRID_CLIFF_DEEPEST` 4). In `operator==`; the save's `"grid"` block holds `"cliffs":{"strokes":[...]}` only when
  there are any (`GridCliffsToJson` / `FromJson`, `chasm_generate`'s `cliffs` too). `ChasmPlaceCliffs` copies them,
  held, into `ChasmLayout::cliff_strokes`, hashed there. `chasm_grid`'s `layout.cliffs`: strokes, plots painted,
  plots refused.
- **The plots** (`TerrainCliffPlots`): the plot nearest each point; from one point's plot to the next, walked along
  fine edges as `LineStepTo` walks a jump - **added after the first try**, since a stroke given by script, as corners,
  would have met itself at a corner and leaked; past a width of one, every plot within half the width of the line
  through the points, smoothed over two points either side, a plot across being the map's area over its plots -
  **changed after the first try**: a lattice quarter (2) and the chain's staircase made width 3 about 3.8 plots across.
- **The heights** (`Terrain::PaintCliffs`, before `SettleHeights`, on the cracked-mud map too): `cliff_open` per plot,
  then each stroke's open plots set to its depth; `cliff` per plot what they came to. `SettleHeights` keeps a piece
  with a painted plot in it. The relief's guard (`BuildRelief`): height painted down fades out within
  `RELIEF_PAINT_RIM_CLEAR` of every plot below the plateau beside a plateau plot.
- **The tool**: `CHASM_TOOL_CLIFF`, "Cliff (debug)" beside Height, locked in play; `ChasmMapTool` names the four map
  tools. `UpdateCliffDrag` keeps `cliff_chain` (by `LineStepTo`, no longer static) and `cliff_band` for the preview;
  with no drag the pick view draws the band of the plot under the cursor, the brush. `CliffPlotTaken` colours it. On
  release, a stroke that takes no plot is turned down with why. The panel: depth and width, the strokes with an x
  each, the plots painted and refused, "Undo the last stroke", "Take all the cliffs away". `chasm_tool` takes `depth`,
  and `width` as plots with the cliff tool in hand; its result has `cliff_depth`, `cliff_width`, `cliff_plots`.
- **Found on the way**: a map tool's drag was never dropped - a tool put down mid-drag left it, to be placed the next
  time the tool was taken up. Now a right press or another tool drops it (`DropMapDrag`), as a road's.
- *Checked* (debug, a copy on port 8777, flat map seed 5, 4,000 cells):
  - A one-plot L dragged by `chasm_tool`: 41 plots at -6 (balcony), walls, turf and soil along both sides; the
    plateau beside it at 0. Held, the band blue.
  - A ring one plot wide: two people put inside, ordered out - "no way there" three times; within it, taken. With a
    winch on each side of the moat, both walked out: down, along the moat, up. The map then had 114 plots painted,
    the L's 41 and the ring's 73: two pieces, both kept - the pits' filling would have left the ring alone.
  - Width 3 at depth 2: -12; a depth-0 stroke over part of it cut a gap back to the plateau. Widths 1-5 on a straight
    50-unit drag: 1.4 (the chain), 2.4, 3.3, 4.3 and 5.2 plots across.
  - A river drawn across the terrace: its wet plots refused (33), the terrace stopping at its margin either side, the
    river on its own ground.
  - The default chasm map: a stroke from the plateau over the rim, width 3 depth 2 - 58 plots painted, 21 refused
    (the rim's and the chasm's), joined to the balcony below at the same depth, drawn like one of the cracks. A
    valley of -15 painted beside it: 2 units from the cliff the plateau at 5.9 (1.2 with no cliff), 13 units off -7.8
    (-9.1), away from it the same to the tenth.
  - Dropped by another tool and by a right press: 12 plots held, nothing placed. A ring given by script as its four
    corners: 74 plots, and no way out. Out of range: depth 9 held to 4, width 0 to 1; in play "a debug tool".
  - Saved, another map generated, loaded: the same world hash; a re-save byte for byte the save. A recording on a map
    with a moat, a terrace, a river, two winches and three orders, replayed twice from a chasm map: rebuilt each time,
    the traces equal on all 1,506 ticks.
  - **The A/B**, both from one snapshot of the tree, since two other sessions were changing it meanwhile: two copies
    of the app folder, A with step 6's map-facing edits undone - the painting, the pits' exemption, the relief's guard,
    `operator==`, the hash, `ChasmPlaceCliffs`'s call, the save's read and write - `diff -r` showing nothing else.
    `chasm_seed_sweep.py 1 60` identical on A and B and to step 5's, no failures; all 18 saves the same world hash and
    the 36 files byte for byte the same; the 6,000-tick replay from `village_year5` with zombies equal on all 6,001
    ticks (its final hash not step 5's: the other sessions' work since moves it, on both sides alike).

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

5. **Height**: the strokes in the settings, the layout and the relief; the Height tool, its preview and its list
   (above, "Height painted by hand").
6. **Cliffs**: the strokes in the settings, the layout and the heights; the Cliff tool on the road's drag, its
   preview and its list (above, "Cliffs painted by hand").

Step 1 and steps 2-4 do not depend on each other; steps 5 and 6 follow step 4's tools.

## Current tuning

| | |
|---|---|
| `ECONOMY_WELL_WATER_MIN`, `_MAX` | 100 and 200. A unit is two days of an adult's water, so ten people draw a well dry in 20 to 40 days |
| `ECONOMY_WELL_ENDLESS` | 40, the settlers' water: what an endless well is kept at |
| `TERRAIN_POND_DEPTH` | 1, as deep as the swamp's pools |
| `GRID_POND_BANK` | 3, the rise past the water's edge |
| `GRID_PLACED_POND_RADIUS` | 6 unless given, held to 2..30 (`GRID_PLACED_POND_SMALLEST`, `_LARGEST`) |
| `GRID_PLACED_RIVER_WIDTH` | 4, the panel's to start with, between a branch's 2.2-3.4 and a river's 5-7.5; held to 1.5..12 |
| `MAP_DRAG_STEP` | 3: the river and height tools take a point each time the cursor has gone this far |
| `GRID_RELIEF_HEIGHT`, `_HIGHEST` | 6 to start with, held to +-20 (the user's) |
| `GRID_RELIEF_RADIUS` | 12 to start with, held to 4..60 (`GRID_RELIEF_NARROWEST`, `_WIDEST`) |
| `RELIEF_PAINT_RIVER_FADE` | 8: painted height fades out along a river over this past its wet margin (the hills' is 24) |
| `RELIEF_PAINT_RIM_CLEAR` | 16: height painted down fades out within this of a drop's foot (the rift's until step 6) |
| `GRID_CLIFF_DEPTH`, `_DEEPEST` | 1 step (6) to start with, held to 0..4; 4 is -24, well above the cloud deck at -39 |
| `GRID_CLIFF_WIDTH`, `_WIDEST` | 1 plot to start with, held to 1..5 |

## Later

- **A well as a spot on the release map**, generated with the map as the deposits are and holding its amount by
  the same seeded rule: found, built over, drawn dry. For the far side, say, which has no rivers.
- **Keeping the colony when water is placed** (above).

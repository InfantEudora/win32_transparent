---
name: chasm-game-plan
description: "apps/chasm top-down colony sim on a Townscaper-style irregular quad grid; decisions so far (painting zones, palette texture, scale, replay-from-state)"
metadata:
  node_type: memory
  type: project
  originSessionId: 9ee540f3-21fa-4533-a07c-ca156fde207e
  modified: 2026-10-04T20:22:18.729Z
---

apps/chasm (empty as of 2026-10-04) is a planned top-down colony sim, Anno-like but low poly. One
huge valley split down the middle by a deep chasm with lower shards/terraces inside it; mountains
+ frozen chasm end north (impassable), swamp/desert south. Reference art in art_source/chasm/
(A Little Age demo screenshot, a mashup of it with the chasm, a painted concept).

Decided with the user 2026-10-04 (exploratory phase, no code yet):
- Grid: Townscaper-style (hex of triangles -> random merge to quads -> subdivide -> relax),
  player clicks vertices, quads render via 8-corner module lookup + deformation; Bad North
  precedent for terrain levels/cliffs.
- Buildings are PAINTED as zones over plots, not placed as fixed footprints; capacity by area.
- Art: no textures, everything uses ONE palette texture; nearly everything procedurally generated,
  only some props (trees) modelled and placed.
- Scale: map ~3x3 screens at max zoom-out = ~100k fine cells (ballpark; changes only for playtime).
- Replay from a saved game state must work like archer's (state hash per tick, bit-exact within a
  build) - so save/restore of state is a day-one requirement, not a later feature.
- Camera: A Little Age's screenshot is our MAX zoom-out; ONE level of detail, no far LOD.
- Plans written: apps/chasm/docs/README.md + grid_plan.md (6 steps, grid generator first).
- STEP 1 BUILT 2026-10-04: exe is build/chasm_nophysics.exe (USE_PHYSICS=0); run on --mcp-port 8769;
  tools chasm_grid/generate/view/check; seeds 1-3 hashes pinned in Grid.cpp, same debug+release.
  STEP 2 (picking, GridPick.*) BUILT same day; a worker window fixed the zig-zag outline
  (Grid::lines + GridLevel::pin sliding vertices - reuse for the step 3 rim). Two agents share
  build/chasm_nophysics.exe: build, then COPY to build/chasm_<id>.exe and run the copy, or the
  running exe blocks the other's link.
  STEP 3 BUILT same day: worker pinned rim+shard lines (Grid::lines[feature_line_base+i], smoothed);
  Terrain.* levels + TerrainMesh.* (marching-squares cells, strata walls, skirt, 144 chunks), sun
  shadows following the view. Objects have only 4 material slots - palette (step 4) replaces them.
  STEP 4 BUILT same day: assets/textures/palette.png (PNG is the source; Palette.h layout), core
  Renderer::ambient_sky/ambient_ground (hemisphere) + background_color added, lighting tuned by
  MEASURING pixels vs art_source/chasm/alittleagedemo.jpg; frustum culling on for chasm.
  STEP 5 BUILT same day: Zones.* rules (house per plot w/ storeys, field per coarse cell) changed
  ONLY by CHASM_CMD_ZONE commands; ZoneMesh.* placeholders; step 6 must RECORD COMMANDS (view
  turns mouse into commands). Third window f6 is modelling props in art_source/chasm/chasm_props.blend
  -> assets/meshes/chasm_props.glb (palette.png stays the single source; palette is 32x16 now).
  STEP 6 BUILT same day: core SIM_CMD_FLAG_RECORD (commands recorded into .rec as `cmd` lines,
  replayed at their tick, live ones dropped during replay); ChasmSave.* saves = recording state;
  run `python apps/chasm/tools/chasm_replay_test.py` after rule changes (PASS debug+release).
  ALL 6 PLANNED STEPS DONE 2026-10-04. Steps 7-9 agreed: 7 forests (BUILT: Forest.*, 148k props via
  core INSTANCE SETS Object::SetInstances + RebuildUniqueMeshList map fix), 8 procedural buildings +
  boundaries that form themselves (walls/fences/palisades on plot-boundary segments whose sides
  differ - refs townscapergarden.jpg, titlescreenlittleage.png) BUILT: ground zones garden/town,
  BoundaryMesh walls/palisade/fences, BuildingMesh hip roofs+windows, CropMesh (by worker 78);
  9 chasm look BUILT: rivers as a position-only channel dip (Terrain RiverDip raster, NOT a level -
  a level would put 3 levels in river-mouth cells), WET vertices refuse zones/forest, falls where a
  river crosses the rim, chasm_water.glsl (archer's adapted), mist + foam = instanced faceted puffs
  posed from the sim clock (Mist.cpp), palette row 15 = EFFECTS row. User liked step 8 as-is;
  palisades may later become a road-like line tool to fence larger areas.
  Engine HAS instancing (Renderer instancedata SSBO) - I wrongly said it didn't once.
  PROPS BUILT + ACCEPTED same day: 15 assets (4 pines 38-58 tris, 3 oaks 58-118, rocks, stump, log,
  bushes) in chasm_props.blend "Export"; re-export = the collection's exporter (one click), NOT
  apps/chasm/tools/blender_chasm_props.py (that only builds a FRESH .blend, needs --force, wipes
  edits). blender_chasm_props_check.py --verify (reimports GLB: origin, row-0 UVs, flat normals) /
  --render (previews/). Stump/log cut faces borrow PAL_PATH: no free palette column.
  ROUND 2 same day: ground cover (grass/flowers/fern/shrub/mushrooms/twig, 6-23 tris) + biome
  trees (snow pine, palm, willow). New assets go in with `--add` (builds only missing names,
  touches nothing else; `--replace NAME` rebuilds one). Snow = PINE_LIGHT: the frozen row makes
  that cell white. Trees are ~2x A Little Age's tree/house ratio at the briefed sizes, so the game
  draws them at 0.7 scale, two per plot - keep modelling at the briefed sizes, scale is game-side.
- RRandom IS seedable now (Generate(seed)); world gen should use its own instance.
- STEP 10 BUILT 2026-10-05 (session 0052a3e4, alongside win32-transparent-35's roads/walker/gates):
  the chasm is GENERATED from the seed - ChasmLayout.cpp (rift spines -> distance field -> rims as
  zero contours; forks, a second rift from S/W/E; terraces = rim stretch offset in past a crevice;
  shards/columns = blobs; rivers = A* + meander swing to falls on rims). Grid::layout + LineKind;
  GridSettings::features and ChasmDefault* are GONE; saves hold no features (old saves load onto a
  different world). Rules: lines >= 2.5 sides apart (`spacing` check), bends radius >= 1.4 sides,
  no rift reaches the north edge (levels use a northward ray parity). chasm_generate takes
  frame_map/pitch/include_screenshot; B = previous seed. Seeds 1-3 re-pinned, same debug+release.
- STEP 10 (2026-10-05) split across two windows: win32-transparent-80 GENERATES chasm/shards/terraces/
  rivers from the seed (free layout, ChasmLayout.cpp, grid_plan.md "Step 10, generated chasm"); this
  side built ROADS + a DEBUG WALKER + GATES + ARCHES (docs/roads_plan.md). Road = ZONE_GROUND_ROAD,
  drawn as Bezier curves through edge midpoints (RoadMesh.cpp); walker = A* over plots (Walkers.*),
  saved with its path, hashed as trace part `walkers`; gate = a dead-end road against a wall
  (ZoneGateOf), the only way through a wall; arch = a road run of <= 3 plots between 2+ storey houses
  (ZoneArchStoreys), bridged in BuildingMesh. User chose: roads painted on plots, debug walker first.

**Why:** the user wants the building mechanic tested before any gameplay goes on top.
- Testing: NO separate engine-free `make rules` build (user's call, unlike archer/bomber). Checks
  and debug views are built into the app as commands (debug panel + MCP), `#ifdef DEBUG` only, so
  failures can be SEEN; fewer rules than archer.

**How to apply:** prototype stepwise, step 1 = app + grid generator + grid debug view together; see [[archer-app]] for the replay/state-hash pattern and
[[replay-determinism-plan]].

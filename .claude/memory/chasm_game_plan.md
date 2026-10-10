---
name: chasm-game-plan
description: "apps/chasm top-down colony sim on a Townscaper-style irregular quad grid; decisions so far (painting zones, palette texture, scale, replay-from-state)"
metadata:
  node_type: memory
  type: project
  originSessionId: 9ee540f3-21fa-4533-a07c-ca156fde207e
  modified: 2026-10-09T14:04:48.312Z
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
  (the chasm_replay_test.py that checked rule changes was retired 2026-10-08 - see REPLAY TEST RETIRED below).
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
  2026-10-05 (after the other machine's roads/biomes/gameplay_plan.md): GAME NAMES - terraces are
  ISLANDS (shard/column/ledge, zeppelin; old "terrace" renamed ledge) and BALCONIES (joined to a wall
  at -12, winch; build anything but fields). Home = EAST: rivers east only, swamp east, desert west;
  each side ONE walkable region (generator redraws rifts; `sides` check). Balcony end = junction
  pinned onto the rim's chain (exempt ring-2, forced spoke, unmerged wedge triangle, Untangle guard);
  3-level cells drawn in quarters; strata at fixed heights.
  BUILDINGS AS THINGS (docs/buildings_plan.md, agreed + step 1 BUILT 2026-10-05): a building is a
  GROUP of plots (fields: coarse cells) with its own id in a table; A DRAG IS ONE BUILDING (stroke
  number in the command's value[2]); sizes by economics later, hard cap only house <= 4 plots; kinds
  house/store/woodcutter/water collector/field (crop per field); the winch comes AFTER food/water/wood.
  Nothing builds on the chasm floor. Replay test village moved to x+190 (old spot became chasm floor).
  WINCH BUILT 2026-10-06 (step 2): one rim plot above a balcony, landing = nearest balcony corner,
  walker link across levels (5 s ride), tool H. Balconies being shrunk to ~1/4 by worker window 39.
  2026-10-06: first goods are WOOD, FOOD, WATER (steam etc. only noted); crops are food KEEPING-KINDS
  (wheat medium + a resource like rope, greens fast/no winter, beans high value/long keep; later fruit
  = short, animals = medium meat + leather). P1 CALENDAR BUILT (Calendar.*, ApplicationChasmTime.cpp):
  one tick count of state, 45 s day, 10 days/season, overlay HUD via core UIOverlay, speed 1/2/3x =
  physics_time_factor, space/-/=, chasm_time tool, date command recorded. Winter snow front drawn by
  a ground shader (worker 39, core Shader opt-in to cast shadows). Seed 95 sides strip = parked map issue.
  P2+P3 BUILT same day (docs/economy_plan.md; Goods.h, Economy.*, ApplicationChasmEconomy.cpp,
  chasm_economy tool): user rules - a store NEXT TO a workplace takes only its goods (fixed); a free
  store takes all, settable per good; workers walk STRAIGHT where clear, grid A* otherwise; stand-in
  worker per workplace until P4, he carries his own goods; spoilage waits for P5. View by worker 39.
  Woodcutter piles logs at his hut when no store (never idle in the woods); going home between trees is
  WANTED (forces house near hut+store). P4 PEOPLE BUILT same day (docs/people_plan.md): settlers only
  (families 4/3/3, wait at a camp inland of the home balcony), rounds start+end at the HOUSE, jobs
  automatic by skill vs walk, skill = pace; rivers split the home side until bridges. CAMP is a building
  kind (tents + fires, house rules, 1 person/plot, walkable), a new map starts with one sized to the
  settlers. NO day/night (user: none of the references have it); maybe a gradual darker winter later.
  CONSTRUCTION (play mode step 5) BUILT same day (docs/construction_plan.md): user's rule WOOD ONLY,
  IDLE (jobless) people carry + build. ZoneState::standing per plot (<= storeys); a PLAY command
  (value[3]) plans storeys, debug stands them at once; economy names raised plots (Raises()), the app
  applies ZONE_OP_BUILD_RAISE on the same tick (not recorded). What a building DOES goes by standing.
  Camp starts with 40 wood/20 wheat/10 beans/20 water. Play mode now has the tool keys + a bottom bar.
  PLAY SELECTION BUILT same day (docs/selection_plan.md): core OUTLINE pass by worker 39
  (Object::SetOutline/SetOutlineOnly), chasm side in ApplicationChasmSelect.cpp - outline-only copy
  meshes of the selected/hovered building, people outlined, pins, cards (clickable store chips, close),
  clickable build bar, right-click = drop tool/deselect; chasm_select tool. Entering play mode glides
  the camera to the camp. Escape still closes the window (core default).
  WATER + BRIDGES BUILT 2026-10-06 (docs/bridge_plan.md): wet band = water + stone bank (4.5 past the
  edge) stays unwalkable; collector on the last DRY plot with a pipe over the stones; ZONE_KIND_BRIDGE
  placed whole (ZONE_OP_BRIDGE, ZoneBridgeChain: dry ends, wet between, water <= 2x river width),
  wood-built from the banks in, deck drawn straight between the dry ends; tool J; chasm_bridge.
  2026-10-08: chasm_replay_test.py's camp fixtures are FOUND BY TRYING (find_camp_spots: road through trees,
  palisade ring + gate, woodcutter, play wall) - PASS on seed 12; fixed offsets went stale on a default-seed change.
  SITE QUEUE + "BUILD FIRST" BUILT 2026-10-07 (construction_plan.md "Which site goes first"): nearest, but
  within ECONOMY_QUEUE_SLACK placing order (= building id); ZoneBuilding::f_priority beats distance.
  PALISADE WALLS BUILT 2026-10-08 (line_works_plan.md step 3, five phases): ZONE_GROUND_WALL drawn with key 7
  like a road, 4 wood/plot, trees felled first; gates = road plots marked ZoneState::gate; ZoneState::line ids
  share the building-id counter, so roads + walls + buildings are ONE queue (FindSiteWork ranks carries and
  line work together). Lines never "Build first".
  P5 NEEDS AGREED 2026-10-07 (docs/needs_plan.md): HEALTH is the food meter, a THIRST bar (empty -> health
  falls faster), COLD the same in winter, beans worth most/greens least. People STARVE - no leaving, no
  immigrants. Target: one field cell feeds one person a year. ALL FIGURES ARE TUNING (needs_plan.md "Current
  tuning" table; 2026-10-08 now 7-day health, 4-day water, 2-day water unit) - see [[tuning-numbers-not-rules]].
  User first said eat ONLY AT HOME, then CHANGED it same day: below a third of water or health a person takes
  no new work and eats/drinks at the nearest store/camp/collector/field (SeekSupply, WORKER_TO_SUPPLY/SUPPLYING;
  one-unit claims, waits at a filling collector) - BUILT. STEP 1 BUILT same day (Economy::Needs/EatAndDrink,
  health+water per person in ticks, EconomyState::dead). STEP 2 FETCHING HOME BUILT same day (FetchHome/TakeHome,
  states 16-18): house < 2 days -> a load from nearest STORE OR CAMP (user: camp counts; camp families don't fetch),
  idle member first, capped at 3 days so water can't crowd out food; house room 1.5 -> 4 (plan's figure).
  chasm_replay_test FAILs on 'working 0' + seed-1 oaks with or without step 2 (A/B-checked) - not a regression.
  BUILDERS BUILT 2026-10-07 (docs/builders_plan.md): user chose a BUILDERS' HUT workplace (ZONE_KIND_BUILDERS 9,
  WORKER_JOB_BUILDER 3, tool G - B is previous seed); a place per standing plot-storey, a hut SITE employs one
  who builds it first; AssignJobs fills huts first then workplaces by id, newest wait (EconomyState::places).
  Job overview bottom-left (DrawJobsOverview, EconomyJobCounts); chasm_economy has `jobs`.
  DEBUG WALKER RETIRED 2026-10-07 (user): tool 7, chasm_walker, saves/trace part gone; Walkers is only
  PlanRoute now; CHASM_CMD_WALKER number kept unhandled. Debug panel PEOPLE section: per-villager health/
  water sliders -> recorded CHASM_CMD_NEEDS on release; chasm_economy op set_needs. An edit at home is
  eaten straight back up (needs run after commands). To screenshot a panel section below the fold, build a
  throwaway copy with it moved to the top - MCP cannot scroll ImGui.
  FIELDS BUILT 2026-10-08 (docs/fields_plan.md): FOLLOWS-WHAT yield table (crop_after), sowing costs TIME,
  a play field is a site (2 wood/cell, field_built per coarse cell, FIELD_RAISE), empty until a starting
  crop (ZoneBuilding::crop, NONE) is chosen; next_crop sticky, AUTO default (best, ties wheat<greens<beans);
  farmer sows first (TO_SOW/SOWING 19/20); store next to a field takes all food; card CARD_CHOICE chips.
  2026-10-07 FIXES: field harvest untouched (SourcePlot -1 for fields: no plots_of; falls back to home_plot);
  walk-back twitch on any placement (RouteTo started at the plot behind; now skips it). Movement bugs: pause,
  sim_step 1 tick at a time, read chasm_economy x/z; chasm_paint while paused blocks - send it on a thread
  beside the sim_step.
  REPLAY TEST RETIRED 2026-10-08 (user): chasm_replay_test.py + saves/replay_test_*.json deleted; ONE new
  replay after the mapping work, made by the agent tuning rivers. Default map now 50k cells (Grid.h; planned
  size still ~100k). START CAMP (ZoneStartCampPlot(w,tents), user 2026-10-08, built): best SCORED spot along
  the rivers (ZoneCampSpots: every 16 along, both banks, rows 8/16/24 from water; refused if the camp's flood
  doesn't fit by house rules, no water-collector spot in survey, rim <27, swamp; nudge 8). Score on a 24
  walk-survey: flat (house share + field share of a full disc) + trees (peak at HALF the plots wooded - the
  user's "forest edge") + 0.25 x middle. Debug layer "camp spots" (chasm_view camp) + chasm_camp tool.
  Seeds 1-30: all camp, all housed; the first row is in the relief's river fade band, often too steep
  (seed 21 none fit). SEASONS DRAWN (docs/seasons_plan.md, user 2026-10-08): seasons EVERYWHERE AT ONCE
  (snow front reserved for later biome temperature), grass fades SMOOTHLY, broadleaf leaves change PER TREE,
  bare oaks MODELLED IN BLENDER (props are Blendered). Step 1 BUILT: palette rows 4-7 (spring/autumn/
  autumn-red/winter, copies of temperate=summer), UpdateSeasonPalette rewrites the GPU palette's temperate
  row in place (img_data + Texture::UploadTexture keeps the bindless handle) - shader blend cost ~15% of
  the colour pass, row rewrite ~0; leaf columns 19-21 stay summer (trees' own); flowers only spring/summer;
  snow also lies on season rows. Steps 2+3 BUILT same day: Seasons.h + ApplicationChasmSeasons.cpp, a MESH per
  (oak/bush kind, stage) = 36 looks, each its own set per chunk (65 sets/chunk; materials are per OBJECT in
  core, not per instance); green look on row 8 PAL_SUMMER (app copies row 0 there at load) so row 0 can fade
  gardens/hedges; leaf cols kept out of the fade only for crops. Bare oaks via blender_chasm_props.py --add
  (bare_broadleaf copies the leafy oak's trunk from the .blend); the USER tunes/re-exports them.
  FIREWOOD (needs step 3, user 2026-10-08, BUILT): dawn burn under snow (1/plot-storey; camp 1/plot) only if
  inhabited, `heat` saved+hashed ([0]=homeless), cold drain +1; separate WOODSHED (4 days' burn); fetch from
  AUTUMN (and spring while snowed) from stores/empty camp, fair share = firewood AT HOMES only (user) + 1 day;
  no reserve for sites. Every house ONE chimney (roof or side stack, HouseChimney); smoke = LIFE puffs + unlit
  orange panes (ApplicationChasmHearth.cpp, BuildHouseCell fixtures). Bug found: WORKER_GETTING mapped wood to
  food. LIFELINES (docs/lifeline_plan.md, user 2026-10-09, NOT built): ONE year of age per GAME year (birthday
  on a day of the year; per-season turned down); baby 1 yr - MOTHER's job is CARING, her place refilled;
  child 1.5 yr, job PLAYING (walks round the house); young adult 2 yr (works, skills growing); adult at 4.5.
  Settlers = families (couple + grown children) + 2 kids = 12; mother/father ids on everyone and the dead;
  couples form moving into a FREE HOUSE together (new family) - user loved it. Then (same day): EXPECTING
  half a year, one settler couple expecting at start; old age AROUND 40 (20 h life); old stop work, wander,
  still fetch; caring mother still fetches; birth needs warm house + fed parents; a mother's MOST CHILDREN cap
  (settler mothers' kids count). NO orphan mechanic (user: too complex, no use yet - they grow up in the house);
  expecting mother works till birth. User wants each feature SEPARATE AND SMALL; build order step 1 = families
  + ageing + children playing (no births, no old age, skills random). STEP 1 BUILT 2026-10-09: born/mother/
  father (age derived, EconomyState::tick, Restore takes calendar tick); PLAYING is a JOB (-2, below NONE) so
  idle-adult code skips kids for free; grow-up in Tick re-runs AssignJobs; play spots incl. camp tents;
  12 settlers, seeds 1-30 all camp. Old saves dropped, not converted (user). DOORS FIXED same day: home_plot =
  lowest plot Walkers::OpenFromOutside (was lowest plot: often shut in - coarse verts have lowest ids); doors
  move -> KeepAtDoors moves indoor people + Replan home via RouteHome (a family died shut in otherwise).
  Names: 30/30 first, 28 surnames, stride (no dupes in first 30). SHUT IN flag (user, same day):
  EconomyState::shut/EconomyShutIn, door needs WALKER_OPEN_GROUND 24 joined open plots behind it; shut house
  = uninhabitable (HouseFamilies can_take), card warning; flag meant for reuse. Economy only Derives on a
  TICK - read flags with the sim running, not paused. LIFELINES STEP 2 BUILT 2026-10-09: partner/due saved;
  jobs BABY -3 / CARING -4 (EconomyStageJob = below NONE); chance at dawn after Heat on EconomyCoupleDay (room
  50% / full 25%, user: room likelier, house APPEAL later via gardens); birth when mother next home after due;
  issues rows HOUSE CROWDED / HOUSE SHUT IN (issues_plan 9-10). User sometimes plays in my test copy.
  LIFELINES STEP 3 COUPLES BUILT 2026-10-10: per-person `surname` (settlers EconomySettlerSurname(family), kids
  EconomyCoupleSurname hash of the two ids); couples pass at END of HouseFamilies (eldest single adult first,
  EconomyRelated = shared id among self+parents+grandparents, smallest empty house >= LIFE_COUPLE_ROOM 2, new
  family number); Lifelines(tick,f_house) re-runs housing when someone turns adult. HouseFamily() names a couple's
  house by the children's surname. NEEDS STEP 5 BUILT 2026-10-10: death line (red, 5 s, fresh deaths only) + THE
  COLONY IS LOST, in DrawBuildBar = PLAY MODE ONLY (chasm_view play:true to test; restore window for UI shots).
  A person AT HOME eats/drinks the same tick, so set_needs to kill only works on someone out walking.
  CAMP FAMILIES NOW FETCH (user reversed own rule 2026-10-09 after a camp baby went dry): camp has room+woodshed,
  never fetches from itself. FETCH FROM ANYTHING (user, same day): Economy::FetchFrom = store, camp supplies, empty
  house, collector, woodcutter pile, own field (fair share); pressing trip brings a load home via FetchFrom +
  TakeHome top-up. Camp PANTRY (another session) holds what camp families fetch. RIVERS REWORKED 2026-10-08 (docs/rivers_plan.md, built):
  every map has a home river (approach bug fixed; small-river and off-the-east-edge fallbacks; `rivers`
  check fails none), 2-3 BRANCHES per river (GRID_RIVER_JOIN, inland heads), far-side scenic SPRINGS and
  home SWALLOW-HOLE streams (GRID_RIVER_SINK) whose spring pours from a dark mouth in the wall
  (Terrain::mouth, TerrainMesh InMouth). Seeds 1-3 re-pinned (debug == release). All old saves and recordings deleted; the USER makes the new recording.
  THIRST MARK (user's ask, same day) then the PERSON BADGE (user's, same evening): ONE badge over everyone
  out of doors - job icon (axe/wheat/bucket/hammer/log) ringed by health (core UIOverlay::AddArc), the drop
  replaces the icon when short of water, ring red when dry; selection pins MERGED into it (orange/amber ring
  + stem). Roof drop for indoor families kept (DrawRoofThirst). ApplicationChasmSelect.cpp DrawPersonBadge.
  engine.mk REFUSES `EXE=` on the command line - copy-link via `make -n` lines with -o renamed.
  sim_step runs at tick rate (50/s) and blocks the HTTP reply: keep it under ~5000 ticks a call.
  If the user's chasm_nophysics.exe holds the link, link a copy by hand (make -n link line, -o renamed)
  instead of killing it twice.
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
  CAMP PANTRY (user 2026-10-09, chose "two stocks"): a camp = open SUPPLIES (`stock`, the colony's, whoever lives
  there) + private PANTRY (`EconomyState::pantry`, what camp families fetch, house room/woodshed); eat/drink/burn
  pantry first; pantry joins supplies the tick the camp empties (OpenPantries). RULE: a lived-in HOUSE's stock is
  its family's alone, an empty one's is anyone's (Economy::PrivateStock; TakeHome refuses a private source).
  Saved as "pantries", hashed. Verified live: camp fetched water into pantry, emptied camp merged it.
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
- BIOMES (docs/biomes_plan.md), user's choices 2026-10-05: option C - levels for hard edges + a smooth
  relief field (soft hills, rugged peaks, swamp dip); the NORTH MOUNTAIN is the only rule a biome
  enforces: it seals the chasm from the north edge, impassable to EVERYTHING; mountain pockets are
  buildable, reachable from ONE side only, mainly for resources. Step 1 BUILT: mountain foot curve with
  a tongue past the main tip (ChasmLayout), Terrain::biome, debug check `sealed` (seeds 1-60 pass,
  proven to fail when the tongue is cut short). Step 2 BUILT: Terrain::Relief/GroundHeight (THE ground
  for everything; TerrainGroundHeight is now the level bump only), hills 14 high, mountain crags to ~60,
  flat along rivers; slope limits (house 0.7, road 1.4, field 1.3 rise), walker slope cost + 0.75 max
  grade; houses on half-storey-rounded footings. Other window did picking/cursor/camera on relief.
  Step 3 BUILT: pockets = meadow + one valley through a spur of the foot (ChasmLayout::Pocket), biome
  POCKET, check `pockets` (one side only, and walkable); rivers now rise at the mountain's foot (seeds 2,3
  re-pinned). 58/60 seeds have a pocket per side. Step 4 BUILT: swamp on ONE side of the main mouth (the
  other side's south is for a desert later), ChasmSwampMask, biome SWAMP, pools = wet + flat water cells
  drawn by a slow copy of the river shader, willows (PROP_WILLOW). Step 5 BUILT: ground colour per
  TRIANGLE (TerrainMesh GroundColour: biome via TerrainBiomeAt + dither, steep = rock, mountain scree/
  snowline 12/crags), the DESERT opposite the swamp (dunes, palms), snow pines, murky pools. All 5
  biome steps done 2026-10-05.
  After: REEDS (other window, Forest.cpp PROP_REEDS_A/B, wet band); CORE ANIMATED INSTANCE SETS
  (Object::SetInstanceMotion, material wind_mode>=8 = PUFF/LIFE/FALL, SSBO 8, Renderer::motion_seconds
  = sim time) - chasm mist uses PUFF; LIFE/FALL untested, meant for fog of war, foam, rain/snow.
  Swamp mist = HAZE in the pool shader (opaque puffs over water read as stones/slabs - don't retry).
  Archer's replay baseline fails from tick 0 since core commit 2cc7819 (not the motion change).
  Later (user, 2026-10-05): a buildable overlay round the cursor in ALL builds, with building footprints.

**Why:** the user wants the building mechanic tested before any gameplay goes on top.
- Testing: NO separate engine-free `make rules` build (user's call, unlike archer/bomber). Checks
  and debug views are built into the app as commands (debug panel + MCP), `#ifdef DEBUG` only, so
  failures can be SEEN; fewer rules than archer.

**How to apply:** prototype stepwise, step 1 = app + grid generator + grid debug view together; see [[archer-app]] for the replay/state-hash pattern and
[[replay-determinism-plan]].
  ARCHES (docs/arches_plan.md, user 2026-10-10): same kind only, one owner (both sides one building, else most
  plots beside, lower id), towers NEVER arched (for now), no arch over a gate, gatehouse later (defence plan).
  STEP 1 BUILT: ZoneState::arch/arch_owner derived by Zones::RefreshArches (not saved/hashed); mesh/pick/outline use
  owner; ZoneKindHasBody. NEXT step 2: arch storeys as SAVED state kept APART from road plot's storeys/building
  (walkers/straight-walk/ladder read storeys as ground-built), arch plots NOT in plots_of; then built as a site with
  scaffolding over the road, cost = owner kind's storey cost; losing it -> crowding.

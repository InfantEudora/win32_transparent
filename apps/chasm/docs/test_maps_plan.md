# Test maps: small worlds made by code

Talked through with the user 2026-10-18. The game has only ever had one kind of map: generated from a seed,
50k cells with the chasm down the middle, a start camp and twelve settlers. Combat and other systems want
testing on their own, on something small and plain - the user's example: a small map, trees placed at random,
no villagers. The same means should also make the title's diorama, which is a 4.3 MB capture loaded from
`assets/dioramas/menu_town.scap` today.

All six steps built and checked 2026-10-18 (as built below each). Left: the title town's look, and deleting
`menu_town.scap` once it is liked (step 6).

## Decided (the user, 2026-10-18)

- **Maps made by code**, and as deterministic as the seeded one: the same settings give the same world.
- **The irregular grid stays.** A test map is today's Townscaper grid, small, with no chasm in it - not a square
  grid. There is no point in a second kind of grid, and the real plot shapes are what the rules are written
  against (a tower's guards are the quads round its plot, a house's room is its area).
- **Saves and replays of a test map work as any other's**; only autosave is off on them (agreed with the
  go-ahead for this plan). Mine, said at the time: in this game a recording's start state IS a save, so turning
  saves off for test maps would turn their replays off too, and a bit-exact replay of a fight is the most useful
  thing a combat test can have. Keeping them costs no more than gating the seven places a save is written.
- **After the plumbing, a check that today's saves are unaltered** (step 4).
- **The title's diorama made the same way** - a town built by code at start, not a file (step 6).

## The idea: a map kind is part of the seed

"The world is a seed plus edits" (`README.md`) holds unchanged. A test map is another *generator*, chosen by a
new `GridSettings::kind`, and what a new game begins with - the start camp and settlers today - is chosen by
the same kind:

- the **world** is derived from the settings and never saved, as now;
- what a kind's **start** puts down is edits, saved like the start camp is now;
- so `LoadSave`, the world-hash check, `RestoreRecordingState` and the replay trace work with no change of
  their own, because each regenerates from the settings.

**It has to be in `GridSettings`, not a flag of the app's.** `LoadSave` and every replay rebuild the world from
the save's settings (`RegenerateGrid(save.settings)`), so a flag kept anywhere else gives back a chasm map on
the first load or replay. And `Economy::Restore` keeps the settlers `Economy::Reset` has just made whenever the
save has no people - which a test map's save without villagers has not - so the twelve settlers would come
back too. Both decide from the world's settings instead: `world->grid->settings.kind`.

| | `chasm` (the default) | a test kind |
|---|---|---|
| Grid | the lattice, then `GenerateChasmLayout` | the same lattice; the layout left empty |
| Relief | hills, the mountain, the swamp | flat, or the hills where the kind wants them |
| Forest | `BuildForest` | the kind's own placement |
| Start | the camp and its settlers | the kind's own: nothing, or what it builds |
| Exploration | nothing explored | all of it explored |
| Autosave | on | off |
| Checks | all of them | the chasm's own skipped |

A kind is a small function with its own constants. A number that a test needs to vary becomes a setting
when it does - in `operator==` and the save, like the rest - and not before.

**What an empty layout already does** (read through 2026-10-18; nothing crashes): the rift distance answers
"outside" with no raster (`ChasmRiftDistance`), the mountain's foot is -infinity, the swamp and desert masks 0
with no radius, so every plot comes out plateau, temperate and dry, with no deposits, mist, water or springs.
Two things stand between that and a test map: the hills, which are seeded noise with no switch
(`Terrain::BuildRelief`), and the forest, which places itself. The lattice is untouched by leaving the layout
out: the layout draws from its own `RRandom` and has not been pinned into the grid since 2026-10-06.

## Step 1: the kind in the settings

- **`GridSettings::kind`**, `GRID_KIND_CHASM` (0) by default, then `GRID_KIND_FLAT`; `GRID_KIND_TITLE` comes
  with step 6. In `GridSettings::operator==`. In the save's `"grid"` block by name (`"kind":"flat"`), **written
  only when it is not the chasm**, so a chasm save's JSON stays exactly as it is - which step 4 checks - and
  read with the chasm as the default.
- **`Grid::Generate`** skips `GenerateChasmLayout` for any other kind; `feature_line_base` stays 4, the map's
  four edges, so `TerrainFeatureLines` and `LineKind` find nothing.
- **`Terrain::Build`** leaves the relief empty for a kind without hills (`Terrain::Relief` reads an empty one
  as 0, and an empty river raster as no river).
- **The checks** (`RunGridChecks`, `RunTerrainChecks`): those about the chasm - layout, one chasm, rivers,
  sealed, sides, balconies - report skipped ("not a chasm map") on another kind, as `f_skipped` already does
  for a check with nothing to check against.
- **`chasm_generate`** takes `kind`; the debug panel's Generate a list of the kinds. Test kinds are a debug
  thing: they are not in New game's `CHASM_STARTS`.
- *Done when* `chasm_generate {"kind":"flat","target_fine_cells":4000}` makes a small flat map, all plateau
  and dry, its checks passing or skipped, and seeds 1-3 still pass `pinned`.

## Step 2: the kind's forest

- **`BuildForest`** hands a test kind to its own placement: trees at random over the plots, from the forest's
  own hash of the seed and the plot (`Chance`), filling `ForestData` as `BuildForest`'s `Place` does - each
  prop's plot, coarse quad and chunk, `chunk_props`, the counts, and `core` where flocks should count it as
  forest.
- Trees are known by their index in `forest->props` (felled trees, fruit, regrowth, `EVENT_TREE_FELLED`, the
  save's `felled`), and `Grid::Hash` does not cover the forest. So the order must follow from the settings
  alone, which a seeded placement gives.
- *Done when* the flat map shows its trees; one felled in debug stays felled across a save and a load.

## Step 3: the kind's start

- **No camp and no settlers** for a test kind: `EnsureZonesWorld` places the camp only on a chasm map, and
  `Economy::Reset` finds a camp and makes settlers only there. The first tick's landing (supplies into the camp,
  `EVENT_SETTLED`) skips a colony with no people.
- **A start of the kind's own** in their place, where `PlaceStartCamp` is called: buildings, grounds and fields
  applied straight to the zones as debug commands are (standing at once), not recorded - as the camp is not.
  Empty for `flat`; the title's town in step 6; a scenario's later.
- **All explored** from the start (`RevealAll`, as `LoadSave` already does for a save with nothing explored).
- **No autosave** on a test kind (`UpdateAutosave`, `Autosave` - `ColonyLost` already skips one), so a test
  never pushes a colony out of the three slots; and the title's **Continue** passes over saves of test kinds,
  so a quick save of one is never what Continue opens.
- *Done when*, in debug mode on a flat map: no one at the start, no clouds; a tower, a house and a wall painted
  stand at once; zombies dropped with `chasm_zombie` stand lost with nobody to chase; a save and a load give
  the same; and an `input_record` / `input_replay` of a minute of it compares equal in `replay_trace`.

**Debug mode first.** It already unlocks every tool and skips the clouds and the settlement's borders, and what
it paints stands at once. Play mode on a map with no villagers is a dead end - see "Later".

## As built: steps 1-3 (2026-10-18)

- **The kind** (`Grid.h`): `GRID_KIND_CHASM` 0 and `GRID_KIND_FLAT` 1, `grid_kind_names`, `GridKindByName`, and
  `GridKindHasChasm` / `GridKindHasHills` - what the rest asks, rather than naming kinds. `GridSettings::kind`
  is in `operator==`. The save writes `"kind"` in its `"grid"` block for a test kind only; a save naming a kind
  this build does not know is refused rather than loaded as a chasm.
- **The world**: `Grid::Generate` leaves the layout empty for a test kind; `Terrain::BuildRelief` leaves the
  relief empty for a kind without hills. The checks skip what is about a chasm: the grid's `layout`, the
  terrain's `one chasm`, `rivers`, `sealed`, `sides` and `balconies` - they still run, and say why they are
  skipped.
- **The forest** (`Forest.cpp`): on a flat map a tree on `FLAT_TREE_SHARE` of the plots that are flat all round,
  `FLAT_OAK_SHARE` of them oaks and the rest pines - no forests, rocks, boulders or ground cover, so nothing is
  in a test's way. No `core`, so no forest to discover and no forest flocks; the wandering birds still land.
- **The start**: `Economy::FindCamp` finds no camp on a test kind, so `MakeSettlers` makes nobody - and a load
  keeps nobody, since `Economy::Restore` keeps what `Reset` made. `EnsureZonesWorld` places the start camp on
  the chasm only, and a test map's exploration is revealed whole. The landing journals `EVENT_SETTLED` only
  with someone to settle. `Autosave` passes over a test map, and the title's Continue over a test map's saves
  (`ChasmSaveEntry::f_test_map`); its button is still enabled by any save at all.
- **Tools**: `chasm_generate` takes `kind`, `chasm_grid` reports it, and the panel's Generate has a kind list.
- *Checked* (debug, port 8771):
  - `chasm_generate {"kind":"flat","target_fine_cells":4000,"seed":5}`: 954 coarse and 3,816 fine quads, 152 x 83
    units; grid 11.9 ms, levels 2.0, mesh 4.2, forest 0.3 - 419 trees. Every check passing or skipped; the relief
    0 everywhere; no workers; 3,311 of 3,311 cells explored.
  - A guard tower painted stood at once, full (8 provisions, 12 water), with 2 guard places; a house and four
    wall plots likewise. Five zombies dropped stood with nobody to go for.
  - Saved, a chasm map generated, and the save loaded over it: the world hash matched (`dc71866ed8e62108`) and a
    re-save was byte for byte the save.
  - Recorded 1,500 ticks with a zombie drop and a house painted halfway, then replayed twice: the traces equal
    on all 1,501 ticks, and the map flat with nobody on it during both.

## Step 4: today's saves are unaltered

Steps 1-3 change nothing on a chasm map by design: every new path is taken on another kind only, and a chasm
save writes no new key. This proves it, A against B - HEAD built in a worktree (`.claude/memory/worktree_ab_build.md`:
the untracked `libs/` and `3rdparty/` to copy, the MAX_PATH trap) against the new build, both debug, each on a
port of its own.

- **First, copy `saves/` aside.** A running copy writes the three autosave slots whenever it loads
  (`chasm_load` counts as leaving a colony); afterwards every other file in it must match its copy byte for
  byte.
- **The generator**: `tools/chasm_seed_sweep.py 1 60` against each; the two outputs identical line for line,
  and `pinned` passing on seeds 1-3 in B.
- **Every save in `saves/`**: `chasm_load` (no world-hash warning), `chasm_save` to a scratch name at once,
  then `sim_pause` and `sim_step` 3000 ticks (a minute of the game, in calls under 5000 ticks) and save again.
  A's and B's files the same byte for byte, both times. A save against its own re-save is not the test: a load
  upgrades an older save's form the same way in both.
- **A replay**: a recording made in A from a busy save (`village_year5`), a few minutes long, replayed in both;
  `replay_trace` finds no tick where they part.
- Run the save part again after any later step that touches a shared path.

**Run 2026-10-18, after steps 1-3.** Simpler than a worktree, since the tree had no code changes: the unchanged
code built first and kept as `build/chasm_before.exe` (A, port 8772), the new build as B (8771), both debug.
The scripts were throwaway, in the session's scratchpad; their steps are the ones above.
- `saves/` copied aside, and both builds given the same inputs: each save copied as `zz_in_<name>`, so the
  autosaves the two copies wrote over each other could not change what was loaded.
- **The generator**: `chasm_seed_sweep.py 1 60` - no failures in either, the two outputs identical line for line;
  `pinned` passing on seeds 1-3 in B.
- **All 17 saves** (the three autosaves among them): loaded with the same world hash in both, and the 34 files -
  each saved at once and again after 3,000 ticks (the calendar moved on 3,000; `village_year5` with its 18
  people) - the same byte for byte, A against B.
- **A replay**: 6,000 ticks recorded on A from `village_year5`, six zombies dropped by the village halfway,
  replayed on A and on B - the traces equal on all 6,001 ticks.
- Afterwards every save but the two autosave slots the copies wrote was its copy byte for byte; those two were
  put back from the copy, and the test's saves and recordings removed.

## Step 5: people put down by a command

Combat needs people, and today they come only from the settlers at the camp and from births; there is no
command that puts one on the map. The save's people list already takes a person's position, skills, archery,
job, house, health and post, so a hand-edited save is the stopgap until this is built. **This step is design,
not plumbing - its questions were the user's**, decided 2026-10-18: the first three as recommended, and the fourth
the other way - **homeless allowed**.

1. **What a placed person is.** Recommended: an ordinary villager, put down by a recorded command
   (`CHASM_CMD_PERSON`: plot, figure, age, skills, archery, health and water) and then living by the game's
   rules - jobs, needs, orders - so a test tries the rules the game has. The other way, a fighter outside the
   households and jobs, scripts faster but tests rules the game doesn't have.
2. **Jobs.** Only archers and guards shoot; an archer's job comes only from a range's places, a guard's from a
   tower, and `AssignJobs` fills and re-fills them. Recommended: the test puts down the range or the towers and
   lets the rules staff them, with `chasm_economy`'s guard orders for the rest - rather than a command that
   sets a job and holds `AssignJobs` off.
3. **Needs.** A test kind has no river, so no water collector: water lasts about four days (three minutes at
   1x, current tuning), and a hungry guard leaves his post. Recommended: a store stocked with
   `chasm_economy set_stock`, so needs work as in the game; a "needs held" setting of the kind only if that
   proves too noisy.
4. **A home.** A guard by hand needs a house, and a homeless person goes back to the camp spot - which a test
   kind does not have (`camp_plot` -1, a path nobody has tried). Recommended: the command names the house a
   person lives in, and refuses one without.

What is NOT in this step: a wave spawner (`chasm_zombie` drops up to 40 on one plot per command) and walls
that break (`zombies_plan.md` step 4) - both are the zombies plan's.

### As built: step 5 (2026-10-18)

- **`CHASM_CMD_PERSON`**, recorded, a debug edit: the plot, the home's building id (0 none), the age in days, the
  four skills and the sex packed into one value, the archery - 0 for any of them the seed's, as a settler's.
- **`Economy::AddPerson`**: an ordinary villager, grown up (a child is refused: it would want a mother caring for
  it). Into a named house he joins the family living there, and its surname; a camp's bed is a family of his own;
  with no home, a family of his own and nowhere to live. Then `HouseFamilies` and the staffing at once, as
  `SettleHere` does - the rules take him up on the next tick, not at the zones' next change. Refused on the ground a
  zombie is: the two now share `EconomyDropRefusal` (the zombie's checks, moved out of `SpawnZombies` unchanged).
- **Homeless with no camp** needed nothing: every reader of the camp's spot already takes -1. Such a person stands
  where he was put - at home there, never indoors, so a zombie's to find - is not employed, as nobody homeless is,
  and is not cold under the snow, which is the camp's.
- **Tools**: `chasm_economy` op `add_person` (x,z or plot; `house`, `female`, `age_years`, `skills` by name,
  `archery`, `count` up to 20; `made` lists the ids, `refusal` says why not); the People panel's "Put one down on
  the selected plot" (debug builds) - homeless, the rest the seed's.
- **Posted where put down** (2026-10-19, the user's, for testing archery): `add_person` `post: true`, and the People
  panel's "Put an archer down" (precision 10, its slider's archery, posted) - and the X key, the same archer on the
  plot under the cursor, in either mode, as Z drops a zombie (debug builds only). A bit in the command's packed value
  (`PERSON_POSTED`, clear in older recordings): the handler orders him onto his own plot (`Economy::Order`, spread
  as a group's) on the same tick, so the staffing never hands him work. With archery he carries a bow
  (`EconomyCarriesBow`) and is a posted archer - range_plan.md, "The bow follows training".
- *Checked* (debug, 8771, flat map seed 5):
  - A house of two storeys, a store stocked by `set_stock` (40 wheat, 20 beans, 60 water), a guard tower and a range;
    two put into the house with precision 9 and archery 900 - one family, both De Wit - and three homeless. The rules
    made the two the tower's guard and the range's archer, and housed the three in the one house.
  - Refused: a child (`age_years` 1), a wall plot, the store's plot, and a store named as the home.
  - Six zombies dropped: the guard on his post, arrows flying, one of the three homeless-come-housed killed by a
    zombie in 3,000 ticks; two replays of it equal on all 3,001 ticks.
  - Three put down in the middle of a recording, replayed twice: the same people in both, the traces equal.
  - Three homeless on a map with no house and no camp, taken through winter's first dawn and four days of full snow:
    nothing amiss, no assert; dry after four days, as the needs rules have it.
  - Step 4's A/B again after it, since the zombie's drop check moved: the 34 files the same byte for byte, and the
    6,000-tick replay equal on both builds, ending on the same hash as the first run's.

## Step 6: the title's diorama, made by code

- **`GRID_KIND_TITLE`**: a small map (a few thousand cells), with its hills, its trees placed by its own code,
  and a start that builds the town - houses, gardens, fields with their crops, roads, a palisade, a store, a
  range - standing at once.
- **Its people are markers written by code**, not villagers: each `person` marker's figure, spot, facing,
  clip and a walk along a road, which `DioramaLife` takes as it takes a captured one. So this step does not
  wait for step 5. The chimneys' and the perches' markers come from the town itself, as at a capture
  (`HearthFixtures`, `BirdDioramaMarkers`) - so the title's flock gets its perches without the re-capture
  `birds_plan.md` still waits on.
- **The capture stays the way it reaches the title, in memory**: the title's world generated first as the
  world, its town built, the view uploaded, `SceneCaptureWrite` into bytes and `SceneCaptureLoad` into the
  title's scene - `LoadDiorama` taking bytes rather than an asset name. Then the game's world is generated as
  now. Every look stays one code path: whatever changes the game's buildings changes the title's town.
- **`menu_town.scap` goes**, and with it 4.3 MB of assets the ship exe bakes in. `chasm_diorama` keeps
  capture and load for looking at a real town in the title, in debug.
- **What has to be solved:**
  - *The order at start.* A ship build opens the title at Init, and while the title shows the world is
    neither ticked nor drawn and `PreRender` leaves it alone (`TitleShowing`). So the town has to be built and
    uploaded before the title opens: a few frames of the plain background first, or an upload made on purpose.
  - *Knowing the view has caught up.* Today a person decides when to capture. Code has to know when every
    chunk of the title world's ground, zones and forest is uploaded for the zones it has.
  - *The cost.* A 50k map generates in about a third of a second in debug (grid 125 ms, levels 58, mesh 82,
    the rest under 20 each); a title map of a few thousand cells should be a small part of that, plus the
    frames to upload it. Measured against today's start to the title before it is accepted.
- **No water.** A test kind has no river, and today's diorama has one. A river put into a code-built layout
  by hand - a `GridRiverLine` running off the map's edge - is untried, and the first piece of the authored
  layout `done/demo_plan.md` left open as option C. The town goes without one first.
- *Done when* the title shows the coded town with its smoke, its people, its clouds round the rim and its
  flock landing in its trees; `assets/dioramas/` is gone; and the time from start to the title is measured.

### As built: step 6 (2026-10-18)

- **`GRID_KIND_TITLE`** (`Grid.h`): flat, no chasm, and the game's own forest - but with nothing tall inside the
  town's wall: `BuildForest` takes the trees, rocks, stumps and logs out of the ring after growing it, keeping the
  bushes and ground cover and the props' order.
- **The town** (`TitleTown.h`, `TitleTown.cpp`): `TitleTownCentre` finds the most open spot in the map's middle third
  by the forest's cores; `Zones::PlaceTitleTown`, its kind's start, puts up through `Apply` as debug paint - so
  standing, and by the rules - a main road east and west out through two gates, a cross road, a palisade ring, the
  houses along both roads (half of two plots; about half of two storeys or more, a few of three), gardens behind them, a store at the
  crossroads, a guard tower inside the east gate, a woodcutter's hut out of the west gate, two bands of fields (wheat,
  beans) and the range. Every figure is a named constant at the top of the file, the look's and not a rule's.
  `TitleTownPeople` gives its six people as markers: two walking in from the east gate, a man with a sack at the
  store, a woman in a garden, a man with a log at the hut, an archer at the range.
- **At start** (`ApplicationChasm::BuildTitleTown`, from `Init` before the game's map): the title's map generated
  as the world, `EnsureZonesWorld` running its start, the uploads a frame would run done once (ground, zones,
  forest, range, and the birds' trees for the perches), the capture taken into `title_capture` with the options
  `chasm_diorama` uses (`DioramaOptions`, taken out of `ServiceDiorama`) - then the game's map, as before.
  `AtTickBoundary` is only a lock and the capture reads the meshes' own vertices, so none of it waits on a frame.
  `CreateTitleScene` loads the bytes (`LoadDioramaBytes`, taken out of `LoadDiorama`); `chasm_diorama op load
  name:town` puts it back after a file's.
- **Found on the way, and fixed**: on a build that opens on the title - the ship build - the broadleaves were
  MAGENTA. The palette's summer row, which their green sits on, is written at load but uploaded only with the
  season's first fade, in the world's `PreRender` - which never runs while the title shows. The title's branch of
  `PreRender` now keeps the palette current too. It would have bitten any diorama with summer leaves.
- **`chasm_generate kind:title`** puts the title's map and town in the world, to look at with every debug view.
- *Checked*:
  - Debug: built and captured in 93 ms (83 objects, 43,595 triangles, 1,244 instances, 619 markers); the title showed
    35 chimneys' smoke, 6 people and 9 birds with 488 trees to perch in - no re-capture needed for the flock.
  - Release without ImGui, which opens on the title as the ship build does: 60 ms, the whole map step 339 ms with the
    game's 50k map after it. Green trees once the palette fix was in; Continue into the game showed the game's map,
    nothing of the town left in the world's scene.
- **Left for the user**: the town's look - its numbers are at the top of `TitleTown.cpp`; and deleting
  `assets/dioramas/menu_town.scap`, which nothing loads any more but a ship build still bakes in (4.3 MB), once the
  coded town is liked. Water stays out, as above.

## After: the goals switched off for testing (2026-10-18)

The user's, from testing with one villager: his death ending the colony in play mode is right, but debug mode
should list every goal and let each be turned off so it does not trigger.

- **The switches are the viewer's, not the rules'.** The goals and the ends are rules - worked out on the tick and
  told once into the journal - and only the VIEWER stops the game for them (`UpdateEnd`), as the pause menu does. So
  a switch holds back only the viewer: never recorded or saved, and the journal, the statistics, the goals' panel and
  a replay are as they were. All on in a build without the panels.
- **What can be switched** (`ApplicationChasm.h`, THE SWITCHES; `ApplicationChasmGoals.cpp`): each goal - its notice,
  and the winter's line in a death's pop-up - a death's pop-up, the colony lost's page and its line over the build
  bar, the demo complete's page.
- **Where**: the debug panel's Goals section, each with how it stands and All on / All off; `chasm_menu`'s `triggers`
  with any op (`{"death":false,"colony_lost":false}`), and every reply's `triggers`.
- *Checked* (debug, play mode, flat maps): a lone villager's death opened the colony lost's page with every switch
  on; with colony_lost off a death's pop-up instead; one of two dying brought the pop-up, and with death off nothing
  - the game ran on; both off, nothing, and the journal still had the colony lost. A wrong name is refused. The
  panel seen in a throwaway build with its section moved to the top.
- Session only: a restart puts every switch back on.

## After: the cracks kind (2026-10-18)

The user's: the cracked-mud chasm from testfx (`chasm_cracks.frag`, mode 0, pull-apart) with their bench settings,
as a map kind, to see what that generation looks like in the game - the terrain only, no mountain or biomes yet.
Not a shader in the game: the same field in C++, reproducible from the seed.

- **`GRID_KIND_CRACKS`** (`"cracks"`): a test kind like the others (no layout, no camp, nobody, all explored, no
  autosave), so the chasm-only checks are skipped and the game's forest grows on it as on the title's map.
- **`MudCracks.h`** is the shader's `Sample()` without what only colours it (hairlines, rubble, the lip), sampled
  once per plot in `Terrain::BuildHeights`; then `Terrain::SettleHeights` - the saddle and pit pass every map
  ends with, moved out of `BuildHeights` unchanged - and the kinds as on any map. The bench's uniforms are named
  constants at the top of `MudCracks.cpp`; the bench's seed -7 is `chasm_generate {"kind":"cracks","seed":-7}`.
- **What the game makes of it**: a crack narrower than about a plot falls apart into separate plots, and every pit
  but the chasm is filled - so the shallow cracks away from the rift (`crack_far`) and the cracks' thin ends go, and
  what is left near the rift is trenches a plot or two wide. The rift runs off the north edge (`rift_tip` 1.11).
- *Checked* (debug, port 8769): against the bench at its own map size (540 x 337.5, zoom 1.13) the shapes match
  the user's screenshot - islands, slumped plates, crack network. Seed -7 on the 50k map: plateau 40,056 plots,
  balcony 1,669, island 1,124, bare 156, floor 7,796; 407 pit plots filled, 77 saddles raised; one chasm; each side
  one region with 4 pockets cut off (285 plots). Levels 82-87 ms in debug against the chasm map's 107. The same seed
  twice, another between, gave a pixel-identical frame; the field alone is bit-identical at -O0, -Og and -O3.

## Order and size

Steps 1-3 are a few hundred lines over about eight files, the grid, terrain, forest, zones, economy, save,
menu and app; 4 is a session of A/B; 5 waits on its four questions; 6 is the most new code, in the title and
the app's start. Steps 5 and 6 do not depend on each other: either can follow 4.

## Later, not in these steps

- **Play mode on a test map.** With no villagers the whole map lies under the clouds; every placement is
  refused as outside the settlement; the water, bridge and winch tools stay locked for good with no river or
  chasm to discover (`ChasmToolNeeds`); each death opens the pop-up that pauses the game, and a wiped-out colony
  the end page. Each is a guard on the kind (`ToolsLocked`, `EconomyCommandInTerritory`, `TickGoals` and
  `UpdateEnd`), but what play mode places is a site for builders to build, so it is worth it only once tests
  have people.
- **Scenarios as saves.** With steps 1-5, a combat scenario needs no format of its own: a test map generated,
  its buildings painted, its people and zombies put down, `chasm_save` - and each run a load and a replay. A
  scenario written by a script is the same JSON.
- **An authored layout** (option C): rifts, balconies and rivers drawn rather than generated, saved with the
  map - for tutorials and a hand-made demo map. Code-built kinds do not need it.

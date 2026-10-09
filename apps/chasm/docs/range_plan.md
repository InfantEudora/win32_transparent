# The archery range: the first unit trained

The user's, 2026-10-09. Archers are the first unit the colony trains, and they train at an ARCHERY RANGE: low
cost, placed as a field is, with a few targets and hay bales. `defence_plan.md` listed "the training ground" for
later and `gameplay_plan.md` agreed what training is (archers from the workers, base skill and experience); this
is that, made concrete. Talked through the same day; the user's answers are below. Nothing is built yet.

## Decided (the user, 2026-10-09)

- **A range is placed as a field is**: painted on coarse cells, low cost, with targets and hay bales on it.
- **Its props are modelled in Blender** (the agent's call, and the user's own choice of tool for props), like
  every prop so far.
- **FULL-TIME TRAINEES FIRST**, so it fits the job system as it is (one job a person). The gameplay plan's
  SHARE OF TIME - a woodcutter who trains half his time and cuts half the wood - is a later step, a change to jobs
  of its own, and must not need the experience redone.
- **Wood only for now. Its cost will include WHEAT** (the hay bales' straw - "grain") when sites can take goods
  other than wood: `defence_plan.md`'s step 4, "site costs per good". Until then a range is built with wood alone.
- **Trained all year**, winter and under the snow too - drill while the fields rest.

## Proposed

### What a range is

- **A new kind, `ZONE_KIND_RANGE`** (appended to the kinds), on coarse cells like a field. On the build grid
  beside FIELD, with its own key.
- **Painted by a new op, `ZONE_OP_RANGE_PAINT`** (appended). Not `ZONE_OP_FIELD_PAINT` with a kind: that op's
  value[1] has always been sent as whatever the tool had (`chasm_paint` sends the ground), so recordings carry
  meaningless numbers there. A range is ERASED as a field is (`ZONE_OP_FIELD_ERASE` takes any coarse-cell
  building's cell), and split by an erase the same way.
- **The cells table** (`ZoneState::field`) holds a range's id as it holds a field's: it becomes "the coarse-cell
  building on this cell", and code that means a crop field asks the kind. The field's placing rules apply as
  they are: flat, not touching a building or ground, not on the chasm floor. It is fenced like a field and not
  walked across.
- **A site first**, as a field is: wood a cell (`ECONOMY_RANGE_WOOD_PER_CELL`, 3 - a little more than a field's
  2, for the targets' stands), carried and built by the idle and the builders, "Build first" on its card. A debug
  paint stands at once. **When site costs per good land: 2 wheat a cell as well** (tuning) - noted on the code's
  constant so it is not forgotten.

### The trainees

- **A range is a workplace**: ONE PLACE A BUILT CELL (tuning), filled in the placing order like every
  workplace (`AssignJobs`), by **precision** - a new job, `WORKER_JOB_ARCHER` (appended), whose skill
  (`EconomyJobSkill`) is precision. A trainee's home stays his house; his round starts and ends there, as
  everyone's does.
- **His day**: from home to the range's shooting line - a spot along one edge of his cell, facing its targets -
  and there he TRAINS for a stint (`ECONOMY_TRAIN_SECONDS`), then home, and out again, as a woodcutter goes out
  for each tree. Two new states, appended: `WORKER_TO_RANGE` and `WORKER_TRAINING`. Needs as ever: pressing
  ones send him to a supply first.
- **All year**: no season or snow stops it. A range under the snow is still a range.
- **Posted** (an order): he goes, as anyone does, and his place waits for him (defence step 1).

### Archery: what training gives

- **A new number on every person, ARCHERY** - experience, 0 to 100% - saved with the person and hashed. It
  grows only while TRAINING, at his precision's pace (`EconomySkillFactor(precision)`), and STOPS at his
  precision's cap: 10% a point (precision 6 reaches 60%, no further). The gameplay plan's two layers: base skill
  decides how good he can get and how fast; experience is how far he has got. Tuning (proposed): an average
  trainee (precision 5) reaches his cap in about a season of full-time training.
- **It does nothing else yet** - there is nothing to shoot. It is what the GUARD TOWER (defence step 5) staffs by
  when it is built - archery first, then precision - and what an archer's aim will use when the zombies come.
  Training is the groundwork.
- **Kept when he stops**: a trainee who takes another job keeps his archery. (Losing it unpractised is a later
  thought, if wanted.)
- **The share of time, later**: the same number grows at the same rate per tick trained; splitting a day only
  changes how many of his ticks he spends at the range. Nothing here needs redoing for it.

### What the player sees

- **The job overview**: ARCHERS n / places, amber where a place is empty, as the others.
- **The person's card**: "Archery 35% (of 60%)" as a bar, below his skills; a trainee's line "Trains at the range".
- **The badge**: a BOW for an archer, beside the axe, wheat, bucket, hammer and log.
- **The range's card**: as a site, wood and carriers and Build first; built, its trainees with their archery, and
  "Nobody trains here" when its places are empty.
- **NEEDS ATTENTION**: "NOBODY AT THE RANGE" (row 7 learns the kind). No "build a range" hint - it is not a step
  every colony takes, and the hints are the tutorial's first steps.
- **Arrows in the targets**: a few, more the longer a trainee has stood there this stint - drawn from his state,
  nothing saved. A first touch of shooting, before any animation of it.

### The look and the props

- **The ground**: trodden earth, as a lot's - a field's soil without its rows - inside the field's fence.
- **Per cell** (`CropMesh` lays out crops; the range lays out its props the same way, deterministic from the cell):
  a TARGET at the far end, a row of HAY BALES behind it as the backstop, a bale or two at the sides; the shooting
  line a strip of darker earth at the near end. A cell's far end: the side away from its range's middle, so a
  range of several cells shoots one way. (As built: every range shoots NORTH, the way the camera looks, so the
  targets face the view with their rings - see the end.)
- **Props, modelled in `art_source/chasm/chasm_props.blend`** and added with `blender_chasm_props.py --add` (it
  builds only missing names): `target` (a straw butt on two legs, rings in palette colours), `hay_bale` (a
  rectangular bale, banded), `arrow` (a stub shaft and fletching, for sticking into targets), each in the tens of
  triangles, palette-coloured, origin at the base centre. The user tunes and re-exports them, as with the rest.
- **Since the crates window has the .blend open now**, the props wait for its release; the range can be built and
  drawn with placeholders first.

## Build order

1. Zones: the kind, the paint op, the cells table shared; fence and rules as a field's; the build grid, the ghost.
2. Economy: the range as a workplace (places by built cells, a site of wood); the job, the two states, the trip
   and the stint; ARCHERY grown while training, capped; saves and the hash.
3. The view: the ground and placeholder props; the cards, the overview row, the badge's bow, the issues row.
4. The props in Blender, and the layout using them; arrows in the targets.
5. Checked in the game: painted, built, staffed by precision; archery growing to its cap over the measured time;
   save and load; under the snow.

## Later

- **Wheat in its cost**, with site costs per good (`defence_plan.md` step 4).
- **The share of time**: trainees who keep their trade (gameplay plan, "Time is split").
- **The tower staffed by archery** (defence step 5), and the alarm sending trainees to their posts.
- **Shooting at something**: zombies, and what archery does there.

## As built (2026-10-09)

All five steps, and the wheat: defence step 4 ("site costs per good") landed while this was built, so a range costs
**3 wood and 2 wheat a cell** from the start (`EconomyCellCost`; `ECONOMY_RANGE_WOOD_PER_CELL`,
`ECONOMY_RANGE_WHEAT_PER_CELL`).

- **Changed from the plan**: every range SHOOTS NORTH (`ZoneRangeCellAt`): its targets at each cell's north side
  face the camera, rings and all, and its trainees stand at the south side with their backs to the view. Along
  the range's long side would have shown long ranges' targets edge-on. No line strip is drawn - the trainees
  standing there show it.
- **Files**: `Zones.*` (`ZONE_KIND_RANGE` 10, `ZoneKindOnCells`, `ZONE_OP_RANGE_PAINT` - the field's paint with the
  kind from the op; the crop, next-crop and land ops refuse a range's cell; `ZoneRangeCell` / `ZoneRangeCellAt` /
  `ZoneRangeLine` / `ZoneRangeTarget`), `Economy.*` (`WORKER_JOB_ARCHER` 4, `WORKER_TO_RANGE` 25 and
  `WORKER_TRAINING` 26, `EconomyWorker::archery`, `EconomyArcheryCap`, `GoTrain`, places a built cell, the job's
  skill precision; a store beside a range is not made food-only, as one beside a field is), `ChasmSave.cpp` (a
  range's cells; a person's `"archery"`), `ApplicationChasmEconomy.cpp` (archery hashed once above 0;
  `chasm_economy`'s worker `archery` percent and cap), `ApplicationChasm.*` (tool R, the paint, the hover, the
  panel's radio, `chasm_paint range_paint`, the props loaded), `ApplicationChasmRange.cpp` (new: the targets, bales
  and arrows as instance sets; stand-ins if the GLB lacks them), `CropMesh.cpp` (a range cell staked out, then
  trodden earth), `ApplicationChasmGhost.cpp`, `ApplicationChasmSelect.cpp` (the card, the person's archery bar,
  the bow badge, the outline, Build first), `ApplicationChasmTime.cpp` (RANGE on the grid, the ARCHERS row),
  `ApplicationChasmIssues.cpp` (NOBODY AT THE RANGE), `tools/blender_chasm_props.py` (`range_target` 96 tris,
  `hay_bale` 22, `arrow` 15 - added with `--add`; `blender_chasm_props_check.py --verify` passes them).
- **Arrows**: one in a trainee's target as his stint begins, up to five by its end, each where a hash of the
  cell and its number puts it on the boss - from his state every frame, the set rebuilt only when it changes.
- **Checked** (debug, a copied exe on 8774, paused and stepped, the default seed):
  - a two-cell range painted by debug: two places, taken by the two most precise settlers (8 and 9) against
    their walk; out to the line, training, home, out again; ARCHERS 2 / 2 in the overview, the bow on their badges;
  - archery: precision 8 reached 13% in two game days (about two thirds of his time on the line), so an average
    trainee (precision 5) reaches his 50% cap in about nine days - the season the plan asked for;
  - the look, close up: the fenced earth, a target with its rings at each cell's north side, the bales behind and
    beside, the trainees on the line facing their targets, arrows in the bosses;
  - painted in play: a site, "Wood 0 of 6, Wheat 0 of 4, 2 cells to build - a trainee a cell"; the idle carried the
    wheat from the camp's supplies and then the wood; it stood within 1,200 ticks and two more trainees were taken;
  - saved and loaded: everyone's archery back exactly; in winter, under the snow's first days, they trained on.
- **Not checked by a run**: NOBODY AT THE RANGE (every test colony had people free), and the stand-ins (the GLB had
  the props).
- **Kinds and jobs taken**: `ZONE_KIND_RANGE` is 10 and `WORKER_JOB_ARCHER` 4; the guard tower (defence step 5) and
  its guard job come after them.

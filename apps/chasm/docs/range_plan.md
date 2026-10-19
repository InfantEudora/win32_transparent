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

- **Wheat in its cost**, with site costs per good (`defence_plan.md` step 4) - done, see below.
- **A TECH TREE, or an order things are built in** (the user's thought, 2026-10-09): a range is not something
  built early on, and later something like a tech tree may require buildings in a specific order. For now its
  price says so instead (below).
- **The share of time**: trainees who keep their trade (gameplay plan, "Time is split").
- **The tower staffed by archery** (defence step 5), and the alarm sending trainees to their posts.
- **Shooting at something**: zombies, and what archery does there.

## As built (2026-10-09)

All five steps, and the wheat: defence step 4 ("site costs per good") landed while this was built, so a range cost
3 wood and 2 wheat a cell from the start (`EconomyCellCost`; `ECONOMY_RANGE_WOOD_PER_CELL`,
`ECONOMY_RANGE_WHEAT_PER_CELL`).

**Current tuning: 12 wood and 8 wheat a cell** - four times that, the user's after seeing it (2026-10-09): "a range
is not something you'd build early on". A two-cell range is 24 wood and 16 wheat - more than half the camp's
starting wood (40) and two fifths of its wheat (40). The figures below were measured at the first price.

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
  (Replaced 2026-10-11 by the shots he actually looses: see "Shooting" below.)
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
- **Props inside the fence** (the user's, 2026-10-18: some stood outside it). They were put at fixed reaches north
  and east of the cell's middle, which on a cell turned to the grid's slant passed its fence. Now
  (`BuildRangeProps`) the backstop lies along the fence the range shoots at - a bale where the way north from the
  middle meets it, one either side along it, each against the piece behind it - the loose bale against the east
  fence, and every prop is kept `RANGE_BALE_ROOM` / `RANGE_TARGET_ROOM` inside the cell's outline or left out. The
  target stays where `ZoneRangeTarget` has it unless that is within `RANGE_TARGET_GAP` of the backstop. All view:
  the trainees' line is the rule's, unchanged.
- **Kinds and jobs taken**: `ZONE_KIND_RANGE` is 10 and `WORKER_JOB_ARCHER` 4; the guard tower (defence step 5) and
  its guard job come after them.

## Shooting (2026-10-11)

The user's Mixamo `Shoot` clip, retargeted onto `male_armature` (`tools/blender_retarget.py`), played by the
trainees on the line. Proposed in six steps and agreed the same day ("those are 6 good steps"), with three answers:

- **An archer always carries his bow**, not only on the line. (Guards too, as built: the tower's archers.)
- **Stand-ins first**: a `bow` and a `quiver` modelled later swap in by name.
- **A low skill can miss the target**, not only scatter wider on it.

### The clip, read

One turn is one shot and its first and last frames are the same pose, so it loops. Read off its hands frame by
frame (120 frames): the right hand goes over the shoulder to a quiver by frame 12, swings down to the bow at
26-34, nocks at the hip to 46, raises and draws to 68, holds at full draw, and the hands part at 93-94; then
the arms come down. The clip turns the body side-on by itself, the bow arm along the figure's forward, so a
trainee facing his target (his last leg points north) needs no extra turn. The beats are `SHOOT_TAKE`,
`SHOOT_NOCK` and `SHOOT_RELEASE` (ApplicationChasm.h), fractions of a turn so a re-export at another frame rate
keeps them; a re-animated clip needs them read again.

### As built

All view: the rules still say only "training, timer", so nothing is saved or hashed and a replay is unchanged.

1. **Played while training** (`ApplicationChasmWorkersView.cpp`): WORKER_CLIP_SHOOT. Its phase and rate come from
   his stint's timer (`ShotClockOf`, ApplicationChasmRange.cpp): a whole number of shots a stint - 8 at the clip's
   5.0 s, 10 if it is exported at 30 fps - so he ends with his arms down and walks off without a cut. Clip changes
   now CROSSFADE (`TransitionToAnimation`, `WORKER_CLIP_BLEND`, longer into and out of Shoot, `WORKER_SHOOT_BLEND`),
   which also takes the snap out of walk to idle. To and from no clip (hidden, bind pose) is still at once.
2. **The kit**: a BOW in his left hand and a QUIVER on his back, shown for WORKER_JOB_ARCHER and WORKER_JOB_GUARD.
   The quiver is a child of his Spine bone. The bow is its own object, placed every frame from his hands, read
   between ticks (AtTickBoundary) as the renderer reads them: its grip at his left PALM (the wrist carried on along
   the forearm - the hand bones have no tails); upright and square to his facing as he carries it; square to the
   arrow from the nock to the lowering (`BowAim`). Fixed to the hand bone instead, it measured horizontal and
   pointing forward while walking and standing (its long axis 0.1-0.2 up), upright only at full draw.
3. **The arrow in his hand**, from the take to the release: along his forearm as he brings it over, then from his
   right palm through the bow's grip. Placed from the posed hands the same way, by the clip's own time rather
   than the stint's clock so it never parts from the hands. Fixed to the right hand it was 20-60 degrees off the
   line through the nock and draw. Drawn in the range's `arrow_full` set (a whole arrow; the stuck `arrow` is a stub).
4. **It flies at the release**: from his bow at `SHOOT_RELEASE` (measured on each figure at Init,
   `worker_release_at`) to the target, rising a little (`RANGE_ARROW_ARC`), slowed to `RANGE_ARROW_SPEED` - an
   arrow's own speed would cross a range in a frame.
5. **Where it lands shows his archery** (`ShotOf`): off the middle by a normal scatter, `RANGE_SPREAD_NOVICE` to
   `RANGE_SPREAD_MASTER`, from a hash of him, his stint and the shot. Inside the face (`RANGE_FACE_HALF_W/H`) it
   sticks; outside it misses, flies on and comes down in the ground behind the target, short of the backstop.
   Worked out against the face: a beginner misses about two in five, the average trainee at his cap one in
   eleven, a master next to none.
6. **The target fills with the shots he has made** this stint, each where it went - replacing the 1-to-5 count.

**Found on the way, and fixed: loads ignored where the armature stood.** Loads were moved into their bone's
space by the skin's INVERSE BIND matrix, which starts from the file's space - the armature's place and scale in
it - while `GLTFLoader::GetSkeleton` leaves the armature's node out of the drawn figure. With `female_armature`
moved to x 1.77 in Blender, a woman's log hung about a world unit beside her. Loads, the quiver and the bow now
use the bone's own bind matrix off the probe (`IntoBone`), so the armature's place in the file no longer matters.

**Flagged, not changed: the armature's SCALE is dropped too.** `female_armature` is scaled 0.83 in the file; the
skin leaves that out, so the woman is drawn 2.09 model units tall (0.96 world) against the man's 1.875 (0.86) -
taller than him, where she is shorter in Blender. Her loads and kit are now fitted to what is drawn. Keeping the
scale is a loader change for every app (GetSkeleton), or the scale applied in Blender.

**Checked** (debug, my own exe on 8769, paused and stepped, the default seed, 31 range cells painted by debug so
nine adults and children trained): men and women side-on at full draw, the bow upright with the arrow through
its grip; a woman reaching over her shoulder with an arrow in hand, another nocking at the hip with the bow
square to the arrow; arrows in the target faces and misses stuck in the ground by the bales; the quiver's
fletchings over a man's shoulder. The children are unrigged figures: their arrows fly from a rough bow hand,
with no clip and no kit.

**Not checked**: an arrow caught in the air in a screenshot (a flight is about 11 ticks of each 250); an archer
walking with his bow; the woman's log after the IntoBone fix, close up. The stand-in quiver is brown on brown
tunics and hard to see.

## The bow follows training (2026-10-19)

The user's: **once someone has trained, they always have a bow on them, even as a farmer** - and posted, they are
a posted archer. Other arms by trade may follow the same way later (a farmer's pitchfork, a woodcutter's axe).

- **`EconomyCarriesBow`** (Economy.h), the one test for both the rules (`Shooter`, Shooting.cpp) and the view (the
  kit in `UploadWorkers`): any archery at all, or the ARCHER or GUARD job - a trainee or a guard before any
  archery. Before, it was the two jobs alone, written out in both places.
- **Shooting is unchanged otherwise**: only someone STANDING shoots - on a post, a tower or the line. A trained
  farmer in his field carries his bow and does not shoot; ordered to a post he does, and step 3b's
  `ZombiesInRange` keeps him there while a zombie is in his sight.
- No new state: `archery` and `post` were already saved and hashed. A replay changes only where someone trained is
  ordered out.
- **Flagged, not changed**: the bow is in the LEFT PALM whatever he carries, so a trained water carrier holds it
  beside his pail. A bow on the back while hands are full would be the view's next step.

**For testing** (test_maps_plan.md, step 5): the People panel's **Put an archer down** - precision 10, the
archery its slider says (1-100%), posted where he is put down, so the staffing never gives him work.
`chasm_economy` `add_person` takes `post: true` for the same. The People table's **pack water** and **pack prov.**
columns set what he carries, in units up to a scout's (6 and 3) - `set_needs` `pack_water` / `pack_provisions` -
recorded on `CHASM_CMD_NEEDS`, its new values one up so older recordings leave the pack alone.

**Checked** (debug copy on 8778, flat map seed 5, paused and stepped): put down posted, job none, on his post and
staying there; one zombie dropped 20 away fell to three hits, a shot each 250-tick Shoot turn, still 9 away; a
screenshot of him at full draw, bow and quiver on, his job none. A lone archer with three zombies 11 away died
after one kill - balance, not this change. `chasm_guard_test.py` 7 of 7 passed. A recording with him put down and
a wave, replayed twice: the traces equal on all 1,516 ticks.

## The pace (2026-10-19)

The user's: a better archer shoots QUICKER, not only straighter, and a little randomness so archers do not all loose
together. Rules, not an event: each turn of the Shoot clip at a zombie gets its length as it begins
(`EconomyShotTicks`, Shooting.cpp) - `ARROW_SHOT_SECONDS` (5.0 s, the clip at its own speed) with no archery down to
`ARROW_SHOT_MASTER` (3.5 s) with all of it, then up to `ARROW_SHOT_JITTER` (10%) either way by the shot hash of him and
the tick it begins (never RRandom). Kept for that turn as `EconomyWorker::draw_turn` - saved in the worker's `draw`
array and hashed with the shot - so the release is still one exact tick, `ARROW_RELEASE` into it, whatever his archery
does meanwhile. The view plays the clip at the turn's rate and puts it back in step at each turn's start (its clock the
tick the turn began). Tuning, the three numbers.

- **Practice on the line is unchanged**: `ShotClockOf` (view only) still shoots at the clip's own speed. It could
  follow the same pace, but a trainee's archery grows during a stint, so the shots-per-stint count would now and then
  change mid-stint and the arrows in the target jump - left out.
- **Checked** (debug copy on 8780, flat map seed 5): turns measured 265/248 ticks at 10% archery (242 expected),
  208/197/200 at 50% (212), 184/168/167 at 100% (175); two equal archers (60%) side by side loosed their first arrows
  46 ticks apart. A master two ticks before his release is at full draw on a 184-tick turn. The pack set, held to 6
  and 3, and left as it was by a set_needs without it. `chasm_guard_test.py` 7 of 7 (its waves now 1,825 and 1,925
  ticks, from 2,750 and 2,550). A recording with two archers, a pack edit and a wave, replayed twice: traces equal on
  all 1,516 ticks.

## Sounds (2026-10-19)

The user's: archer's bow sounds on our archers - the creak of the draw and the loose; its landing thud is too cartoony
for here, its swoosh does for a MISS; a HIT gets a sound of the user's own, to come. All presentation: the cue table
(assets/cues/chasm.json) and `SignalShooting` (ApplicationChasmSound.cpp, the physics thread once a pass, as the
zombies' sounds) - no rule, state, hash or recording changes.

- **The files** are archer's own `.ogg`s, copied as they are: `bow_tension`, `arrow_leave`, `arrow_swoosh`.
- **The bow**, any turn of the Shoot clip - at a zombie by the rules' `draw`/`draw_turn`, at the straw by the shot clock:
  from the clip's `SHOOT_NOCK` the scope `bow_drawn` (his id) is open and the creak plays (`on_end: stop`); at the
  release - the rules' own tick for an arrow at a zombie - the scope ends, cutting it, and `arrow_loose` plays. A turn
  given up before its release ends the scope too. The nearest `SOUND_BOW_NEAREST` (6) drawing within the work's reach
  are heard; one coming into earshot mid-turn plays nothing late.
- **The arrows at zombies**, each a scope `arrow` of its own (the rules give arrows no id: who loosed it and when). A
  miss signals `arrow_miss` every pass of its flight with `in` the passes until it comes down (ticks over
  `physics_time_factor`), and the row's `forecast` lands the swoosh's loudest part there, once. A hit signals
  `arrow_hit` once, where it went in, as it does - an arrow that fells its zombie is gone that very tick, so where it
  was going is kept from the pass before. Its sound is the user's own take, `arrow_hit.ogg` (added the same day),
  a plain row with a little pitch - checked live in a muted copy: one play for each hit.
- **Not heard**: practice arrows on the range (the view's alone, ApplicationChasmRange.cpp) - only the bow there.
- Gains (0.45 creak, 0.6 loose, 0.5 swoosh, all on `activity`, pitched a little) are first guesses for the ear.
- **Checked** (a muted debug copy, live at 1x): two archers' creaks begun on the nock and cut on the pass the loose
  played, about 1.8 s apart at 90% archery; swooshes about 0.4 s before each miss came down; with a throwaway probe
  row (since removed), `arrow_hit` once for each of six hits.

**A test save, `saves/archers_test.json`**: the flat test map (seed 5), four archers posted in a loose line with open
ground to the west - 25, 50, 75 and 100% archery, precision 10, packs full (6 water, 3 provisions) - and nothing
else: no houses, no store, no zombies. Load it and drop zombies to the west (Z, or the Zombies panel). Checked: loaded
on another map, all four back on their posts with their packs and archery, and shooting at three dropped 20 away.
Like every save it is not migrated, so a rule change may one day refuse it; make it again with `add_person` `post`.

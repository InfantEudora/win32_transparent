# Birds Plan: flocks that rise out of the trees

Proposed and agreed 2026-10-17, nothing built. The user wants the world to feel more alive for the demo: a few
flocks of black birds flying around and, most importantly, **flying up out of a tree that is being cut, or when
a villager walks through the forest**. The user will gather the sounds: magpie, crow and raven calls.

## What the user asked for (2026-10-17)

- **Birds change nothing in the game.** They are the first animated thing in Chasm that is not state.
- **Modelled with the props in Blender**, and animated with **shape keys rather than an armature**, if that
  works.
- **Black birds first.** For now every bird looks and behaves the same.
- **A few flocks fly around.**
- **They fly up out of a tree being cut, and when a villager walks through the forest.**

## What the user decided (2026-10-17, on the proposal)

- **Birds freeze when the game is paused**, like the mist and the smoke. They run on the game's clock.
- **The props script makes the first bird**, with its three shape keys, so the export path is proven end to end.
  The user reshapes it afterwards.
- **Calls from birds you can see are cue-table events**, alongside the score's bird ambience. This is an
  exception to the sound plan's "nature is the music's" rule, noted there as well.
- **Larger than true scale: about half a villager's size.** That is roughly twice a real crow beside them, the
  same exaggeration the trees have.
- **A flock over the title scene is the first of the ideas to be built** (step 5). The other ideas are kept as
  noted, for later.

## As built (2026-10-17)

**Steps 0, 1 and 2 are built, and step 4's homes across the map.** Sound (step 3), the clouds and seasons of
step 4, and the title flock (step 5) are not. The game side is `Birds.{h,cpp}` (the flocks, engine-free) and
`ApplicationChasmBirds.cpp` (the world they're told about, the objects, the `chasm_birds` tool). The wiki page
is `wiki/birds.md`. The figures are the constants in `Birds.h`; the tuning table below was the starting point.

**Step 0, the bird** (a worker window):

- `bird` in `chasm_props.glb`: 80 triangles, ONE primitive, wingspan 0.85, beak to tail 0.43, origin between
  the feet, nose at glTF +Z.
- Keys `WingsUp`, `WingsDown`, `Folded` in that order; slot 3 is free. `Folded` also sits the body up 15°, so a
  perched bird sits rather than lying flat.
- Palette row 11 is `PAL_CREATURES` (`make_palette.py --creatures`).
- The props tool exports shape keys, and `--merge` carries their sparse accessors across.
- Previews are in `art_source/chasm/previews/bird_*.png`.
- **Open:** a bird the user has reshaped can only reach the GLB by a whole export. `--merge` refuses a name the
  GLB already has, and `--replace` rebuilds it from the script. A "re-merge these nodes" option would close it.

**Seen in game** (a muted test copy on `village_year5`):

- Birds perch on the pine tips and flush with a stagger. They climb away together, regroup, and settle about
  35 units off.
- Walkers flushed the colony's flocks without the tool. So did the woodcutting, once the axe drew the flocks
  in (below).
- At full zoom-out they read as crow shapes over open ground and water, and are nearly lost over dark pine
  forest. Size (`BIRD_DRAW_SCALE`) and colour (row 11) are the user's to judge.
- Cost: 42-49 µs a frame for 84 birds in the debug build, flying or not, so no off-screen level of detail
  is needed yet.

**Decided while building (mine, for the user to overrule):**

1. **The colony's flocks follow the axe.** Homed only in the forests' cores, no flock was flushed in a minute
   at 4x: people came within 14-50 units of them, never within reach. So `BIRD_COLONY_FLOCKS` are homed round
   the colony. Moving on, they settle near a tree a woodcutter felled at in the last `BIRD_LURE_MEMORY_S`, clear
   of the chopping itself, so the next tree or the walk past puts them up.
2. **The clock is real time times the play speed,** zero in a pause. The tick count moves in 20 ms steps,
   which a bird at 5 units a second visibly stutters on.
3. **A flush is one way out for the flock.** Every bird straight away from the scare scattered them like a
   burst shell.
4. A flock whose woods are all felled finds a new home in the nearest trees left.

**Wanderers, added the same day at the user's ask** ("a few birds fly around in small flocks and single"):

- A flock's role is now `BIRD_ROLE_FOREST`, `_COLONY` or `_WANDER`. There are `BIRD_WANDERERS` wanderers of
  one to three birds (`BIRD_WANDER_ALONE` of them single), half homed round the colony.
- They sit 8-25 s, fly to trees at least `BIRD_WANDER_AWAY` off, circle 4-12 s, and fly on rather than land
  half the time, a little higher than the flocks.
- Every flock now travels on a turning, gently wandering way (`BIRD_TURN_RATE`, `BIRD_WOBBLE`) instead of a
  straight line.
- Seen in game: at any moment 4-8 of the 8 are in the air, three of them over the colony's woods. The step
  costs 60-100 µs a frame in debug, up from 42-49.

**Not yet:**

- The calls are made (`BirdCall`) but dropped until step 3.
- There is no debug panel or view layer. The tool's `status` gives each flock's state, `flushes` and
  `scare_gap` (how near the nearest scare came, past its reach).

## The one rule: birds read the rules, and the rules never read the birds

Birds are view, the way the mist, the chimney smoke and the people's walk cycles are:

- **Not saved, not hashed, not in the recording.** After a load the flocks start again from their homes.
- **They never draw from the shared `RRandom`.** Every choice comes from a hash of the world seed, the flock and
  the bird, as the sound's `Draw()` does it. Drawing from the shared stream is the one way birds could break
  replay (see the open `rrand` item in the memory notes).
- **They read only published snapshots**: `GetWorld`, `GetEconomy`, `GetZones` and `GetExploration`, as
  `UploadWorkers` does. They send no commands.

So a bug in the birds can look wrong, but it cannot change a colony. A replay may show different birds,
which is fine.

## Shape keys, not a rig: the user's instinct holds up

The engine applies shape keys **on the GPU, per object**. `Object::morph_factors` goes into each instance's
`instance_data` (`Renderer.cpp:520`), and `default.vert` adds the deltas. So N birds sharing one mesh are
**one instanced draw per pass**, each bird with its own wing position. A frame's CPU cost is a transform and
three floats per bird.

For comparison, a skinned figure costs about 7 µs of render CPU per frame plus 2.3 µs per tick
(`chasm_villager_animation`, measured 2026-10-10). Sixty skinned birds would cost as much as sixty villagers.
With shape keys, birds cost close to nothing.

Three constraints follow:

- **Each bird is its own `Object`**, not an instance set. An instance set gives every instance the owner's
  morph factors (`Renderer.cpp:482`), so the whole flock would flap in step.
- **The engine has 4 shape key slots** (`NUM_MORPH_FACTOR_SLOTS`, `MAX_MORPH_TARGETS`). The bird uses 3 and
  keeps 1 spare.
- **Shape keys move positions, not normals.** `default.vert` never reads `morph_vertex::normal`, so a raised
  wing is lit as if it were level. On a near-black bird this hardly shows. If it does show, the fix is a small
  core change: about four lines in `default.vert` and `default_skinned.vert`. It would change how archer's bow
  and spider are lit very slightly, but not their replay.

## Step 0 - the bird, in Blender and in the GLB

**The model** goes in `chasm_props.blend` alongside the props. It is **generated by
`blender_chasm_props.py`** as a new asset, shape keys included, as the stone props were. The user then reshapes
it in Blender: Blender edits the basis with the keys in place, so the keys survive a reshape.

- **Low poly**, about 40-80 triangles: body, head and beak, a fanned tail, and **two segments per wing** so a
  wingbeat bends at the wrist instead of flapping like a flat paddle.
- **The origin is between the feet**, so a perched bird sits on a point of the tree. It faces the same way the
  villagers' figures do, so one yaw rule serves both.
- **The basis shape has the wings spread level**, which is the glide pose.

| Slot | Shape key | Used for |
|---|---|---|
| 0 | `WingsUp` | top of the stroke; the outer segment lags behind the inner |
| 1 | `WingsDown` | bottom of the stroke, wingtips swept back |
| 2 | `Folded` | perched or hopping: wings along the body |
| 3 | spare | later `Peck`, head down, for ground feeding |

A wingbeat drives one key at a time from the glide pose: `up = A·max(0, sin φ)`, `down = A·max(0, -sin φ)`.
A single key driven from -1 to +1 would shorten the wing at both ends of the stroke. Two keys keep the
distortion small.

**The colour** goes on a new palette row, `PAL_CREATURES` (row 11, currently spare). Its cells are crow black,
a blue-black sheen and a beak and legs grey, with magpie white added later. The black is very dark grey-blue
rather than `PAL_VOID`'s pure black, so the facets still read. It sits off row 0 so it never fades with the
seasons.

**Getting it into the GLB** needs two tool fixes in `apps/chasm/tools/blender_chasm_props.py`:

- **`export_morph=False`** is set in `EXPORT_SETTINGS` today, so the shape keys would be dropped. Turn it on.
  No other prop has shape keys, so nothing else changes.
- **The `--merge` step drops the keys' data.** It remaps a primitive's `attributes` and `indices` but copies
  `targets` unchanged, so the shape keys would point at the wrong accessors. Remap `targets` the same way.
  Blender writes shape keys as sparse accessors, and core has read those since 2026-09-22.

Add the bird with `--add --merge`, as the stone props were, never with a whole export (`chasm_stone_props`).
`blender_chasm_props_check.py --verify` should list the three keys.

**Done when** `chasm_birds op bench` puts one bird in front of the camera, flapping, gliding and folding in a
paused game, and the screenshot looks right.

## Step 1 - one flock that lives in the trees

Two files, following chasm's rules/view split:

- **`Birds.{h,cpp}`**, engine-free. It holds flocks, birds, roosts and `Step(dt, disturbances)`, which
  outputs poses. It has no Objects, no GL and no sound.
- **`ApplicationChasmBirds.cpp`**:
  - `BuildBirdScene` creates the Objects: one mesh, the palette material, visual only, not pickable, casting
    shadows.
  - `UploadBirds` runs from `PreRender` on the render thread, where every other view object is posed. It steps
    the birds by the change in `SimSeconds()` in fixed sub-steps, and caps the steps so a long frame does not
    teleport them.
  - `chasm_birds` is the MCP tool.

**The birds' clock is the game's** (the user's decision). A paused game holds them in mid-air, like the mist
and the smoke, and at 3x they fly three times as fast, like everyone else.

**Perches**:

- **Only standing trees**: `ForestIsTree`, prop state standing, not hidden under a house or field.
- **Perch points come from the mesh, worked out once per look at load.** These are the centres of upward-facing
  triangles in the top third of the crown: the tip of a pine, several spots across an oak. Each look (kind and
  season stage) gets its own set, so a bare winter oak has its own perches and no bird floats where the leaves
  were.

**A flock's behaviour**:

- **Perched:** birds sit with `Folded` on and turn their heads (a little yaw). Now and then one hops to a perch
  beside it.
- **Restless:** every so often the flock moves to another roost in its forest, so the sky has life even when
  no one is near.
- **Flying:** the flock follows a smoothed path to its next roost. Each bird adds loose flocking (keep apart,
  keep together, match heading) and a flap rhythm with a phase of its own. Glides come between flap bursts.
- **Landing:** the flock circles the new roost, glides in, flares with `WingsUp` held, and folds.

Birds fly at a cruising height above the ground, which is sampled ahead of the flock so a hill never cuts
through them. Their shadows land on the ground and on the trees, which shows how high they are.

## Step 2 - flushed: the moment the user asked for

A **disturbance** is a point, a strength and a radius, gathered each frame from the snapshots:

| Disturbance | Read from | Strength |
|---|---|---|
| a tree being cut | the test `SignalWork` already makes: `WORKER_CLEARING`, or a woodcutter `WORKING` at a standing `prop` | strong; the first blow, then again while the chopping goes on |
| a tree felled | `EVENT_TREE_FELLED` in the event ring (its prop drops out of the perches) | strongest |
| a villager walking through | anyone out of doors, moving, within reach of a perched flock | mild |

**A flush:**

1. **One sentinel calls first.** Then the birds within the radius take off with staggered delays of 0-0.4 s,
   nearest first. The stagger is what makes a flock read as startled rather than scripted. Birds farther off
   in the same flock follow a beat later.
2. **They climb steeply and scatter.** The flap rate doubles, each bird heads away from the disturbance with
   its own spread, and they call.
3. **After 2-4 s they regroup.** The pull to stay together ramps back up, and the flock picks a new roost at
   least a few radii from the disturbance.
4. **They stay wary.** The flock will not land near that spot again for a while. A woodcutter working a stand
   of trees keeps it empty of birds until he stops.

## Step 3 - the sounds

**The files the user gathers.** Single, dry takes, with no other birds and no ambience under them, so they can
be layered and timed:

- 3-5 crow caws and 2-3 raven croaks;
- 2-3 wing bursts: several birds taking off at once;
- optionally a single flutter, for a hop or a landing.

**Rows in `assets/cues/chasm.json`.** Sounds go through `tools/sound_ogg.py` as usual (`ogg_vorbis_settings`):

- **`birds_flush`**: the wing burst at the flock's centre, with distance falloff.
- **`bird_call`**: one call. These are timed on a per-flock clock, the way woodcutters' chop sounds are timed
  per woodcutter: now and then while perched, more often in flight, and a quick overlapping burst on a flush.
  Each call passes the bird's id as the instance, so `pitch_jitter` varies it.
- **Only the nearest few flocks** within reach are heard, like `SOUND_WORK_NEAREST`.
- **They play on the bus that holds in a pause**, since the birds hold too.

**Handing sounds over:** birds step on the render thread, but cues are signalled on the physics thread. Birds
put their signals in a small locked list, and `UpdateSound` drains it, the way `Say()` hands over the voices.

**Decided: both kinds of bird sound stay.** The sound plan says "nature is the music's", and the score already
has `birds` layers. Those are songbird beds with no place in the world. These calls come from birds you can see,
placed and timed by what they do, so they are events.

## Step 4 - a few flocks across the map

- **Homes:** about a dozen flocks for the whole map (current tuning), homed in the largest forests and spaced
  apart, so a play view at its full zoom-out usually has one or two flocks in or near it.
- **Simulated at two levels of detail.** Off screen, only the flock's centre moves and no per-bird flocking
  runs. A flock coming into view takes up full detail where it is.
- **Under the clouds:** a flock over ground no one has explored is not drawn. This needs a check: a bird over
  the edge of the clouds must not give away what lies beyond.
- **Seasons:** larger flocks in autumn and winter, which is when crows and rooks gather. Black birds on winter
  snow will read well.

## Step 5 - a flock over the title scene

The first of the ideas to be built (the user's choice). It needs only steps 0 and 1, so it can be pulled forward
if the title matters sooner than the flocks across the map.

- **The title is a scene of its own** (`ApplicationChasmTitle.cpp`): the world is neither ticked nor drawn
  under it, and its animations run on the title's own clock. The flock steps in `UpdateTitleView`, beside the
  diorama's walkers, with the title's `dt`, and uses the same `Birds.{h,cpp}`.
- **Perches come from the diorama.** `menu_town.scap` already carries markers for the chimneys and the people,
  which `DioramaLife` brings to life. The capture adds a `perch` marker for each perch point on the standing
  trees within its radius. The flock circles the town, settles in its trees, and moves on now and then.
- **No perch markers, no landing.** Until the diorama is captured again, the flock only circles and passes
  over, so this works with today's `menu_town.scap`.
- **The diorama's woodcutter flushes them.** If a person marker is chopping, his tree is a disturbance, as in
  step 2. This is a small touch, but it shows the game's best moment on the title.
- **Silent at first.** The world's sound buses are held under the title, and the title has its music. Calls
  under the title can be added later if the user wants them.

## Current tuning

These are starting figures, not rules (`tuning_numbers_not_rules`). They become named constants in `Birds.h`.

| | |
|---|---|
| birds per flock | 4-10 |
| flocks per map | 12 |
| size | about half a villager's size (the user's): 0.43 units from beak to tail beside a 0.86-tall villager, so a wingspan of about 0.85, twice a real crow's; tune by eye at zoom 55 |
| cruising speed / flushed | 5 / 8 units per second |
| cruising height | 5-8 units above the ground |
| flap rate | 3.5 per second, doubled when flushed |
| reach of a disturbance | walking 4, chopping 8, felled 12 units |
| wariness | 20 s before landing within twice that reach |
| restlessness | a new roost every 30-90 s |

## Checking it

- **`chasm_birds`**, the MCP tool: `status` (flocks, states, birds drawn), `flush` at an x and z, `bench`,
  `home` (put a flock near the view), `list`.
- **Debug panel and a `birds` view layer:** roosts, perch points, disturbance radii, each flock's path.
- **Run on a muted test instance** on port 8769, with `include_ui:false` screenshots. Read
  `chasm_test_instance_autosaves` before loading a save.
- **Cost:** `chasm_crowd_bench`-style timings with all flocks in view. The target is under 0.1 ms per frame.
- **A wiki page** `wiki/birds.md` once built, like any other thing in the game.

## Ideas, noted for later (the user liked them, 2026-10-17)

Each one is its own small step. The flock over the title scene was the first one taken up (step 5).

- **Crows warn of zombies.** Crows mob predators. Birds rising and calling at the forest edge where zombies are
  walking would tell the player something is coming before it is on screen. This is pure view: the rules are
  untouched, but it is the one idea here that would change what the player knows.
- **Ravens over the chasm.** Ravens nest on cliffs. A pair soaring on the updraught *below the rim*, wings
  spread on the glide pose and hardly flapping, would show the chasm's depth better than anything else could.
- **Crows on the fields.** They land on fields that were just sown or harvested and walk and peck (the spare
  `Peck` key). A farmer walking through flushes them.
- **Magpies, the second kind.** A table row with its own mesh colours, calls, flock size (pairs) and boldness.
  Magpies tolerate people, so they would perch on roofs and fences in town. Step 1's flock settings can be a
  table with one row from the start, so this is a row rather than a rewrite.
- **A click scatters them.** It costs nothing and players find it. It must never take a click meant for the
  game, so only a click that picked nothing else would count.
- **A murmuration over the chasm in autumn.** Hundreds of starlings would be the most striking thing the flock
  code could do. At that count, birds would need an instance set with the flap worked out in the shader, which
  would mean a new `INSTANCE_MOTION_*` mode in core. Only worth it if the demo wants a "wow" moment.

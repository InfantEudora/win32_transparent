# Replay determinism: a state hash, and the leaks it has to close

Status: **DONE 2026-09-28** (sections 7-10). Every recording replays bit-identically on every
tick - fresh app or warm, in any order, debug or release - and the baselines (`.cues` and
`.trace`) are written from that state.

The point of the deterministic sim (step 7 of the deterministic-sim plan: record and replay) is that a
recording replays to the same result every time - in a fresh app, after other replays, in any
order. Today it does not, and the check we have cannot tell us how far off it is.

---

## 1. What is checked today, and why it is not enough

`tools/cue_replay.py` replays each recording and compares the **cue log**: which sound fired on
which tick with which gain. It is a coarse proxy for state. Two runs can end centimetres apart and
print identical lines - on 2026-09-25 replays ended 0.03 units apart and the log could not have
shown it. A divergence is caught only once it has grown big enough to change a sound, many ticks
after it started, with nothing to say where.

"All same" from `cue_replay.py` means "the same sounds", not "the same run".

## 2. What we know is wrong

Found 2026-09-28 while rewriting the cue baselines.

1. **State leaks from one replay into the next.** Seven recordings replayed back to back in one
   app, then again in the same app: the second pass differed (`archer_20260925_140425`: a kick
   that had missed now landed, an extra heartbeat). Every pass from a freshly started app agreed.
   `RestoreRecordingState` calls `NewGame`, which rebuilds the Stage, blocks, props, straw men and
   stuck arrows and resets the body clocks - so the leak is in what it does NOT reset.
   **Located with the trace (section 7): the physics world.** From a fresh start every physics
   body repeats exactly, every tick of every recording. In a warm app they part at tick 1 or 2,
   and the first to part are always props `NewGame` rebuilt: `crate_0`, `crate_1`, `target_6`,
   `target_7` at tick 1, or `stand_5`, `stand_8` at tick 2 - identical at tick 0, parting as they
   settle. The props are rebuilt identically; the rp3d world they are added to is not new. It
   keeps its internal arrays (which reorder when a body is removed), its broad-phase tree and its
   id counters from the bodies it had, so the same bodies are solved in a different order and the
   last bits differ. Fix: a fresh physics world on restart (the terrain, which deliberately
   survives a restart, re-added to it), rather than trying to make rp3d forget.
   **Fixed in the rp3d fork instead** (the rp3d agent, 2026-09-28): `PhysicsWorld::
   rebuildInternalState()` (local fix) leaves the world internally as if it had just been built
   from the bodies and joints it holds, in the order they were created - so nothing on the app
   side has to be torn down and re-added. `ApplicationArcher::RestoreRecordingState` calls it as
   its very last statement, after every rebuilt body and placement. See `PhysicsWorldState.cpp`
   in the fork for what it rebuilds.

2. **Her animation state is not restored** - the prime suspect for (1), found when the recorder was
   built (2026-09-25) and left open. There is one `ArcherModel` and one `Puppet` for every scene:
   the playing clip, the blend and its factor, the clip phases, `clip_yaw`, the loose-leg chains.
   Root motion comes out of the clip phase, so a replay that starts mid-blend walks a different
   distance, and from there a kick connects or not and an arrow leaves from somewhere else.

3. **A focus change released the replay's keys.** The write run of `archer_20260925_145919`
   loosed an arrow at recording tick 137 while the recording holds `draw` from 96 to 318; no other
   run did. First suspected: live key-ups - but `DrainAndApplyEvents` already drops all live input
   for recorded actions while a replay runs. The path around it was the focus-loss release in
   `ApplyTickInput`, which let go of EVERY held key, the replay's included. A minimised window
   still gets activated (Windows hands it the foreground when the window above it closes), and
   the next click elsewhere is a focus loss. **Fixed** (step 1): that release now skips the keys
   a running replay owns.

4. **Not a problem: key auto-repeat.** Recordings carry runs of `down draw` with no `up` between
   (the OS repeating a held key). `ApplyTickInput` ignores a down on a key already held, live and
   in replay alike, so they are inert. They could be dropped at record time for tidiness only.

## 3. The state hash

### When

**At the end of every tick.** Not only on input ticks:

- the known leak (2.2) diverges BETWEEN inputs - a phase, a blend - and would surface only at the
  next input, far from its cause;
- long stretches have no input at all (a fall, an arrow's flight, a straw man settling), and
  would go unchecked;
- it costs nothing worth weighing: one 64-bit hash a tick is 3,600 a minute.

**The end is sufficient** for detection, because the end of tick N is the start of tick N+1 -
*provided nothing changes simulation state between ticks*. That proviso is exactly a bug class
this engine has (`UpdateView`/`BeginPass` run on every physics pass, ticking or not - see the
"input has two clocks" note; the engine poses her before the tick; the UI and MCP write state).
So there is a **start-of-tick hash as a diagnostic mode**: `start(N+1) != end(N)` names mutation
that happened outside the tick. Off by default.

### What

Bit-exact over the raw bytes of each value - **never rounded**. Rounding does not make the hash
tolerant (a value near a rounding boundary still flips), and it hides a small divergence that will
grow. Same exe, same machine, same inputs, same start state must give the same bits; anything
else is a bug to find, not noise to filter.

Per component, so a mismatch says WHAT diverged, then a total:

| component | contents |
|---|---|
| `body` | Stage: position, velocity, mode, facing, aim, on_ground, coyote, level tick |
| `rules` | the rest of Stage: vitals, arrows, crumble groups, spring plants, bridge, zones |
| `anim` | Puppet state; base and blend clip, their time indices, blend factor; `clip_yaw`; leg chains |
| `props` | every prop body's rp3d transform and linear/angular velocity, in a fixed order; straw men |
| `view` | the view-side clocks that decide signals: breath, heart, blink, chest; footstep state |
| `cues` | the cue history (last picks, last firings) |

Hair and chest scale are visual and read by nothing in the sim, so they are left out - adding them
would only make a render-order question look like a determinism failure.

### Where

- **core**: a small hasher (FNV-1a or xxHash64 over bytes) and an `Application::HashSimState`
  hook the app fills per component. During a replay the core collects `(tick, components)` at the
  end of each tick; an MCP tool (`replay_trace`) hands the list back.
- **archer**: `ApplicationArcher::HashSimState`.
- **tools/cue_replay.py**: writes `recordings/<name>.trace` beside `.cues` with `--write`, and on a
  check reports the FIRST tick whose hash differs and which components, before the cue diff.

## 4. Floating point and compiler options

Within **one exe on one machine**, compiler options do not matter: the same instructions run on
the same inputs and give the same bits. What breaks that is not the compiler but the program -
uninitialised reads, races between threads, iterating a container keyed on pointers (the order
follows allocation addresses, which differ run to run), wall-clock time or an unseeded random.

Between **our debug and release exes** results *can* differ, though our flags avoid the usual
culprits (`engine.mk`: debug `-Og`, release `-O3`):

- no `-ffast-math`, so GCC never reassociates or approximates;
- no `-march=native`/`-mfma`, and `-std=c++17` (ISO mode) defaults to `-ffp-contract=off`, so no
  multiply-add is fused - fusing rounds once instead of twice and changes the last bit;
- x86-64 does float arithmetic in SSE registers, so there is no x87 80-bit excess precision.

What remains: GCC folds math on constants at compile time with exact arithmetic, where the
runtime library may round the last bit differently, and optimisation levels change what
undefined behaviour does. So **debug and release probably agree, but it is not guaranteed** -
test it once (section 6); if they differ, keep baselines per configuration.

**Across machines**, the same exe gives the same results for plain SSE arithmetic on any x86-64
CPU; differences come from code that picks a path by CPU at runtime (some math libraries, SIMD
dispatch). Cross-architecture replay is out of scope by decision.

## 5. Steps

1. **Core: replays take no live key input.** DONE - live input was already dropped; the
   focus-loss release was the gap (2.3).
2. **Core: the hasher, the hook, the per-tick collection and `replay_trace`.** DONE.
3. **tools/cue_replay.py: `.trace` files**, first-divergence report. DONE.
4. **Archer: mark the show and add its own parts.** DONE - section 8. `SetVisualOnly` on
   the leaves, grass clumps, foliage, vine leaves, fireflies and streaks (and the hair and chest
   bones if they are to stay out - they are driven by the view's sims, not the rules); then
   `ApplicationArcher::HashSimState` calls the default for the objects and adds the parts only the
   app can see - Stage, the Puppet and animation, the body clocks, the cue history (section 3).
5. **Fix the leaks** in the restore: the Puppet and animation state (2.2) DONE, section 9; a fresh
   physics world (2.1) with the rp3d agent.
6. **Run the matrix** (section 6), then re-write baselines, `.cues` and `.trace`, from the
   finished state.

## 6. Tests - the pass condition

Every recording identical, trace and cues, in each of:

- **fresh**: one recording in a freshly started app, twice (two app starts);
- **warm**: all recordings in one app, then all again in the same app;
- **order**: all in one app forward, then reversed in another;
- **hands off vs typing**: one replay while someone types in another window - the test for 2.3,
  which step 1 must turn from a difference into a match;
- **debug vs release** (`CONFIG=release`): once, to learn whether one set of baselines serves both.

A trace mismatch is reported by tick and component, so a failure in any of these points at what
the restore missed.

## 7. Built 2026-09-28 (core and tool)

- **`core/StateHash.h`**: FNV-1a 64 in named parts; `Add` takes plain data (fields, not padded
  structs), strings with their length; `Hex` gives the 16 digits the tools compare.
- **`Application::HashSimState(StateHash&)`**, virtual, called at the end of every tick while a
  replay runs (`TraceTickEnd`, after `UpdatePhysics`, before `UpdateView`), kept by recording
  tick. The default hashes every object's name and local transform, split into `bodies` (objects
  with a rigid body, plus their linear and angular velocity) and `objects` (the rest). Names, not
  ids: a rebuilt object gets a new id and is still the same object.
- **Start-of-tick check** (`f_trace_tick_starts`, `replay_trace {"tick_starts": true}`): the
  same hash before the tick's input, compared with the last tick's end; the parts that differ
  are listed on the tick as `changed_between`.
- **`replay_trace`** MCP tool: the last replay's trace (tick, total, parts, changed_between);
  `detail` splits `bodies` into one part per body (`body<n>:<name>`) to name the one that parts.
- **`Object::f_visual_only`** (`SetVisualOnly`/`IsVisualOnly`, copied by the duplication
  constructor): pure show - the default hash skips the object and everything under it. The
  user's call: leaves have no effect on anything and are excluded, and an object carries the
  flag rather than the app listing them. A promise the app makes, not a switch: marked on
  anything the simulation reads, it hides the divergence the hash is for.
- **`InputController`**: the focus-loss release skips keys a running replay owns (2.3).
- **The physics world's own hash** (the rp3d agent): `PhysicsWorld::computeStateHash()` (local fix
  in the fork) returns FNV-1a 64 parts over the raw bits of everything the next `update()` reads -
  `world` (settings), `bodies`, `colliders`, `order` (the layout of every component array the
  solver walks), `broadphase` (the AABB tree and free list), `pairs`, `contacts` (warm-start),
  `joints`, `constraints` - and a total. The default `HashSimState` adds it as one `physics` part,
  or with `detail` as the nine `physics.*` parts. It sees what no object shows: two worlds that
  differ only in order are caught on the tick they differ.

**Audit, 2026-09-28: nothing the leaves and wind use draws from `rrand`.** No archer code calls
the engine's `RRandom` at all; `Leaves.h`, `Foliage.h`, `Vine.h` and `PlaceHash.h` take their
variety from position hashes, and the debris and cues from `CueHash01` on the level tick.
`ParticleEmitter` (the one core user of `rrand` besides the debug panel) is not used by archer.
No `rand()`, no `<random>`; the clock is read only for timing readouts, the loading screen, the
cue table's once-a-second hot-reload poll and a tool's wait loop. It must stay that way: a
visual-only object drawing from a shared stream moves every draw the simulation makes after it.
- **`tools/cue_replay.py`**: `--write` also writes `recordings/<name>.trace`; a check reports
  `state same` or `STATE DIFFERENT from tick N, in <parts>` before the cue diff. `--tick-starts`,
  `--detail`. The existing `.trace`-less baselines report "no state baseline" until re-written.

**Measured on archer** with the default hash (all seven recordings):

| comparison | `bodies` | `objects` |
|---|---|---|
| fresh app vs fresh app | identical, every tick | differ from tick 0 |
| fresh pass vs second pass, same app | part at tick 1-2 | differ from tick 0 |
| forward vs reversed order | part at tick 1-2 (one at 63) | differ from tick 0 |

`objects` differs from the first tick even between two fresh runs and "changes between ticks" on
nearly every tick: in archer it is mostly show - grass, leaves and hair moved by the view each
frame - which is why archer needs its own `HashSimState` (step 4) rather than the default.
`bodies` is the useful signal today: exact when the history is the same, and it located 2.1.

## 8. Step 4, built 2026-09-28: archer's own hash

**What was marked visual-only** - found by measuring, not by reading: `replay_trace {"detail":
true}` splits `objects` by top-level object too, and two fresh runs with `tick_starts` named every
object that differed or was moved between ticks. Grass never appeared (it bends in its shader, not
as objects). The list was `Main Camera`, `background`, `Sun` (all follow the view), the
`firefly_light`s, `wind_leaves`, and `archer_model` - through its hair bones, which a wind published
from another thread moves. Marked: the camera (in core, for every app - it is the view), the
backdrop, the sun, the leaves group, the fireflies and their lights, the streaks, the hair bones.
After it, `tick_starts` reports nothing changed between ticks.

**`ApplicationArcher::HashSimState`** = core's `bodies` and `objects`, then `Stage::HashState`
(`her`, `world`), `Puppet::HashState` (`puppet`), `anim` (the model's current, previous and blend
clips by name with their time indices, the blend and transition, `clip_yaw`, the layer weights,
`playing_clip`), `body` (breath, heart, blink, chest, footstep state, edge flags, shake) and `cues`
(`CueSystem::CaptureHistory`). Stage and Puppet hash themselves, beside their fields, so a new
field has an obvious place to go; `make rules` still builds them with no engine (712 + 77 checks).

**Measured, release build, all seven recordings:**

| part | fresh vs fresh | fresh vs warm (second pass, same app) |
|---|---|---|
| `her`, `world` | identical | identical, except the two longest: `world` from 209 / 327, `her` from 375 |
| `bodies` | identical | part at tick 1-2 - the physics world (2.1) |
| `puppet` | identical | differs from tick 0, every recording |
| `anim` | **differs from tick 0 in 5 of 7**, for part of the run | differs from tick 0 |
| `objects` | follows `anim` (the bone poses) | differs |
| `body`, `cues` | identical | follow the rest late in the long recordings |

Read together:

1. **The animation is not restored (2.2), and it shows even between two fresh apps.** Between
   replays the app keeps running on the wall clock, so the idle clip stands at a different time
   whenever the next replay starts. The difference lasts until a clip restarts from its beginning.
   It has not reached the rules in these recordings - `her` and `world` stay identical - but the
   footsteps and the bow read the playhead, so it will. Fix: reset the model's clips, blend and
   transition, `clip_yaw` and `playing_clip` in `RestoreRecordingState`.
2. **The Puppet is not restored either** - identical fresh, different warm: it remembers its last
   crossfade, settle and yaw from before the restart. Fix: a `Puppet::Reset` called by the restore.
3. **The physics world (2.1)** is what carries into the rules in the long recordings - props
   drift, and arrows and kicks meet them differently. With the rp3d agent.

## 9. Step 5 (animation and pose), built 2026-09-28

**More show marked visual-only** (the user: the grass and any visual prop can go, which also saves
the hashing): the foliage group (2,498 objects - grass and plants), vines (506), boulders (62),
the terrain meshes (`terrain_bay_*`, `terrain_back_*` - the blocks are what she stands on), the
scenery tiles (their colliders are Stage blocks), the aim-arc dots, hit popups, arrow views (the
view of `Stage::arrows`, which the rules hash), rope skin and markers, the wind debug view and the
fill light. Of 5,415 objects about 150 non-physics ones are left in the hash - the blockout's
looks and her skeleton. Core warns once if a visual-only object has a rigid body.

**The restore now forgets the animation** (`ApplicationArcher::ResetAnimationForReplay`, the last
thing `RestoreRecordingState` does): `Puppet::Reset(facing)` (every field `HashState` covers, back
to its initialiser, the yaw set to where she faces; the measured tables kept); every clip rewound
and Idle switched to from its first frame, no blend, no transition, rate 1, `clip_yaw` 0; and
`ArcherModel::ResetPoseMemory` - the layer inputs the app sets a tick ahead (upper layer, overlay,
aim, legs, chest scale), the saved base pose, the leg and hair chains. `NewGame` also clears the
footstep tracker's last playhead with its validity flag.

Found step by step with the trace: after the Puppet and clips, one tick of the upper body and the
chest still differed at tick 0 - `detail` now gives each object its own part, which named the
bones, and the cause was the layer inputs the model reads one tick late.

**Measured on the 22:37 export (animations fixed), all seven recordings:**

| comparison | result |
|---|---|
| fresh vs fresh, release | **identical, every part, every tick** |
| debug vs release, fresh | **identical, every part, every tick** |
| fresh vs warm, release | `bodies` part at tick 1-2 (2.1); the rules follow only in the two longest recordings (`world` from 209 / 327); every other part identical |

So the compiler options question has its answer for this code: `-Og` and `-O3` give the same bits.
What remains is the physics world, with the rp3d agent.

One unexplained event: the first release pass of that run lost its MCP connection during the third
recording (`ConnectionResetError`), and its log was overwritten by the next start before it could
be read. A rerun completed and matched. If it recurs, keep the log - it may be a release-only crash.

## 10. The physics world, and the result (2026-09-28)

With the rp3d fork's `rebuildInternalState()` as the last line of the restore and its
`computeStateHash()` in the trace (section 7), measured on all seven recordings:

| comparison | result |
|---|---|
| fresh app vs fresh app (release) | identical, every part including `physics`, every tick |
| fresh pass vs second pass, same app | identical |
| fresh pass vs third pass, same app, reversed order | identical |
| debug vs release (fresh) | identical, `physics` included |

**Baselines re-written** from a fresh debug app with `tools/cue_replay.py --write` (`.cues` and,
for the first time, `.trace`), then checked twice in that same app and once with the release exe:
all seven the same in state and sounds every time. `archer_20260925_140425`'s sounds changed from
the old baseline - the bridge rules - as expected.

What made it: the focus-loss release sparing a replay's keys (2.3), the show marked visual-only
(sections 8-9), the restore forgetting the animation, the Puppet and the model's pose memory
(section 9), and the physics world rebuilding its internal state (2.1). From here, a check that
differs between two runs of one exe is a new leak, and `--detail` names it.

## 11. One test recording (2026-09-28)

Every change to the game changes every recording's baselines, and re-recording seven by hand is
too slow to do after each feature. So, the user's call: ONE recording is kept as the test -
`apps/archer/recordings/archer_test.rec`. The other six moved to `recordings/archive/` without
their baselines. Every agent that changes the game runs `python tools/cue_replay.py` after, reads
the diff, and rewrites with `--write archer_test` when the change is intended - the procedure is in
CLAUDE.md under *Archer's test recording*.

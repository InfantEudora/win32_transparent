# Archer Cue Plan

What the game does, and everything that answers it: sounds, voice lines and narration first, then
camera shake, rumble, particles, music and authored moments, all hung off one layer. Written
2026-09-25, after the kick had been given four sounds by hand in four different ways. Revised
2026-09-27 after a pass over the game and the engine: the plan had been written as a SOUND
system, and it is really an event layer that sound happens to need most. **Agreed; steps 0 to 5
done (2026-09-27) - the archer's sounds, footsteps, camera shake and rumble run on the cue table,
and the Cues panel tunes it. Step 6 (the missing events) next.**

The short argument: every reaction so far answers the same three questions - *when does it fire,
what does it do, and what stops it* - and each one answered them in its own code. A **cue** is one
row that answers all three. The rules keep saying what happened; the cue table decides what that
looks, sounds and feels like; one small player does the firing, the waiting and the cancelling for
all of them.

**Sound goes first** because it has had the least attention and needs this the most - it is the one
reaction with timing, variation, cancelling and voices all at once. Everything else a cue can do
is a smaller case of the same row.

---

## 1. What exists today

Eight sounds, wired five ways, in `ApplicationArcher::UpdateSound`, `StartArrowSwooshes` and
their neighbours:

| wiring | sounds | where |
|---|---|---|
| a rules EVENT, with a value | `arrow_leave` (volume by draw power), `arrow_hit` (by speed and distance from her), `kick_land` (on `f_kick_connected`) | `UpdateSound`, `PlayArrowHit` |
| an APP event, found after the rules | `arrow_hit` on a prop, `nice_shot` (a 10 on a stand) | `ResolveArrowsAgainstProps`, `RegisterTargetHit` |
| a STATE EDGE, with a stop | `bow_tension` starts on the nock, is cut when the string goes | `UpdateSound`, off `Stage::IsNocked` |
| a TICK INTO A MOVE | `kick_swing` on tick 11 of the kick, shifted by each kick's strike | `UpdateSound`, off `stage.kick_ticks` |
| RANDOM chance and timing | one of `kick_hyaa` / `kick_hija` / `kick_hoowa`, on 40% of kicks, starting on tick 10..16, each moved so its loud part lands where `kick_hyaa`'s does | `UpdateSound`, `Hash01` of the kick's tick |
| a FORECAST | `arrow_swoosh`, started before a PREDICTED impact and played from partway in, so its peak lands on the strike | `StartArrowSwooshes`, off `Stage::PredictArrowImpact` |

Plus three panel sliders for the kick alone, and verification by the `sounds_playing` count over MCP,
which says that *something* started and never what.

What that already taught:

- **Timing is authored by ear**, so every timing has to be a number on the panel, not a constant.
- **A move can be cut short** (a kick loses the ground, a draw is released before the nock), and a
  sound planned for later in it must not play.
- **Some sounds must stop** (the creak) and some must be left to ring (`kick_land`).
- **Randomness has to come from the tick**, never from `rand()` or the shared `RRandom` stream, or a
  recorded session sounds different on playback - see `SpawnDebris` for the pattern.
- **A sound's moment is its LOUD PART, not its first sample.** The shouts and the swoosh are both
  placed by where they peak (`SoundSystem::LoudestAt`, measured at load), so a re-cut file
  re-times itself. The plan's first draft had no field for this and every such cue would have
  needed code.
- **Some sounds start before their moment.** The swoosh is heard before the arrow arrives, which
  no trigger in the first draft could express.
- **Events come from two places.** Stage reports what the rules decided; the app finds what only
  rp3d knows (a prop struck, a ring scored) later in the same tick.

---

## 2. Events, cues and actions

Three layers, and the line between the last two is the rule that keeps this honest:

- an **event** is what happened, with a payload: a value (speed, power, points), a point in the
  level, a material or block, and what it came from;
- a **cue** is a row saying how the game answers that event: when, how often, what stops it;
- an **action** is one thing the answer does - play a sound, shake the camera, rumble the pad.

**A cue only PRESENTS. Anything that changes the game is an effect, and effects live in `Stage`.**
The cue player runs after the tick, on the app side, and nothing it does may change what the next
tick computes. So opening a gate, locking the controls, setting a flag, spawning a crate or a
hitstop are not cue actions: they are rules, tested by `make rules` and replayed by a recording,
and they REPORT events that cues then present. That is also why a cutscene is a Stage mode (section
8), not a cue scope.

---

## 3. The cue

A row in a table:

| field | meaning | kick_hyaa, as a cue |
|---|---|---|
| **trigger** | what fires it - section 5 | the kick move |
| **scope** | what it belongs to, for cancelling - section 6 | the kick |
| **chance** | 0..1, drawn once per trigger | 0.4 |
| **delay + jitter** | ticks after the trigger (or after a named point in its move), plus a random 0..jitter | `strike` - 1, + 0..6 |
| **align** | `start` or `peak`: whether the delay places the sound's first sample or its loudest moment | peak |
| **actions** | what it does - section 4. Each action has its own small offset from the cue's moment, so a sound, a shake and a rumble can be one row and stay locked together | a sound: one of `kick_hyaa`, `kick_hija`, `kick_hoowa` |
| **gain** | a base, times a curve over the trigger's value, times distance from her | 0.8 |
| **group** | the voice group, if any - section 7 | her voice |
| **gap** | fewest ticks between two firings of this cue | - |
| **max instances** | most of this cue playing at once; a new one past it is skipped | - |
| **on scope end** | `drop`, `stop` or `keep` - section 6 | drop |

Every field but the trigger has a default, so most rows are a trigger and a sound. A sound action
picks one of its files, never the same one twice running.

`align: peak` is what the shouts' `peak_shift` and the swoosh's lead are today, done once for every
cue: the player knows each file's `LoudestAt`, so a delay always means "when it is loudest".

---

## 4. What a cue can do: actions

| action | what it does | notes |
|---|---|---|
| **sound** | plays one of its files, on a bus, panned by where it happened | `SoundSystem`; section 10 |
| **held sound** | a loop that follows a named parameter (gain and pitch) until its scope ends | the rope creak by swing speed, a crate scrape by push speed |
| **shake** | adds trauma to the camera, with a direction | below |
| **rumble** | the pad's motors, the same envelope as a shake | `InputController` already has rumble |
| **particles** | a burst at the event's point | `SpawnDebris`, dust |
| **popup** | a number or word rising from the point | `SpawnHitPopup` |
| **music** | a stinger, a key change up or down, a suspense target | apps/music; see the note below |
| **subtitle** | a line of text for as long as its sound plays | `UIOverlay`, `SoundSystem::LengthOf` |
| **hook** | a named callback the app registered, for anything core should not know about | presentation only - see section 2 |

**Named parameters.** Held sounds and the music read continuous values - swing speed, push speed,
wind, suspense - that are not events at all. The app publishes a small set of named parameters
once per tick, and an action reads one by name. That keeps "follows the swing" out of code.

**Music runs on its own clock.** apps/music changes key at the next bar, on the audio thread. A cue
REQUESTS a key change or a stinger on its tick, and that request is what is deterministic and what
the log records ("requested"); when it is heard depends on the bar. Suspense is a parameter, not a
cue - it is a level, not a moment.

### Camera shake

- **An offset added in `PlaceCamera`, never a change to `camera_target`.** The sun follows
  `camera_target` and is snapped to its own texel grid; shaking the target would crawl the
  shadows. The follow ease and the keep-in clamp read the target too, and would soak up or fight
  a shake put there.
- **Trauma**, not a random offset per tick: shake actions ADD to a trauma value that decays each
  tick, and the offset is trauma squared times smooth noise sampled at `stage.ticks`. Smooth,
  because per-tick random offsets read as jitter; sampled at the tick, so it replays, freezes under
  `sim_pause` and moves one tick per `sim_step` like everything else.
- **Directional**: a landing shakes mostly vertically, a kick along its direction.
- **Sized in screen units**, a fraction of the view's half-width like the camera's lead, so zooming
  in does not make the same landing feel stronger.
- **One global slider**, where zero is off. None in the orbit camera, which is a debug view.

---

## 5. When a cue fires

1. **Event.** A `StageEvents` flag or list entry, with its value: `f_shot` (power), `f_landed`
   (speed), each `arrow_hits` entry (speed, point, block), `f_kick_connected`, a score. Or an APP
   event with the same shape - a prop struck, a ring scored - signalled when the app finds it.
2. **State edge.** A condition starting or ending: nocked, pushing a crate, swinging on the rope,
   kneeling. The END is as important as the start - it is what stops a held sound.
3. **Move tick.** Tick N of a timed move. The kick, the climb, the get-up and the kneel are all
   counted in ticks already, so "tick 11 of the kick" is exact and replays exactly. **A move also
   publishes its named points** - the kick's `strike` - and a delay can be counted from one. That is
   what `strike_shift` is today, done in the table: every kick's swing is "strike - 2", however
   early or late its boot lands.
4. **Animation marker.** A moment in a CLIP rather than in the rules - a foot planting, a hand
   gripping the rope. The glTF has no events, so these are MEASURED at load, the arrangement every
   other clip fact here uses (the kick's strike, the landings' contact frame, the stop's plant):
   pose the model at each keyframe and find where each toe reaches the floor. Fired when the
   playhead crosses one. During a blend only the LEADING clip's markers fire, or two blended walk
   cycles step twice.
5. **Zone.** The archer entering, leaving or standing in an area of the level - section 8.
6. **Forecast.** Something the game can PREDICT: an arrow's impact (`PredictArrowImpact`), a
   landing (`PredictLanding`). The app signals "this happens in N ticks", and the cue fires early
   enough that its aligned moment lands on it - playing from partway in when the moment is nearer
   than the sound's lead, as the swoosh does. The forecast belongs to a scope (the arrow's flight),
   so a flight that ends somewhere else stops what it started.

---

## 6. When a cue cancels: scopes

Every cue belongs to a **scope**, which is something with a start and an end: a kick, a draw, a
rope swing, an arrow's flight, a zone visit, the level. When a scope ends - normally or cut short -
each of its cues does what its `on scope end` says:

| rule | what happens | example |
|---|---|---|
| **drop** (default) | a cue still WAITING (its delay not yet up) never plays; one already playing rings on | the kick loses the ground on tick 8: the shout picked for tick 14 is dropped, `kick_land` plays out |
| **stop** | waiting cues are dropped and a sound PLAYING is cut | the arrow leaves the string; the creak stops |
| **keep** | both go on, as though the scope had not ended | a delayed echo that should happen however the move ended |

(The first draft had `ring` as a third rule; written out, it was `drop` under another name, and
`keep` is the case that was actually missing.)

The level is the outermost scope, so a restart ends every scope at once - that is the whole of "a
restart silences everything". And since waiting is counted in TICKS, a paused game holds its
waiting cues exactly where they are and `sim_step` releases them one tick at a time.

**There is a Stage per level, and a cue system per level beside it.** The app keeps one of each
per scene, and leaving the range and coming back finds it exactly as it was left. So a scene switch
is not a restart: the level being left is FROZEN, sound included. Its cues stop ticking, with their
open scopes, waiting cues and history intact, and each level's sounds play on a bus of its own
(`world`, `range`, `rope`, `character`, all under the master), which is held while the level is
parked. Coming back releases the bus and the level carries on from the same tick, with the
waterfall mid-loop and a bow's creak mid-draw. A restart is what clears the rest.

The cue system is per level because it runs on the level's clock. When there was one for the
whole app, a switch put it on another Stage's `ticks`: a line started at the world's tick 50,000
counted as still playing on the range until the range reached 50,000, and anything waiting fired a
level's age early or late (fixed 2026-09-30). The table's buses are made per level as
`<level>/<name>`, and the table's "master" is the level's bus.

Under the title nothing ticks, so nothing fires. The title has a bus of its own too (`title`), held
everywhere but on the title, which is where the title music will go.

---

## 7. Voice groups

A voice cannot say two things at once, and a voice that speaks on every event stops being heard.
So every spoken line belongs to a **group**:

- **one line at a time** per group;
- a **priority** per line, and a rule for a line arriving while another is speaking: `skip` (the
  default - most lines are only worth saying at the moment they apply), `queue` (with a longest
  wait, after which it is dropped anyway), or `interrupt` (a higher priority cuts the lower);
- a **gap** per group, so that she does not comment on three things in a row.

Two groups to start with: **her** (`kick_hyaa`, `nice_shot`, the sighs, `phfieuw`, the effort grunts)
and **the narrator** (section 8). The narrator outranks her: while it speaks, her lines are skipped.

Spoken lines play with `SOUND_KEEP`, so a burst of debris knocks cannot take their voice halfway
through a sentence, and noisy cues (debris, footsteps) carry a `max instances` so they cannot
crowd the voices out in the first place. The pool is 32 voices (section 10).

---

## 8. Trigger zones, flags and narration

The extension. Areas of the level that fire a cue when she enters them - a short narration, a hint,
later a cutscene. It is the fifth trigger kind and it needs little new from the rest of the plan: a
zone visit is a **scope**, so a narration can be dropped when she leaves before it starts, and the
narrator is a **voice group**.

**A zone is a rectangle in the rules**, the same shape as a `StageBlock`: centre, half extents, a
name, an id. It lives in `Stage` beside the blocks, knows no engine types, and is tested with the
rules (`make rules`) like everything else there. Built in `BuildMainLevel` and friends the way the
blocks are, drawn as outlines in the blockout view (F2), and movable in the Inspector through the
same editor-layout path as the blocks (`KeepBlockLayout`), so placing one is dragging a box.

**The rules report three things**, off the archer's body box against the zone:
`entered`, `left`, and `stayed` (in it continuously for N ticks - for "she has been standing at the
edge of the gap for two seconds, give her the hint").

**Per zone, how often:**

- `once per session` - a story line; heard once, however many restarts;
- `once per run` - reset by a restart; a tutorial prompt on a fresh attempt;
- `every entry`, with a gap - an ambience change, a creak underfoot.

**Flags and counters.** A few named values in `Stage`: counters the rules keep (ledges caught,
kicks connected, bullseyes - cheap, and also what an achievement or a score screen would read) and
flags that zones SET when entered or when their line has played. **Conditions** read them: `on the
ground`, `in this mode` (not while on the rope), `after X` (a line that follows on from another),
`unless she has already done Y` (the ledge hint is pointless once she has caught a ledge). A
condition is a short all-of list of `name op value` - deliberately not a scripting language.
Because zones can set flags as well as read them, a sequence of authored moments is a chain of
zones and flags with no code of its own.

**Narration itself:**

- the narrator group, above her voice;
- **ducking**: the effects, ambience and MUSIC buses turned down while the narrator speaks, and back
  up after - a bus gain eased once per tick, which `SoundSystem` now smooths;
- **subtitles**, drawn with `UIOverlay` for as long as the line plays (`SoundSystem::LengthOf`) - a
  line needs its text beside its file in the table;
- **leaving the zone** ends its scope: a narration still waiting (a zone line wants a short delay, or
  it fires the instant a foot crosses the edge) is dropped; one already speaking rings on.

**Cutscenes** come later, but the start of one fits here - as a **Stage mode**, because locking the
controls changes the game (section 2). The level-entry get-up already is exactly that: `MODE_GETUP`,
controls locked for `GETUP_TICKS`, `f_got_up` when they come back. A cutscene is that generalised: a
zone starts the mode, the mode counts its ticks and reports them, cues present them, and it ends.
Camera moves and scripted animation are a separate plan.

---

## 9. Where things run, and determinism

Everything is decided **on the physics thread, inside the tick**:

- **signals are collected through the whole tick and the cues fire once, at its END**, just before
  `PublishSnapshot`. Not straight after `stage.Tick`, where `UpdateSound` runs today: prop hits and
  `nice_shot` are found later, by `ResolveArrowsAgainstProps`, and a player run earlier would fire
  them a tick late or in an order that depended on which code found them;
- **the clock is `stage.ticks`**, the level's, which a replay restores - not the scene's
  `GetPhysicsTick()`, which the hit popups use and a replay does not;
- a cue's chance, jitter and pick come from a hash of that tick, the cue's id and an **instance
  index** - two arrows striking on one tick must not draw the same variation - and nothing touches
  the shared `RRandom` stream;
- the waiting queue is keyed by tick, so pause and `sim_step` are exact;
- zones are tested by the rules, so a recording replays through the same zone at the same tick.

**The catch is history.** "Never the same file twice running", the gaps, the voice groups' last
line and `once per session` are all state that outlives the tick, and some of it outlives a
restart. A recording's start state (`CaptureRecordingState`) carries a small cue-history blob -
last pick per cue, gap and group timers, the lines heard - and `RestoreRecordingState` puts it
back, so a replay mid-session behaves as the original did rather than as a fresh session would.

---

## 10. Engine and app

**Engine: `core/SoundSystem` (step 0, done 2026-09-27).** What the cue layer needed from it and
did not have:

- **live control of a playing sound**: `SetGain`, `SetPitch`, `SetPan` on a handle. Gain is
  smoothed over `SOUND_GAIN_SMOOTH_SECONDS` (10 ms, under a 60 Hz tick), so a gain set every tick is
  a smooth ramp rather than a buzz of steps - and a new voice's FIRST gain is taken as-is, so the
  smoothing never softens an attack;
- **pan** as a stereo balance, -1..1, for placing a sound by its screen x. There is still no
  listener or spatializer, and does not need to be for a side view;
- **buses**: `AddBus(name, parent)`, `SetBusGain`, a master that exists from `Initialise` on and
  that every sound and stream feeds by default. What ducking and a settings screen's sliders stand
  on. Bus gains are smoothed like voice gains;
- `Play(name, SoundParams)` with gain, pitch, pan, bus, looping, keep and start offset in one
  struct; the old argument-list `Play` and `PlayStream(source, gain)` still compile unchanged;
- **limits**: 256 buffers (was 32 - section 11 alone went past it), 32 voices (was 16), 8 buses,
  and each load logs its decoded size and the running total, which is the real cost;
- `LengthOf(name)`, for subtitles and gaps measured from a sound's end;
- `ListVoices`: every voice making noise, with its name, bus, gain, pitch, pan and position - what
  `sounds_playing` never said, and what the cue log will print;
- `InitialiseOffline` + `Render`: the same engine with no device, mixed on demand. That is what the
  checks below measure against, and later what a replay's audio can be rendered to a file with.

Verified by `tools/sound_test.cpp`'s new offline section - 19 checks, all passing, run with
`sound_test.exe shared_assets --offline-only` and making no sound: mono centred equal on both
sides; pan -1 and +1 each empty the other channel (RMS 0.00000), at the start and while playing;
`SetGain` to 0 still sounds in its first millisecond and is silent 20 ms later; a new voice at 0.25
peaks at 0.125 in its first millisecond against a 0.5 sine (no ramp down from 1); a bus at 0 is
silent, at 0.5 halves, and a master at 0.5 over it quarters; a missing bus plays on the master;
pitch 2 plays a 1 s sine in 0.500 s; `ListVoices` reports name, bus, pan, loop and position. The
device checks in the same file were not re-run (they make noise at the desk). Archer builds and
starts against it (48 kHz device, all ten sounds load); no other app was built, per the usual rule.

**Engine: `core/CueSystem`.** Generic, knows nothing about archery:

- the cue table type, the waiting queue, the history blob;
- `Signal(id, payload)` for events and forecasts, `BeginScope` / `EndScope` for scopes (edges and
  moves are scopes, a move tick is a delay after its scope began, a named point is a delay after
  that), `SetParameter(name, value)` for the held sounds and the music, `Tick(tick)` to fire what is
  due;
- variation picking with no repeat, gaps, chance and jitter, `align: peak`, max instances, voice
  groups with ducking;
- action kinds as a small interface: sound it does itself through `SoundSystem`; shake, rumble,
  particles, popups, music and hooks are handed to whoever registered them, so core does not grow
  a camera or a particle dependency.

**App: the cue table and the wiring.** The few lines that turn `StageEvents` and the app's own
finds into signals, scopes and parameters; the shake, applied in `PlaceCamera`; the action handlers.
Zones, flags, counters and cutscene modes are rules, so they live in `Stage`.

**The table is a JSON file**, `assets/cues/archer.json`, hot-reloaded through core's file watcher -
the arrangement the music bench's score already uses. Timings are tuned by ear, there will be
dozens of rows, and with `align: peak` almost none of them need code. The `ARCHER_CLIPS` style of
table in code stays right for clip facts, which the code measures; cue timings are authored.

**Tuning and checking.**

- **One cue panel** replacing the three kick sliders: every cue's delay, jitter, chance and gain on
  sliders, a button to fire it (submitted as a command, never run on the UI thread), and **Save**,
  which writes the row back to the file.
- **A cue log over MCP** - the last N cues fired, each with its tick, its scope, its pick and gain,
  and why it was skipped if it was. Plain text with rounded numbers, so two replays' logs can be
  diffed with no noise. That replaces reading `sounds_playing` and is what makes a replay a real
  test: replay a recording, read the log, compare it with the last one.

---

## 11. Sounds to author, for events the game already has

| event | sounds |
|---|---|
| footsteps (needs markers) | 4 variations each on grass, wood, stone; a scuff for the stop and the turn-around |
| jump | a small effort breath and a cloth whoosh |
| landing | `jump_landing`, and a heavier variant for a hard landing (the Puppet already bands soft and hard); a shake scaled by `land_speed` |
| head bump | a thud and an "ow" |
| ledge | hand slap on the catch, effort and scrape on the climb, a small release |
| kneel / stand | cloth rustle, a knee on the ground; a "hmm" when there is no room to stand |
| level-entry get-up | a sigh or `phfieuw` |
| rope | grab, a creak that follows the swing speed (a held sound, like the bow's), release, a whoosh on a rope jump, a snap when the rope goes (`f_rope_lost`) |
| spring plants | a boing scaled by `stomp`, a creak on a pumped swing (`f_swung`, `swing_speed`) |
| branches | a creak while balancing, a crack and a yelp on `f_lost_balance`, a grab on `f_caught_branch` |
| ramps | a slide scrape that follows her speed down the ramp |
| bow | quiver rustle and a nock click as the arrow goes on; a string relaxing when a draw is cancelled |
| arrow | a flight whoosh stopped when it sticks; hits by material - target, crate, stone, ground, the cracked wall |
| scoring | `nice_shot` for a 10; lines for a near miss and a poor score |
| crates | a scrape while she pushes one, a thump when one falls or topples |
| walls | a crumble for the brick and the cracked wall (`broken_blocks`), with a shake; small debris knocks, with a gap and a max instances |
| ambience | `archery_range` looping on the range; wind and birds on the main level |
| narration | per zone, with its subtitle text |

**Events the rules do not report yet**, needed by the table above: the nock (today an edge the app
derives), a cancelled draw, a push starting and stopping, a slide starting and stopping, a prop
landing or toppling (from rp3d's contact callback - read the velocity on `ContactStart` only, it is
reported before the solver runs), the animation markers, and the zones.

---

## 12. Steps

0. **`SoundSystem` for the cue layer** - live gain/pitch/pan, buses, the new limits, `LengthOf`,
   `ListVoices`, offline render and its checks. **Done 2026-09-27**, section 10.
1. **The baseline log.** Before anything moves, the cue log's line format printed from the
   CURRENT `UpdateSound`, `StartArrowSwooshes` and `RegisterTargetHit` at each `Play`, and the kick
   and bullseye replays recorded through it. Without this step 3 has nothing to compare against.
   **Done 2026-09-27** - see "The baseline" below.
2. **`core/CueSystem`** - the JSON table and its reload, signals, scopes, parameters, the waiting
   queue at the end of the tick, variations, `align`, gaps, max instances, groups, the history
   blob in the recording state; the MCP cue log. **Done 2026-09-27** - see "The cue system" below.
3. **Move the eight existing sounds onto cues** with no change in what is heard, and prove it by
   diffing the step 1 logs against the new ones (swing on the strike; land on connect; shout 40%
   on 10..16 by its peak; the swoosh on its forecast; `nice_shot` on the 10). Buses for effects,
   voice and ambience set up here, and arrow sounds panned by their x. **Done 2026-09-27** - see
   "The move" below.
4. **Shake and rumble**, as the first non-sound actions - cheap, and the proof that one row can
   drive several outputs before anything larger is built on it. Landings, kick connects, broken
   walls. **Done 2026-09-27** - see "Shake and rumble" below.
5. **The cue panel**, replacing the kick sliders. **Done 2026-09-27** - see "The cue panel" below.
6. **The missing events** in section 11, each with a rules test.
7. **Animation markers**, then footsteps on them. **Footsteps done early, 2026-09-27**, on the
   one marker that already existed - see "Footsteps" below; general markers are still to do.
8. **Zones, flags and counters** in `Stage`: the rectangle, enter/leave/stay, how-often, conditions,
   drawing and editing; then the narrator group, ducking and subtitles.
9. **The cutscene mode** - the level-entry get-up rebuilt as the first one.
10. **Music as an action**: stingers and key requests from cues, suspense and brightness from
    parameters and zones.

### The baseline (step 1)

- **`core/CueLog.h`** - the log, header-only, with its own lock. The line format is the
  contract: level tick, cue, `play`/`stop`, sound, the cue's gain (before the master volume), and
  pitch, pan and start offset only when they are not the default. Step 2's `CueSystem` writes
  through this same class, so the format cannot drift between old and new code.
- **Every sound in the archer goes through `PlayCue` / `StopCue`**, which log first and then
  play. `ArrowSoundGain` no longer contains `sound_volume`; `PlayCue` applies it, so muting leaves
  the log alone. Cue names are the ones step 3's rows will have: `bow_tension`, `arrow_leave`,
  `arrow_swoosh`, `arrow_hit`, `kick_swing`, `kick_land`, `kick_shout`, `nice_shot`.
- **MCP**: `cue_log` (`last`, `clear` after reading) and `archer_sound` (`volume`, and every voice
  playing, through `SoundSystem::ListVoices` in the snapshot).
- **`tools/cue_replay.py`** replays recordings in a running archer (`--minimized --mcp-port
  8768`), muted, and diffs each against `recordings/<name>.cues`; `--write` makes the baselines.
  Exit 1 on any difference, with a unified diff.

The five baselines, and what they cover:

| recording | lines | covers |
|---|---|---|
| `archer_20260925_143356` | 2 | a front kick that connects: `kick_swing`, `kick_land` |
| `archer_20260925_145919` | 6 | one draw to a bullseye: `bow_tension` play and stop, `arrow_leave`, `arrow_swoosh` from 0.110, `arrow_hit`, `nice_shot` |
| `archer_20260925_140425` | 29 | five shots, two of them tens (one swoosh from 0.027 at gain 0.980), and a kick that connects |
| `archer_20260926_143350` | 0 | a minute of jumping on the spring plants: nothing fires |
| `archer_20260927_133347` | 7 | recorded for this: four kicks in the air of all three kinds, three of them shouting (`kick_hija` twice, `kick_hoowa`) |

Every run of each was identical, pass against pass and replay against the live session - the
level tick restored by the replay is what makes the hashed shout draws repeat. The check was shown
to fail: a baseline edited by one tick came back `DIFFERENT` with the line named. Not covered by
any recording: `kick_hyaa` (the draw never picked it) and a creak cut by anything but a loose.

`archer_20260925_140425.rec`, `archer_20260927_133347.rec` and (since the footsteps and the
shake) `archer_20260927_162520.rec` and `archer_20260927_165729.rec` are not in git (`*.rec` is ignored; the other three were
force-added), so their baselines need them added with `git add -f`.

### The cue system (step 2)

- **`core/CueSystem.h/.cpp`**. The table format is written out in full at the top of the header:
  one of `signal` / `begin` / `end` per cue, then scope, when, once, chance, delay, jitter,
  delay_from, forecast, align, sounds, no_repeat, seed, gain and gain_by curves, pan_by, pitch,
  bus, looping, follow, group / priority / busy / max_wait, gap, max_instances, on_end, actions.
  Plus top-level `sounds` (registered on load), `buses` and `groups` (with ducking). An unknown
  field is an ERROR naming it, and a failed load keeps the table in use.
- **No sound dependency in core.** Core is compiled once for every app and `SoundSystem` only
  into sound apps, so cues play through a `CueOutput` interface. `core/CueSoundOutput.h` is the
  header-only adapter to `SoundSystem`, compiled in the app. With no output the cues still decide
  and log.
- **Deterministic without the audio thread.** Every "is it still playing" question - max
  instances, a group speaking, a duck - is answered from each sound's length counted in ticks,
  never by asking the mixer, so a replay decides the same with or without a device.
- **Draws are `CueHash01`, `PlaceHash.h`'s `Hash01` mixing** with the cue's key and the
  trigger's instance where x and y were: a cue with `"seed": 0` draws exactly what the archer's
  hand-wired shout drew. Other cues key off their name, so two cues on one tick do not draw alike.
- **Settled while building it:** `ring` became `keep` (section 6). `duck_ticks` is how long the
  whole duck takes, not a rate. A cue whose scope STOPS it logs its `stop` when the scope ends even
  if the sound has already run out, which is what the old creak code did and what the baseline
  holds. Cues on the same event and tick fire in table order, which is alphabetical
  (json objects come back sorted). A clock that goes backwards (a restart) clears a cue's gap
  rather than blocking it. The load methods are `LoadTable` / `LoadTableText`, because
  `LoadString` is a Windows macro.
- **`tools/cue_test.cpp`**: 32 checks, all passing, silent (build line at its top). Most run on a fake output and read
  the log: table errors, curves and the distance floor, conditions, delays and delay_from,
  scoped signals, drop and stop at a scope's end, no_repeat over 400 draws, gaps, max
  instances, skip / queue / interrupt in a group with its gap, ducking eased over its ticks,
  actions with offsets, history capture and restore, restart and reload.
  **The parity checks** run the archer's old code beside the cue rows meant to replace it.
  4,000 kicks at four strike shifts gave 1,587 shouts (534 / 513 / 540 across the three files),
  and 160 swoosh flights of 1..40 ticks: same tick, same file and same offset, 0 differences. The
  rows that did it are the ones step 3 will write:

  ```json
  "kick_swing":   { "begin": "kick", "sounds": "kick_swing", "delay": 10, "delay_from": "strike_shift", "gain": 0.8 },
  "kick_shout":   { "begin": "kick", "sounds": ["kick_hyaa", "kick_hija", "kick_hoowa"], "chance": 0.4,
                    "delay": 9, "jitter": 6, "delay_from": "strike_shift", "align": "peak",
                    "no_repeat": false, "seed": 0, "gain": 0.8 },
  "arrow_swoosh": { "signal": "arrow_impact", "forecast": "in", "scope": "arrow", "once": true, "sounds": "arrow_swoosh" }
  ```

  `no_repeat: false` and `seed: 0` are there for the proof only. Once step 3 has matched the
  baselines, flipping them is a deliberate, separately re-baselined change. One check drives the
  real `SoundSystem` offline through `CueSoundOutput`: a narrator line on its group ducks the
  effects bus, and the hum on it measures 0.354 before and 0.088 under it.
- Archer builds and links with the new core object; it does not call it yet. No other app was
  built.

### The move (step 3)

- **`assets/cues/archer.json`** is what the archer sounds like now: the ten files, three buses
  (effects, voice, ambience - nothing on the last yet) and the eight cues, each with a comment
  saying why its numbers are what they are. It reloads within a second of being saved, paused
  or not (`PollCueTable`, a mtime poll from `UpdateView`); a table that fails to parse is logged
  and the last good one kept. Checked: three saves to the running game, three reloads.
- **`ApplicationArcher` only reports.** `SignalCues` straight after the rules (the `nocked` and
  `kick` scopes off their edges, `shot`, `kick_connected`, level `arrow_hit`s), the props' hits and
  `stand_hit` from where they are found, and `ForecastArrowImpacts` - one `arrow` scope per flight,
  `arrow_impact` with `in` every tick of it. `cues.Tick(stage.ticks)` fires them all at the end of
  `RunSimulationTick`. Gone: `UpdateSound`, `PlayCue`/`StopCue`, `ArrowSoundGain`,
  `StartArrowSwooshes`, the kick-shout state and the three kick sliders (the panel names the
  table instead). `NewGame` resets the cues; the recording state carries `cue_history`.
- **The volume slider is the master bus**, set from `UpdateView` every pass - so it now turns down
  what is already playing, which it did not before.
- **No device, or USE_SOUND=0: the cues still decide and log**, with no output. A declared sound
  whose file will not load is silent with a warning rather than taking the table down.
- **Two engine fixes found on the way.** `CueSystem` now holds back a cue due on the tick its scope
  ends, since the thing the scope stands for is over by then - the old `kick_ticks == N` test never
  played one. And `LoadTable` falls back to `LoadFile` when there is no loose file, so a packed
  build finds its baked table.
- **The proof.** `tools/cue_replay.py` against the five step-1 baselines, on the parity table:
  **every play and stop identical**, on all five. The only new lines were three `skip (chance)`,
  a shout's draw saying no, which the hand-wired code never wrote - so the tool now leaves skip
  lines out of a comparison unless `--with-skips`. Then, muted, a live shot's `arrow_hit` was seen
  playing on the device, on the effects bus, at the gain the log gave it.
- **Then the audible change, separately**: `arrow_swoosh` and `arrow_hit` panned by `dx`, 0.6 at the
  screen's edge (16 units). The check against the old baselines differed in exactly the 12 arrow
  lines, each identical once its `pan` is taken out. The baselines were rewritten from that (they
  are the cue system's own now, skip lines included) and two further runs matched them with
  `--with-skips`.
- **The shout's old-draw emulation is gone** (2026-09-27, after the move was heard and approved):
  no `seed`, `no_repeat` on, so it is keyed off its own name and never the same shout twice
  running. The last thing the archer asked the sound system directly - where the swoosh peaks,
  for the forecast's horizon - is `CueSystem::PeakOf` now, so nothing in the app touches a
  sound but through the cues.
- Not tried: a ship (`make ship`) build of archer, which is where the baked-table fallback matters.

### Footsteps (the start of step 7)

- **A footstep is a foot planting in the clip on screen**, not a timer and not a distance walked.
  `MeasureClipPhases` already posed the model at every keyframe of the walk and the two runs to
  find where the LEFT toe is lowest (for the blend's phase lock); it now finds the RIGHT one too,
  `Puppet::clip_phase_right`. Measured rather than assumed half a cycle on, and rightly: the right
  foot plants 0.50, 0.56 and 0.53 of a cycle after the left in Walking, Running_Slow and
  Running_Fast.
- **`SignalFootsteps`**, each tick before the animation is synced: the LEADING clip of the blend pair
  (the heavier side - both sides plant together, so firing from both would double every step),
  its playhead as a phase from its left plant - which is what the phase lock holds equal between
  the two clips, so a walk handing over to a run neither doubles nor loses a step - and a
  `footstep` signal (foot, speed, x) for each plant it crossed. Only on the ground, only in a
  locomotion clip, only forward, never more than half a cycle in a tick (anything else is a
  playhead that was set, not played).
- **The `footstep` cue**: one of `footstep_1..4`, never the same twice running, gain 0.6 at a walk
  rising to 1.0 at a sprint, `gap 4` against a double step at a handover, on the effects bus.
- **Checked**: on the flat slab past x 100, a walk steps every 21-27 ticks (the two feet's
  uneven halves), a jog every 15-18, a sprint every 11-13, and nothing while she stands. In the
  older recordings she runs only in short bursts between running jumps - a per-tick trace of
  `140425` showed 18 ticks on the ground between jumps, half a cycle of the fast run, and exactly
  the one plant in it that fired. `recordings/archer_20260927_162520` is new for this: walk, jog,
  sprint on the slab, 16 steps. All six baselines rewritten and replayed twice, identical with
  skip lines included.
- **The four files are uneven**: `footstep_3` is about 2.5 times as loud as `footstep_1` (RMS 0.029
  against 0.011), so one step in four stands out. A per-sound gain in the table, or levelling the
  files, would even it; neither is done.
- Not yet: surfaces (grass, wood, stone), the stop's scuff and the turn-around (Running_ToStop and
  the turn are not locomotion clips, so they are silent), landings (`jump_landing.wav` is there,
  unused), and a small pitch jitter per step, which the table cannot express yet.

### Shake and rumble (step 4)

- **An action gets its cue's gain.** `CueAction::gain` is the cue's `gain` times its `gain_by`
  curves - exactly what a sound of that cue would play at - and the log's `act` lines print it.
  So "shake harder the harder she lands" is a curve in the table, not a rule in code.
- **`shake`** (`amount` = trauma added, `axes` = [across, up]) is `ApplicationArcher::AddShake`. One
  trauma value, capped at 1, decaying over `shake_ticks` (24) every tick; the view moves by trauma
  squared times smooth value noise (`CueHash01` of the level's tick, smoothstepped, `shake_hz` 14),
  weighted by the axes, sized as `shake_max` (0.04) of the view's half-height so it is the same size
  at any zoom. Applied in `PlaceCamera` to the camera and its look-at together - a slide, never a
  turn - and never to `camera_target`, which the sun and the follow read. None in the orbit camera.
  `shake_scale` is the player's setting, 0 off. All four on the camera panel and `archer_camera`;
  `archer_state.shake` reports trauma and the offset.
- **`rumble`** (`low`, `high`, 0..1 per motor) sets `InputController::lmotor/rmotor`, which the pad's
  poll decays by itself - so the strength is also the length. ONLY WHILE THE WINDOW HAS FOCUS: an
  agent's minimised run must not buzz the pad in someone else's hands.
- **New signals**, for these and for the sounds to come: `jumped` (x), `landed` (speed, x - a
  routine jump lands at about 18.7, a 5.5-unit drop at 25, the fastest fall 34), `block_broken`
  (count, x; once a tick), and `kick_connected` now carries `dir` and `x`.
- **The rows**: `land_shake` (from speed 20, so a routine jump does not shake; 0.4..1.0 by 34;
  mostly vertical; heavy motor), `kick_shake` (along the kick, light motor), `wall_shake` (the
  biggest, more with more bricks). A kick that breaks the wall fires both on one tick and the
  trauma adds up to the cap.
- **Checked**: a 3-unit drop does not shake; a 13-unit one lands at gain 0.636, adds 0.445 trauma,
  moves the view mostly down (dy -0.070 against dx -0.009) and dies away at 1/24 a tick. Paused at
  trauma 0.404, the camera stood exactly the reported offset from where shake 0 put it (dx -0.0103,
  dy -0.0263, dz 0), and returned to the same place when shake came back on. Against the baselines
  the only change was added `act` lines - the two kick connects and four spring-launched landings.
  `recordings/archer_20260927_165729` is new: a kick into the cracked wall. Seven baselines,
  replayed twice, identical with skips.
- Not done: a roll in the shake (a little rotation reads well for big hits), and hitstop, which is
  an effect for the rules (section 2), not an action.

### Jump and landing sounds (2026-09-27)

- **`jump_takeoff`** on `jumped`. **`jump_voice`** - `jump_huh_1`, `jump_huh_2`, `jump_ha_1` - works
  the way the kick shout does: 40% of jumps, 0..2 ticks of jitter, `align: peak` with the
  latest-peaking file (`huh_1`, 9.6 ticks in) first so the others only ever move later.
- **`jump_landing` is a forecast**, like the swoosh: the file builds 0.13 s to its thud, so it starts
  on the new `landing_ahead` signal (`Stage::PredictLanding`, every tick of a flight that will
  LAND rather than catch a ledge) and the thud falls on the contact tick. Once per `airborne` scope
  (every stretch in MODE_AIR). Nothing under speed 5; 0.35..1.0 by 25.
- **`land_grunt`** on `landed` from 25, the hard landing's threshold.
- **The `her` voice group** now exists, holding kick_shout, jump_voice, land_grunt and nice_shot:
  one line at a time, a second one skipped. No gap yet.
- **Checked**: ten jumps on the slab - a takeoff on every one, the voice on three, each landing's
  thud started 7 ticks early from 0.013 s in; a drop from 16 - thud at 1519, touchdown, grunt,
  shake and rumble all at 1526. Against the baselines: only additions (24 takeoffs, 30 landings,
  13 voice lines, 1 grunt), nothing removed and no kick shout lost to the group.
- **And a fix to the proof itself**: one line from the PREVIOUS recording - a flight still in the
  air when it ended - landed in the next one's log, between the tool clearing the log and the
  replay restarting the level. `CueSystem::Reset` now logs a `reset` marker and
  `tools/cue_replay.py` keeps only what follows the replay's own restart. Baselines rewritten and
  checked three times in a row, all seven identical. (One earlier check run reported one
  recording different; it was not reproduced in three runs since and may have been the test
  window being closed during it - worth knowing if a flaky difference ever turns up.)
- `breathing_1.wav` stayed off the table: the breaths became their own in and out files, on the
  vitals' breath cues - `vitals_plan.md`.

### The cue panel (2026-09-27)

- **A "Cues" tab beside the Archer panel** (`ApplicationArcher::DrawCuePanel`): every row of
  `archer.json` with its trigger beside its name, and, opened, its delay, jitter, chance and gain on
  sliders, its sounds and its comment. The vitals' graphs and holds are at its top.
- **It edits the file's TEXT.** `PatchCueNumber` replaces the one number where it stands, or adds
  the field at the end of its row, and leaves every other character of the hand-laid file alone.
  Checked offline against the real table: four patches, the diff exactly those four numbers, the
  table still parsing, a curve's own `gain_by` never mistaken for the row's.
- **Live on letting go**: the edited text is handed to the physics thread and loaded as the file
  would be (`PollCueTable`), so the game plays the edit before it is saved. **Save** writes it;
  **Revert** takes the file back; a hand edit of the file while nothing is unsaved shows up in the
  panel within a second, and one under unsaved edits is flagged rather than lost.
- **Play** beside every row: `CueSystem::Audition`, new in core - the row at full strength (every
  gain_by curve at its loudest), past its trigger, condition, chance, delay and group, the next of
  its sounds on each press, its actions too. It touches no history, so it cannot change what a
  replay decides. Also over MCP as `cue_play`. `tools/cue_test.cpp` has 6 checks on it (42 now).
- **Checked**: the play button's path over MCP (jump_voice's three sounds in turn, land_shake's
  shake and rumble, an unknown name refused) and the panel on screen. The sliders' live reload was
  NOT driven by a click here - a minimised window takes none - so the first drag of one is the
  first real test of that path. The seven baselines are unchanged.

(What was step 8, "audio follows the pause", is done already: `SoundSystem::SetPaused` holds every
voice on a pass that does not tick, so a paused game is silent and `sim_step` plays one tick.)

---

## 13. Open questions

- **Zones authored in code or in Blender?** Code first, like the blocks. A named box in the blockout
  export is the obvious later route, and would put narration placement in the same tool as the level.
- **The narrator's voice** - a separate character, or her thinking aloud? It decides whether her own
  lines and the narration can ever overlap.
- **Subtitles on or off by default**, and whether her own lines get them too.
- **Hitstop** - a few frozen ticks on a hard kick or a bullseye - would be a strong addition to the
  feel, and it is an EFFECT (section 2): the rules would hold motion for N ticks and report it.
  Worth trying once shake is in, as its own small rules change.

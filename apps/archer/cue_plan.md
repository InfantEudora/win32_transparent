# Archer Cue Plan

Sounds, voice lines and narration, hung off what the game does. Written 2026-09-25, after the kick
had been given four sounds by hand in four different ways. **Agreed in outline, not started** - this
is to be extended before anything is built.

The short argument: every sound so far answers the same three questions - *when does it fire, what
does it play, and what stops it* - and each one answered them in its own code. A **cue** is one row
that answers all three. The rules keep saying what happened; the cue table decides what that sounds
like; one small player does the firing, the waiting and the cancelling for all of them.

---

## 1. What exists today

Seven sounds, wired four ways, all in `ApplicationArcher::UpdateSound` and its neighbours:

| wiring | sounds | where |
|---|---|---|
| a rules EVENT, with a value | `arrow_leave` (volume by draw power), `arrow_hit` (by speed and distance from her), `kick_land` (on `f_kick_connected`), `nice_shot` (a 10 on a stand) | `UpdateSound`, `PlayArrowHit`, `RegisterTargetHit` |
| a STATE EDGE, with a stop | `bow_tension` starts on the nock, is cut when the string goes | `UpdateSound`, off `Stage::IsNocked` |
| a TICK INTO A MOVE | `kick_swing` on tick 11 of the kick | `UpdateSound`, off `stage.kick_ticks` |
| RANDOM chance and timing | one of `kick_hyaa` / `kick_hija` / `kick_hoowa`, on 40% of kicks, starting on tick 10..16 (each shout moved to put its loud part where `kick_hyaa`'s is, and all kick sounds moved with Down+K / Up+K's later strike) | `UpdateSound`, `Hash01` of the kick's tick |

Plus three panel sliders for the kick alone, and verification by the `sounds_playing` count over MCP,
which says that *something* started and never what.

What that already taught:

- **Timing is authored by ear**, so every timing has to be a number on the panel, not a constant.
- **A move can be cut short** (a kick loses the ground, a draw is released before the nock), and a
  sound planned for later in it must not play.
- **Some sounds must stop** (the creak) and some must be left to ring (`kick_land`).
- **Randomness has to come from the tick**, never from `rand()` or the shared `RRandom` stream, or a
  recorded session sounds different on playback - see `SpawnDebris` for the pattern.

---

## 2. The cue

A row in a table:

| field | meaning | kick_hyaa, as a cue |
|---|---|---|
| **trigger** | what fires it - one of the five kinds in section 3 | tick 1 of the kick move |
| **scope** | what it belongs to, for cancelling - section 4 | the kick |
| **chance** | 0..1, drawn once per trigger | 0.4 |
| **delay + jitter** | ticks after the trigger, plus a random 0..jitter | 9 + 0..6 |
| **sounds** | one or more files; one is picked, never the same twice running | `kick_hyaa` |
| **volume** | a base, times a curve over the trigger's value, times distance from her | 0.8 |
| **group** | the voice group, if any - section 5 | her voice |
| **gap** | fewest ticks between two plays of this cue | - |
| **on scope end** | `drop`, `stop` or `ring` - section 4 | drop |

Every field but the trigger has a default, so most rows are a trigger and a sound.

---

## 3. When a cue fires

Five kinds of trigger. The first three are what already exists; the other two are new.

1. **Event.** A `StageEvents` flag or list entry, with its value: `f_shot` (power), `f_landed`
   (speed), each `arrow_hits` entry (speed, point, block), `f_kick_connected`, a score.
2. **State edge.** A condition starting or ending: nocked, pushing a crate, swinging on the rope,
   kneeling. The END is as important as the start - it is what stops a held sound.
3. **Move tick.** Tick N of a timed move. The kick, the climb, the get-up and the kneel are all
   counted in ticks already, so "tick 11 of the kick" is exact and replays exactly.
4. **Animation marker.** A moment in a CLIP rather than in the rules - a foot planting, a hand
   gripping the rope. The glTF has no events, so these are MEASURED at load, the arrangement every
   other clip fact here uses (the kick's strike, the landings' contact frame, the stop's plant):
   pose the model at each keyframe and find where each toe reaches the floor. Fired when the
   playhead crosses one. During a blend only the LEADING clip's markers fire, or two blended walk
   cycles step twice.
5. **Zone.** The archer entering, leaving or standing in an area of the level - section 6.

---

## 4. When a cue cancels: scopes

Every cue belongs to a **scope**, which is something with a start and an end: a kick, a draw, a
rope swing, a zone visit, the level. When a scope ends - normally or cut short - each of its cues
does what its `on scope end` says:

| rule | what happens | example |
|---|---|---|
| **drop** | a cue still WAITING (its delay not yet up) never plays | the kick loses the ground on tick 8; the shout picked for tick 14 is dropped |
| **stop** | a sound PLAYING is cut | the arrow leaves the string; the creak stops |
| **ring** | anything playing finishes; only waiting cues are dropped | `kick_land` plays out after the kick ends |

The level is the outermost scope, so a restart or a scene switch ends every scope at once - that is
the whole of "a restart silences everything". And since waiting is counted in TICKS, a paused game
holds its waiting cues exactly where they are and `sim_step` releases them one tick at a time.

---

## 5. Voice groups

A voice cannot say two things at once, and a voice that speaks on every event stops being heard.
So every spoken line belongs to a **group**:

- **one line at a time** per group;
- a **priority** per line, and a rule for a line arriving while another is speaking: `skip` (the
  default - most lines are only worth saying at the moment they apply), `queue` (with a longest
  wait, after which it is dropped anyway), or `interrupt` (a higher priority cuts the lower);
- a **gap** per group, so that she does not comment on three things in a row.

Two groups to start with: **her** (`kick_hyaa`, `nice_shot`, the sighs, `phfieuw`, the effort grunts)
and **the narrator** (section 6). The narrator outranks her: while it speaks, her lines are skipped.

---

## 6. Trigger zones and narration

The extension. Areas of the level that fire a cue when she enters them - a short narration, a hint,
later a cutscene. It is the fifth trigger kind and it needs nothing new from the rest of the plan: a
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

**Conditions**, because the right line depends on what she has done: `on the ground`, `in this mode`
(not while on the rope), `after cue X has played` (a line that follows on from another), and
`unless she has already done Y` (the ledge hint is pointless once she has caught a ledge). The last
one wants a few counters in the rules - ledges caught, kicks connected, bullseyes - which are cheap
and are also what an achievement or a score screen would read.

**Narration itself:**

- the narrator group, above her voice;
- **ducking**: effects and ambience turned down while the narrator speaks, and back up after;
- **subtitles**, drawn with `UIOverlay` for as long as the line plays - a line needs its text beside
  its file in the table;
- **leaving the zone** ends its scope: a narration still waiting (a zone line wants a short delay, or
  it fires the instant a foot crosses the edge) is dropped; one already speaking rings on.

**Cutscenes** come later, but the start of one fits here. A cutscene is a scope that **locks the
controls** while it lives, with cues on its ticks. The level-entry get-up already does exactly
that - controls locked for 3.5 s, `f_got_up` when they come back - so a cutscene is that
generalised: a zone starts a scope, the scope locks input, cues play on its ticks, and it ends.
Camera moves and scripted animation are a separate plan.

---

## 7. Where things run, and determinism

Everything is decided **on the physics thread, inside the tick**, straight after `stage.Tick` - where
`UpdateSound` already runs:

- a cue's chance and jitter come from a hash of the tick and the cue's id - replay-identical, and
  nothing touches the shared `RRandom` stream;
- the waiting queue is keyed by tick, so pause and `sim_step` are exact;
- zones are tested by the rules, so a recording replays through the same zone at the same tick.

**One replay catch:** `once per session` is state that outlives a restart, so a replay from a
session that had already heard a line behaves differently from a fresh one. The recording's start
state (`CaptureRecordingState`) needs to carry which lines have been heard, or those zones should
read as unheard during a replay.

---

## 8. Engine and app

**Engine: `core/CueSystem`.** Generic, knows nothing about archery:

- the cue table type and the waiting queue;
- `Signal(id, value, x)` for events, `BeginScope` / `EndScope` for scopes (edges and moves are
  scopes, a move tick is a delay after its scope began), `Tick(tick)` to fire what is due;
- variation picking with no repeat, gaps, chance and jitter, voice groups with ducking;
- plays through `SoundSystem` by name - particles and camera shake can be further outputs of the
  same cue later.

**App: the cue table and the wiring.** A table in the archer like `ARCHER_CLIPS`, and the few lines
that turn `StageEvents` into signals and scopes. Zones and the "has she done Y" counters are rules,
so they live in `Stage`.

**Tuning and checking.**

- **One cue panel** replacing the three kick sliders: every cue's delay, jitter, chance and volume
  on sliders, and a button to fire it. The panel prints the row to paste into the table when a
  number is right, like `MeasureKickClip` prints the `KICK_TICKS` to type.
- **A cue log over MCP** - the last N cues fired, each with its tick, its scope, and why it was
  skipped if it was. That replaces reading `sounds_playing` and is what makes a replay a real test:
  replay a recording, read the log, compare it with the last one.

---

## 9. Sounds to author, for events the game already has

| event | sounds |
|---|---|
| footsteps (needs markers) | 4 variations each on grass, wood, stone; a scuff for the stop and the turn-around |
| jump | a small effort breath and a cloth whoosh |
| landing | `jump_landing`, and a heavier variant for a hard landing (the Puppet already bands soft and hard) |
| head bump | a thud and an "ow" |
| ledge | hand slap on the catch, effort and scrape on the climb, a small release |
| kneel / stand | cloth rustle, a knee on the ground; a "hmm" when there is no room to stand |
| level-entry get-up | a sigh or `phfieuw` |
| rope | grab, a creak that follows the swing speed (a held sound, like the bow's), release, a whoosh on a rope jump, a snap when the rope goes |
| bow | quiver rustle and a nock click as the arrow goes on; a string relaxing when a draw is cancelled |
| arrow | a flight whoosh stopped when it sticks; hits by material - target, crate, stone, ground, the cracked wall |
| scoring | `nice_shot` for a 10; lines for a near miss and a poor score |
| crates | a scrape while she pushes one, a thump when one falls or topples |
| walls | a crumble for the brick and the cracked wall; small debris knocks, with a gap |
| ambience | `archery_range` looping on the range; wind and birds on the main level |
| narration | per zone, with its subtitle text |

**Events the rules do not report yet**, needed by the table above: the nock (today an edge the app
derives), a cancelled draw, a push starting and stopping, a prop landing or toppling (from rp3d's
contact callback - read the velocity on `ContactStart` only, it is reported before the solver runs),
the animation markers, and the zones.

---

## 10. Steps

1. **`core/CueSystem`** - table, signals, scopes, waiting queue, variations, gaps, groups; the MCP
   cue log.
2. **Move the seven existing sounds onto cues** with no change in what is heard, and prove it with
   the kick and bullseye replays against their current timings (swing 11; land on connect; shout
   40% on 10..16; `nice_shot` on the 10).
3. **The cue panel**, replacing the kick sliders.
4. **The missing events** in section 9, each with a rules test.
5. **Animation markers**, then footsteps on them.
6. **Zones** in `Stage`: the rectangle, enter/leave/stay, how-often, conditions, drawing and editing;
   then the narrator group, ducking and subtitles.
7. **The cutscene scope** - the level-entry get-up rebuilt as the first one.
8. **Audio follows the pause**: `sim_pause` pauses what is playing, not only what is waiting.

---

## 11. Open questions

- **The table in code or in a file?** In code like `ARCHER_CLIPS` is the house arrangement and
  needs nothing new; a data file could be hot-reloaded (core has a file watcher) and edited without
  a rebuild, which suits tuning by ear. The panel's "print the row" covers most of that either way.
- **Zones authored in code or in Blender?** Code first, like the blocks. A named box in the blockout
  export is the obvious later route, and would put narration placement in the same tool as the level.
- **The narrator's voice** - a separate character, or her thinking aloud? It decides whether her own
  lines and the narration can ever overlap.
- **Subtitles on or off by default**, and whether her own lines get them too.

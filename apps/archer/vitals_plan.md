# Vitals Plan

Her body as two slowly moving numbers, **exertion** and **fear**, and the two sounds that make them
heard: her **breathing** and her **heartbeat**. Talked through and BUILT 2026-09-27 - see Built,
at the end, for what is in and what is still open.

It is small on purpose. There is no health or power yet, because the mechanics they would serve
are not in, but this is where they will live when enemies arrive, and nothing here should need
moving to make room for them.

---

## Decided

- **The vitals live in the rules, in `Stage`.** Nothing reads them but the cues today, but aim
  wobble when she is winded, a weaker grip when she is scared and a sprint that costs stamina are
  all obvious next steps, and anything that changes play is `Stage`'s (`cue_plan.md` section 2).
  Being in `Stage` also makes them deterministic, restored by a recording and cleared by `NewGame`
  for free.
- **Two kinds of number, kept apart.** A *level* drifts with what is happening and nothing spends
  it: exertion, fear. A *resource* is spent and refilled by events: health, power, later. If
  exertion ever becomes stamina, that is the moment it changes kind, and it is a decision, not a
  drift.
- **Breathing is her voice.** It goes in the `her` group with the voice lines, below them, so a
  breath never plays over a "huh" and a voice line never waits for a breath.
- **Breathing comes in and out, the heartbeat as whole beats.** For the heartbeat only the rate
  and the loudness change. Both are one file per half-breath or beat, fired by a clock, never a
  loop, so the rate can change between any two of them.
- **Sound only, for now.** Exertion and fear will change how she behaves (2026-09-27: "yes, but I
  don't know how yet"), so they are built as rules values, but nothing reads them except the cues
  until that is decided.
- **Fear is the music's suspense.** `cue_plan.md` already names suspense as a parameter the music
  reads; it is this number, not a second measure of "how tense is this" that drifts away from it.

---

## 1. The state

```cpp
struct StageVitals{
    float exertion = 0;     //0 rested .. 1 spent; rises fast, falls slow
    float fear = 0;         //0 calm .. 1 terrified; rises fast, falls slower still
    float heart_rate = 0;   //beats per minute, lagging both - the body does not jump to a rate
};
```

A member of the `Stage`, stepped in `Stage::Tick` after her movement (so it reads the tick's mode
and speed), in the recording state with everything else, zeroed by `Reset`. Each is a leaky
integrator toward a **target** the tick works out, with a rise rate and a separate, slower fall
rate: the whole feel is in those rates, and they are defines with the reasoning beside them, the
way the rest of `Stage.h` is tuned.

The app publishes all three to the cue layer each tick with `cues.SetParameter`, the way a cue
already reads speed or power, and `archer_state` reports them.

## 2. Exertion

**The target comes from what she is doing**, all of it already in `Stage`:

| doing | target |
|---|---|
| standing, kneeling, lying | 0 - recovering |
| walking | holds - neither rises nor recovers |
| jogging | a little |
| sprinting, climbing a ledge, on the rope | high |
| hanging from a ledge | rises slowly - holding on is work |

**Efforts add a kick on top**: a jump, a kick, a mantle. A burst, not a level, so three quick jumps
wind her where one does not.

**Fast up, slow down.** She should still be breathing hard for several seconds after a climb, and
the heart rate should lag behind that again.

One number is enough to start. A slow *fatigue* under it (tired after a long run, not just after
the last sprint) is the obvious second one, and is left until something wants it.

## 3. Fear

There are no enemies yet, but there is danger, and nearly all of it is **height**:

- **Hanging over a drop**: how far below her feet the ground is. Hanging from the low ledge is
  nothing; hanging over the gap is not.
- **Near an edge**: standing within a step of where the ground ends, scaled by how far down it
  goes. The middle of a floor is 0 however high up it is.
- **Falling**: `PredictLanding`'s landing speed while airborne. A long fall ramps fear up before she
  lands, and the landing is when the heartbeat is loudest.
- **Losing her balance** on a branch (`f_lost_balance`), and the wobble before it.
- **Almost falling off an edge.** There is an animation for it (a teeter at the lip) but no system
  yet. Until there is, the near-an-edge term stands in for it. When the teeter becomes a rules
  state, starting one adds a burst of fear on top of the edge term, the way an effort adds a
  burst of exertion.
- **Later**: the bridge creaking and cracking (`bridge_crumble_plan.md`), a crumbling rock under
  her, the tightrope - and enemies, which are one more input, not a redesign.

The ground-below query is a column scan of the blocks at her x; the rules already know the blocks.

**Fear falls slowly.** The best moment for a heartbeat is the second or two after a near miss, when
the danger is over and it is still pounding. That tail is the tuning to get right.

## 4. The heart rate

```
target bpm = rest + exertion part + fear part       (about 65 at rest, 170 at the top)
```

**The rate comes from both; how loud it is comes from fear.** A real heart races after a sprint
too, so she can have a fast heart that is not heard. Only fear makes it loud, plus exertion at the
very top (spent, the pulse is in her ears).

The heart rate is smoothed separately from its two inputs, so it trails them.

## 5. The clocks

**The rates are rules; the clocks are presentation.** Exertion, fear and heart rate are in `Stage`.
The breath clock and the beat clock that turn them into signals live in the app, beside
`SignalFootsteps`, because they are only sound, and the breath clock needs the footsteps (below).
Their phases go into the recording state beside `cue_history`, so a replay breathes the same.

If the heartbeat ever moves anything in play (aim that sways with the pulse is a classic), the beat
clock moves into `Stage` with it. Not before.

**The beat clock** advances by `heart_rate / 60` beats a second and fires `heartbeat {fear,
exertion, bpm}` on each beat. At 170 bpm a beat is 0.35 s apart, so the file should be shorter than
that or allow `max_instances: 2`. If the fast beats sound smeared, pitch is the knob, but it is not
planned; you said rate and loudness only.

**The breath clock** fires `breath_in {exertion}` and then `breath_out {exertion}`, one pair per
breath (see The files, below):

- **At rest** the period is long and the gain is 0, so she is silent; the breaths fade in as
  exertion rises. Heavy breaths, when they exist, are a second pair of cues on the same signals
  split from these by a `when` on exertion, each with its own `gain_by`.
- **Spent**, the period is 1.35 s: an in-breath and an out-breath are 0.9 s of file together.
  The period falls with the square root of exertion, so breathing quickens early in an effort.
- **Running, breaths lock to her feet**: one breath every two or three footsteps (fewer when
  harder), the way runners breathe. The clock still sets the rate; the footstep chooses the tick.
- **An effort is a breath out.** A jump or a kick, heard as a "huh" or not, drops the out-breath
  still pending, and the gasp back in comes half a second later, or sooner if one was due. (The
  first version restarted the whole cycle from "just breathed out", and someone jumping every two
  seconds never breathed at all.) On the effort, not on the voice line, so the flow stays one
  way: the app never needs to know whether the cue layer played the voice.

## 6. Her voice, all of it

Every sound she makes goes through the `her` group, which already plays one line at a time. What
is missing is the order. The group's `priority` and `busy: interrupt` settle it: a higher priority
line cuts a lower one, and equal priorities still skip, as all of her lines do today.

| priority | cues | busy |
|---|---|---|
| 2 | `land_grunt`, later pain and hits | interrupt |
| 1 | `kick_shout`, `jump_voice`, `nice_shot` | interrupt |
| 0 | `breath_in`, `breath_out` | skip |

So a grunt cuts a breath or a shout, a shout cuts a breath, and a breath arriving while she speaks
is skipped. The breath clock does not wait for it; it just goes on to the next breath.

Assigning these priorities can happen before any breath exists, and it changes nothing heard (the
lines are equal today, and a grunt cutting a shout needs both at once, which the baselines never
have), so the seven baselines staying identical is the check.

The heartbeat is not her voice, and it overlaps everything. It gets a `body` bus of its own, and
later a **duck of the ambience and the music** while it is loud, which is the cinematic version of
fear. NOT BUILT: a cue ducks through a group, and a group plays one sound at a time, which a
heartbeat running into its own next beat cannot be in; and the ambience bus has nothing on it yet.
When there is ambience or music to duck, that wants a group that ducks without taking turns.

---

## Build order

1. **`StageVitals`** with exertion and fear, the column-scan drop query and the heart rate.
   `make rules` tests: a sprint raises exertion and rest lowers it; hanging over the gap is more
   fear than hanging from the low ledge; standing mid-floor is 0; fear decays after a drop. Report
   in `archer_state`; a sim command to pin a value for tuning (a command, so it stays
   deterministic).
2. **The voice priorities** in `archer.json`. Baselines unchanged.
3. **The breath clock**, the light and heavy cues, the footstep lock and the effort reset, on your
   files.
4. **The beat clock**, the heartbeat cue, the `body` bus and the duck.
5. **Graphs** of the three values in the cue panel, the cue plan's step 5, to tune the rates by
   watching them.

Each sound step re-baselines the recordings, after showing that the diff is only breaths or beats.

## The files (measured 2026-09-27)

| file | length | shape |
|---|---|---|
| `breathe_in_normal_1..2` | 0.30-0.44 s | quiet (RMS peak 0.02-0.04) |
| `breathe_out_normal_1..3` | 0.45-0.55 s | twice as loud as the in-breaths, peak about 0.1 s in |
| `heartbeat_1..2` | 0.75 s, stereo | lub at 0.14 s, dub at 0.42 s, silent after 0.46 s |

So a breath is **a pair**, in then out, and the breath clock fires both halves: the in-breath on
the cycle, the out-breath once the in-breath has had its time. Only the "normal" breaths exist so
far; heavy ones are a second pair of cues on the same signal when they arrive.

The heartbeat's 0.10 s of lead-in does not matter (nothing has to land on the beat), but its lub
to dub is fixed at 0.28 s. **Above about 130 bpm the next lub arrives on top of the dub**, so
either the top rate stays near 130 or the file overlaps itself up there, which is a listening
question. Two files alternate, which keeps the beat from sounding looped.

## Built (2026-09-27)

- **`Stage::vitals`** (`StageVitals`, the `VITALS_*` defines in `Stage.h`), stepped by
  `Stage::TickVitals` last in `Stage::Tick`, zeroed by `Reset`. `Stage::DropBelow` is the column
  scan; pads, branches and ramps are not floors to it. `make rules` has 17 new checks
  (`TestVitals`, on a layout of its own): the middle of a floor and the lip of a 2-unit step are
  no fear, the lip of a 15-unit pit is 0.4, a step back from it less; hanging over the pit 1.0 and
  the same lip over a floor 0.03; the fear tail (0.82 a second later, 0.14 ten seconds later); a
  long fall frightening before it lands and the hard landing adding to it; a flat jump none; a
  sprint winding her, standing recovering faster than walking, twenty seconds' rest resting her;
  three jumps telling; Reset. 661 checks, 0 failures.
- **`ApplicationArcher::SignalBody`**, the two clocks, beside `SignalFootsteps` (which now
  returns its count). The clocks' phases and the vitals are in the recording state
  (`vitals`, `body_clocks`), and `archer_state` reports `vitals`.
- **`archer.json`**: `breath_in` and `breath_out` (the five "normal" files, silent below exertion
  0.15, `her` group priority 0), `heartbeat` (both files alternating, heard from pound 0.2, the
  `body` bus), and the voice priorities from section 6.
- **Checked in the running game** (on port 8768, muted): a six-second sprint on the test ground took
  exertion to 0.71 and her heart to 102 bpm, with a breath every 1.7-2.1 s getting louder, then
  fading at rest; no heartbeat, since she was winded, not afraid. A drop from 20 units: the
  heartbeat from mid-fall, loudest just after the grunt and the shake, slowing and fading over
  about eight seconds.
- **Baselines**: the diff against the old ones was only breaths and heartbeats added, plus
  breaths cut by her shouts (`stop` lines), with no other line moved, so the priorities changed
  nothing else. Re-written, and two check runs after it identical.

**Then, the same day:**

- **The HUD**: a small card at the top right of the game, panels or not (`DrawVitalsHud`) - her
  heart rate with a dot that swells on each beat and fades until the next, and her exertion as a
  bar going from the moss to amber past 0.6. Off with the panel's HUD box.
- **Graphs** of exertion, fear and heart rate over the last 20 seconds, at the top of the Cues tab.
- **Holds**: ARCHER_CMD_VITALS pins exertion or fear at a value, from the panel's hold boxes or
  `archer_vitals` over MCP, so the breath and the heartbeat can be listened to at one level. Held
  past a restart until let go. Checked: exertion 0.9 and fear 0.8 held took her heart to 161 bpm,
  with full-strength breaths and a heartbeat every 23 ticks.

**Not built yet**: the duck (above). The breaths' and heartbeat's levels against the rest of the
mix want ears.

## Open

- **Does exertion ever limit her, and does fear?** Yes, both will, but not how yet. Sound only
  until then.
- **The teeter**, see Fear: the one fear source with an animation waiting for a system.
- **Fatigue**, the slow second exertion, when a long level wants it.

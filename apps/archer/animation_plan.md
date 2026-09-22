# Archer Animation Plan

How other engines handle heavy blending and mid-clip changes, what this engine already has, what the
archer's own asset actually contains, and the order worth building it in.

The problem this is answering: the archer is a **side-view, fast-paced** platformer, so most
animations involve turning around, and actions have to start before the previous one has finished.
That is three separate problems wearing one coat, and they have three different answers — which is
why "blend faster" does not fix it.

> **Two older docs cover neighbouring ground: `docs/animation_state_machines.md` (the Layer 1-5
> survey) and `docs/animation_root_motion.md`. Both predate the root-motion rewrite and describe
> `AnimationGraph` and `f_end_orientation_different`, neither of which exists in the code any more
> (checked 2026-09-21). Read them for the concepts, not for the API.** What is current is
> `RootPose`/`RootMotionDelta` swing-twist extraction and the per-clip
> `extract_horizontal_root_motion` / `extract_vertical_root_motion` flags in `core/ObjectAnimation.h`.

---

## The asset, measured

`apps/archer/assets/meshes/archer.glb` — one skin, one skinned mesh, fourteen clips, one 4096x4096
base-colour texture. Everything below is read out of the file rather than assumed, and the app
re-measures all of it at load (`ApplicationArcher::MeasureClips`) so a re-export corrects these
numbers instead of contradicting them silently.

```
skin        archer_armature, 65 joints, single root (mixamorig:Hips)
mesh        archer, 23,699 verts / 61,197 indices, skinned, elf_archer_material
rig height  0.8911 units in bind pose  ->  scaled 2.020x to stand ARCHER_MODEL_HEIGHT (1.80)
```

| clip | secs | loops | native | at 2.02x | rate at a full run |
|---|---|---|---|---|---|
| `Idle` | 12.30 | yes | — | — | — |
| `Idle_LookingAround` | 12.50 | yes | — | — | — |
| `Walking` | 1.00 | yes | 0.78/s | 1.58/s | 5.7x |
| `Walking2` | 1.90 | yes | 0.73/s | 1.48/s | 6.1x |
| `Running_Slow` | 0.77 | yes | 1.45/s | 2.92/s | 3.1x |
| **`Running_Fast`** | **0.57** | yes | **2.57/s** | **5.19/s** | **1.73x** |
| `Running_TurnAround` | 0.70 | no | in place | — | — |
| `Kick_Front` | 1.13 | no | in place | — | — |
| `Climb` | 2.80 | no | a mantle: +1.01 up, 1.50 forward | — | — |
| `Crouch` | 4.00 | no | in place | — | — |
| `Stretching` | 11.07 | yes | in place | — | — |
| `WarmUp` | 14.67 | yes | in place | — | — |
| `Dance` | 8.67 | yes | in place | — | — |
| `Twirl` | 4.00 | yes | 0.22/s | 0.44/s | 20.3x |

(`Twirl` travels 0.879 units while spinning a full turn that nets -3.6 deg - see the yaw rule below.)

**`Running_Fast` closes the locomotion gap almost entirely.** `ARCHER_RUN_SPEED` is 9.0 and that
clip covers 5.19 units a second, so a full sprint plays it at **1.73x** — inside `PUPPET_RATE_MAX`,
which is to say the feet very nearly plant. The first export had only the walk and slid **5.7x**.
What is left is a ladder rather than a gap:

```
idle  ->  Walking 1.58/s  ->  Running_Slow 2.92/s  ->  Running_Fast 5.19/s  ->  game tops out 9.0
```

`Puppet::Choose` picks whichever rung needs the least stretching, judged in RATIO rather than in
units per second, then stretches it. That is deliberately the same search the blend space wants:
step 1 keeps it and takes the two rungs either side instead of the single best one.

`Running_TurnAround` is an in-place pivot despite the name — it is a §7 option-3 clip, not
locomotion.

Two things the export decided that the app corrects:

- **`metallic` arrives at 0.6.** This app turns the skybox off, and a metallic surface with no
  environment to reflect gives up its diffuse term and gets nothing back — it renders **black**,
  which is the trap the palette comment in `BuildMaterials` already names. `BuildArcherModel` turns
  it down to 0.10 before the renderer takes its copies, rather than in Blender, because a number
  the exporter writes is a number a re-export can quietly put back.
- **A second, un-rigged copy of the body** sits at the scene root (`tripo_...`). Asking for the mesh
  by node name leaves it behind, but it is a good fraction of the file's 5.8 MB.

And one worth knowing about rather than fixing: **29% of vertices carry a fourth bone influence**,
and this engine skins with **three** (`GLTFLoader::GetSkinnedVertex`: *"We only store 3 bones
because that how we roll"*). The first three weights average 0.9835 and never fall below 0.761, so
the cost is at most a few percent of shrink on 4% of vertices. Exporting with max 3 influences
would make it exact.

## Where the engine is today

The state machine lives in `Object` (`core/Object.h:302-347`, `Object::ApplyAnimation`) and is a
**two-clip crossfade**: one clip fading out, one fading in, mixed by a single factor.

| Capability | State | Where |
|---|---|---|
| Crossfade between two clips | Yes | `Object::TransitionToAnimation`, `Animation::Lerp` |
| Per-transition blend times | Yes, sparse `(from,to)` table with wildcard | `Object::SetBlendTime` / `LookupBlendTime` |
| Root motion, position + yaw, per-axis opt-in | Yes, yaw since 2026-09-22 | `Animation::SampleRootMotion`, `LerpRootMotion` |
| Clip-level interruptibility | Yes, one bool for the whole clip | `Animation::interruptible` |
| Reverse / park a clip mid-play | Yes, one signed rate | `Object::SetAnimationRate` |
| Per-bone mask | Yes, but static and un-layered | `ObjectAnimation.cpp:95-100` |
| Bone influences per vertex | **Three**, not four | `GLTFLoader::GetSkinnedVertex` |
| **Retarget to a third clip mid-blend** | **Refused** | `TransitionToAnimation`, with a note explaining why |
| Parametric blending (blend space) | Yes, since 2026-09-22 | `Object::SetBlendPair`, `Puppet::Choose` |
| Phase / foot sync between clips | Yes, measured per clip | `Object::blend_phase_offset`, `Puppet::clip_phase` |
| Additive poses | No | — |
| Time-ranged cancel windows | No | — |

The refusal is the important row. `TransitionToAnimation` will rewind a blend if asked to go back
where it came from, but a request for a genuinely new clip mid-blend is dropped with a warning,
because honouring it would mean either blending three clips or snapping. That is the correct call
for a two-clip crossfade — and it is exactly the wall a fast-paced game hits. The industry's answer
was to stop crossfading clip pairs, not to extend the crossfade.

### Root motion, the translation half

**Set per clip on 2026-09-22, and it had been at its default of OFF for every clip since the model
first loaded.** `extract_horizontal_root_motion` and `extract_vertical_root_motion` both default to
"the translation stays on the bone", which is the right default for the engine — a clip should opt
into moving the character rather than doing it by surprise — and the wrong setting for every gait
in this game.

`Stage` owns where the archer is, and `SyncArcherAnimation` writes it every tick. So an
un-extracted stride is drawn *on top of* the position she has already been walked to: the hips
creep ahead over the cycle and snap back when the clip wraps or is replaced. Measured off the
authored track, Walking's hip runs `z` 0.000 → 0.756 rig units monotonically across its one
second — **1.53 world units**, about a body width. Running_Fast is 2.94. The snap on coming to
rest and dropping to `Idle` is the whole of that offset vanishing in one frame, which reads as the
character jumping backwards.

The column is **`f_extract_move` / `f_extract_lift`, deliberately separate from `f_travels`**, and
conflating those two is what makes this look like a one-line fix when it is not:

| | `f_travels` asks | extraction asks |
|---|---|---|
| | should the blend space measure this stride? | is the rules layer already walking it? |
| Climb | **no** — a mantle's "speed" is a meaningless number | **yes** — and vertically too |
| Twirl | **yes** — measured and reported with the rest | **no** — nothing but the preview plays it |

Vertical is its own column because the answer usually differs. Extraction pins the axis to the
**bind pose**, so setting it on a run cycle does not merely remove drift — it flattens the 0.02-unit
footfall bob, which is the bounce. Only `Climb` has genuine rise, and `Stage` is carrying her up the
ledge at the same time.

Measured after, reading the hip bone live over MCP while the clip plays:

```
clip      extracted     hip.x              hip.y                hip.z
Walking   move          0.0020 constant    0.4689 .. 0.4877     0.0371 constant
Climb     move + lift   0.0020 constant    0.5056 constant      0.0371 constant
Twirl     neither      -0.008 .. 0.117     0.4978 .. 0.5398     0.007 .. -0.405
```

Walking is pinned flat where its authored track swept 0.756, and keeps its bob. Twirl still steps
back, which is what the Blender comparison asked for. `stage_test.cpp` asserts that every rung of
`PUPPET_LOCOMOTION` extracts — that check caught a shifted column in the table on its first run.

### The other half of root motion: the yaw

**Fixed in core on 2026-09-22.** Worth keeping the diagnosis, because the symptom was misleading.

`SampleRootMotion` gated the root bone's POSITION on the extract flags and did not gate its YAW at
all: the twist came off the bone unconditionally and went to `Object::ApplyRootMotion`, whose base
implementation does nothing. So on anything that was not a `PlayerCharacter` the rotation was
removed from the pose and then discarded — and that is not "the character does not turn", it is
**the hips do not rotate at all**. Measured across every clip in this app: exactly 0.00 degrees of
hip twist, while every other bone animated freely.

That third state — stripped and not re-applied — is one nobody ever wants, and it was the default.
There are only two sensible ones:

| | pose | character |
|---|---|---|
| twist stays on the bone | as authored | does not turn |
| twist extracted to the object | as authored | turns |

`Animation::extract_yaw_root_motion` is now the choice, defaulting to false beside the two position
flags and for the same stated reason — a clip opts into moving the character rather than doing it by
surprise. `PinnedBoneRotation` mirrors `PinnedBonePosition`: extracting gives swing-only, not
extracting recomposes `swing * twist * reference` and the clip plays exactly as animated.
`LerpRootMotion` reads each side's own flag, so blending a pivot into a cycle behaves at both ends.

**The archer sets it from `ArcherClipInfo::f_turns`**, which is the authored column saying whether a
clip's hip rotation IS the clip or is the gait. Measured after the change:

```
clip                 f_turns   hip twist in the POSE   model yaw in the WORLD
Running_Fast         no            23.11 deg                0.00 deg
Walking              no            14.78 deg                0.00 deg
Dance                no            40.33 deg                0.00 deg
Running_TurnAround   YES            0.00 deg              327.10 deg
Twirl                YES            0.00 deg              316.21 deg
```

`Dance` was losing 40 degrees of hip rotation, which is the case that matters: the more a clip is
carrying, the more this took away. `Puppet::clip_turn_deg` reports each clip's measured net turn
beside its column, so a pivot with no `f_turns` (a turn being thrown away) and an `f_turns` on a
clip measuring near zero (a cycle about to wag) are both visible in the panel.

The turn is **added** to the facing the game asks for, not substituted for it — a turn authored in
a clip is relative to wherever the character was already pointing.

#### Extracting the yaw does not compose with a translation left on the bone

The one rule to know before setting the flag. The character's rotation is applied ABOVE the root
bone, so `R(yaw) * T(p) = T(R(yaw)*p) * R(yaw)`: an authored offset still sitting on the bone gets
**rotated** by the extracted yaw rather than translated. A clip that walks while it turns therefore
orbits a point instead of walking.

That is what `Twirl` was doing. Authored, it is a 0.879-unit step back while spinning; with its yaw
extracted the hips traced a circle whose radius grew to **1.759** world units — the same 0.879 times
the 2.020 model scale, swept round. With the yaw left on the bone the hips travel in a straight line
to 1.759 against an authored 1.776, and the model yaw stays put. Same numbers, one is a line and the
other is a circle.

Extracting the translation as well **is** the escape, and the note that used to sit here saying
otherwise — "Stage owns where the archer is and the app discards it" — had the argument backwards.
Discarding it is the point: `f_extract_move` takes the offset off the bone, so there is nothing left
for the yaw to sweep round. What is forbidden is extracting the yaw and *leaving* the translation.
`stage_test.cpp` enforces exactly that, and the check was confirmed to fail when the rule is broken
rather than merely passing today. A pivot needs neither flag, because it turns on the spot already.

`Twirl` never needed it anyway: measured net turn **−3.6°**. It spins a full turn and comes back, so
nothing downstream ever needed to know its facing had changed. `Running_TurnAround` measures a clean
net −180° in place, which is what a pivot looks like and what the flag is for.

#### Reading the export directly

`tools/gltf_clip_dump.py` prints what a `.glb` actually contains — clips with their duration, travel,
net turn and total yaw movement, plus the rig, the material and the skin-weight distribution — and
with `--clip NAME` dumps the root track frame by frame. No Blender, no engine, no third-party
packages.

It is the right place to settle "is the clip wrong or is the engine wrong with it", because the
export is what the engine was actually given. Read the **net** column to decide whether a clip is a
pivot and the **yaw move** column to decide whether extracting its yaw would sweep its travel; a spin
that returns nets zero and still sweeps. Treat a large number on a clip that plainly does not spin as
an artefact of the swing-twist split, which is ill-conditioned when the hips leave vertical —
`Kick_Front` reads −347°. That costs nothing while the clip's yaw is not extracted, because the
recomposition is exact however the split behaved.

**Still asymmetric, and not fixed:** the root bone's rotation write ignores `animation_mask`, where
`ApplyIntervalOnto` respects it for every other bone. That will matter for the upper-body layer in
step 2.

### One trap worth its own paragraph

**`Renderer::skinned_shader` is NULL by default and every app that draws a skinned mesh assigns it
itself.** Forgetting it produces no warning and no error: the model loads, the skeleton binds, the
clips play, the object reports itself visible and in the scene — and nothing appears. It reads
exactly like a failed asset load. Bomber's own comment says so; this app found out the long way,
after checking the mesh, the bind pose, the weights and the material first. One line, in `Init`:

```cpp
renderer->skinned_shader = new Shader(shader_skinned_vert_name,shader_lit_frag_name);
```

---

## 1. Inertialization — the direct answer to "change midway through"

Instead of sampling two clips and mixing them by a factor, **snapshot the pose** (and its per-bone
angular/linear velocity) at the instant of the switch, start the new clip immediately at full
weight, and decay the difference between snapshot and new clip to zero over ~0.15-0.25s with a
critically-damped curve.

Why it is the first thing to reach for here:

- **Cost is one pose, not N clips.** Two animations are never sampled in the same tick.
- **An interruption can be interrupted, arbitrarily deep.** A second request mid-blend just
  re-snapshots the pose that is already blending. The "can I retarget to a third clip" question
  stops existing rather than getting a better answer.
- **It is velocity-continuous**, so a reversal does not produce the dead-stop that a linear
  crossfade factor gives you.

Reference: Dave Bollo, GDC 2016, *"Inertialization: High-Performance Animation Transitions in Gears
of War"*. Unreal 5 ships it as an `Inertialization` node, and Motion Matching depends on it.

In this codebase it would **replace** `previous_animation` and `animation_transition_factor` with a
per-bone snapshot array on the object — less state than there is now, and `Animation::Lerp` /
`LerpRootMotion` stop being needed for transitions at all. The four
`ANIMATION_STATE_TRANSITION*` states collapse to one.

## 2. Layers with bone masks — so most interruptions never happen

An archer's blending problem is usually stated wrong. Drawing a bow should not interrupt running in
the first place. The standard structure:

- **Base layer**: locomotion (idle / walk / run / jump), full skeleton.
- **Upper-body layer**: draw / hold / release / reload, masked to spine-and-up, faded in over ~0.1s.

Unreal calls this *Layered Blend Per Bone*, with *Montage slots* for the transient action; Unity
calls it Animator layers plus Avatar Masks.

**The primitive already exists.** `animation_mask` is per-bone and slerps the clip's pose against
whatever is currently in the bone (`ObjectAnimation.cpp:95-100`), so applying locomotion to the
whole skeleton and *then* applying an upper-body clip whose masks are non-zero only above the spine
gives a crude two-layer result with no new blending code. Worth prototyping first for that reason.

Two rough edges to expect:

- The mask is a static per-bone value, not a per-layer weight, so fading a layer in and out means
  animating masks across the bone set.
- `Animation::time_index` lives on the `Animation`, not on the object playing it, so one skeleton
  cannot play the same clip twice at two different phases without a second copy of the clip.
  `Animation::CopyConfigFrom` exists for making those copies.

The rig's bone names are Mixamo's, so the mask set is the usual one: everything from
`mixamorig:Spine` up, plus both shoulder chains.

## 3. Additive / aim offsets for the bow pitch

Aiming up and down should not be clips. Unreal's *AimOffset* is a blend space of **additive** poses
(deltas from a reference pose) driven by aim yaw/pitch. Side view means only pitch matters, so this
is a 1D additive — or, to start, a procedural rotation applied to spine and shoulder after the
animation pass. The `RootPose` swing/twist split is the same kind of decomposition, so the shape of
this is already familiar ground in this engine.

`ArcherAnimParams::aim_deg` already carries the angle, over the full `BOW_AIM_MIN_DEG` ..
`BOW_AIM_MAX_DEG` range, whether or not the bow is drawn.

## 4. Parametric blending instead of clip-to-clip transitions

For locomotion, discrete states plus transitions is the wrong shape — it is the combinatorial
explosion `docs/animation_state_machines.md` opens with. A **blend space** (Unreal) / **blend tree**
(Unity) / `AnimationNodeBlendSpace1D` (Godot) blends idle/walk/run continuously on a speed
parameter. There is no transition to interrupt, because there is no transition; you move a float.

For a side-view game, **one 1D blend space on signed horizontal speed** (-run … -walk … idle …
walk … run) is remarkably strong, and it makes decelerate-and-reverse read correctly with no turn
clip at all. That single change removes most of the turnaround problem.

`ArcherAnimParams::speed` is that signed parameter and already exists. `Puppet::Choose` currently
picks ONE clip from it and stretches the playback rate; the blend space is the same function
answering with two clips and a weight.

## 5. Phase synchronisation — the bug that appears the moment two cycles blend

Crossfading walk into run with independent playheads mixes a left-foot-down pose with a
right-foot-down pose. The result is the classic skating/stuttering feet. Two standard fixes:

- **Sync groups / normalised time**: clips in a group share a normalised phase — the leader drives,
  followers are sampled at the same *fraction* of their own duration.
- **Sync markers** (Unreal): clips are tagged `LeftFootDown` / `RightFootDown` and the follower's
  time is warped to align the next marker.

`Animation::Lerp` and `LerpRootMotion` already take independent `this_interval` / `target_interval`,
so the normalised-time version needs no signature change — just a different choice of times at the
call site. Cheap, and it is the difference between a blend that looks authored and one that looks
broken.

This will bite on the FIRST blend, not eventually: the ladder runs 1.00s / 0.77s / 0.57s, so any
two rungs blended on independent playheads are already out of phase.

## 6. Cancel windows and input buffering — "midway through" as authored data

Fast games do not decide interruptibility per clip, they decide it **per frame range**. Fighting
games call it frame data (startup / active / recovery, with cancel-into windows); Unreal calls the
mechanism *AnimNotify States* and montage *branching points*.

`Animation::interruptible` is one bool for a whole clip. The generalisation is a small list of
`{start_time, end_time, what_may_interrupt}` — a release can be cancelled into a roll during
recovery but not during the loose frames. Two things pair with it:

- **Input buffering**: remember a request for ~6-10 ticks and fire it the moment a window opens.
  This, far more than blend length, is what makes a game feel responsive.
- **The one-deep queue** that `TransitionToAnimation`'s own comment proposes — the missing
  `next_animation` slot. If inertialization lands, this is unnecessary; if it does not, it is the
  minimum fix.

Durations here are **ticks**, per the house rule — see `Scene::GetPhysicsTick()`.

## 7. The turnaround, specifically

Three real options in 2.5D, in increasing cost:

1. **Rotate the root; do not animate the turn.** Slew yaw 180° over ~4-6 ticks while the locomotion
   clip keeps running. Nearly free, and at speed it reads fine — most 2.5D platformers do exactly
   this. Add an additive spine counter-lean during the slew (see §3) and it stops looking like a
   turntable. **This is what step 0 built** — `PUPPET_TURN_TICKS`, five ticks, deliberately through
   zero so she turns toward the camera rather than showing her back.
2. **Signed-speed blend space** (§4), so a reverse is a deceleration through zero rather than a turn
   event at all. Best fit for "fast paced", because there is no event to interrupt.
3. **Authored pivot clips** with extracted yaw root motion — which the twist extraction handles
   natively, and is the nicest-looking option. But they are blocking one-shots, which is the thing
   fighting the pacing. Usually reserved for stop-and-turn-from-a-sprint, gated on speed above a
   threshold. `Running_TurnAround` (0.70s, in place) is a clip of exactly this kind, and 0.70s is
   42 ticks — long enough that it would have to be gated on a genuine sprint stop.

### One caution on root motion

Driving *locomotion* from root motion tends to feel laggy in a platformer. Root motion is the right
answer for the ledge climbs and pivots — `Stage.h:178` already notes a climb lerp standing in for a
clip it expects to get — but for run and jump, most platformers drive velocity from code and slave
the animation to it through the blend-space parameter. That is the call this prototype has made:
every clip loads with both extract flags OFF, the rules own the motion, and the root track is
resolved only so its travel can be measured.

---

## Step 1 — the blend space. BUILT.

Two core additions and a rewrite of one function in `Puppet`.

**`Object` gained a SUSTAINED blend**, which is a different thing from the crossfade it already
had. A crossfade is an *event*: a fixed-length blend that ends by itself. A sustained blend is a
*state*: two clips sampled every tick at a weight the caller owns and can move anywhere, which
never ends on its own. That is what a blend space is, and it is why one removes transitions rather
than speeding them up — there is no transition to interrupt, because there is no transition.

The pair share **one normalised playhead** advancing at the interpolated duration, so the stride
rate eases between the two clips instead of snapping to whichever is nominally current, and
`blend_phase_offset` shifts the follower so the footfalls line up. `SetBlendPair` sets both sides
at once and re-bases the phase onto whichever clip survives when the *pair* changes — which is what
makes crossing a rung invisible rather than a pop at 0 and another at 1.

**Phase sync needed no authoring.** `MeasureClipPhases` poses the model through each clip and
watches the toe bone's height; its lowest point is the plant. Measured in-app at **0.81 / 0.67 /
0.60** for walk / slow run / fast run, against **0.83 / 0.69 / 0.62** computed independently
straight out of the `.glb` — two methods agreeing to within one sample step. The difference between
two clips' values *is* the correction.

**`Puppet::Choose` returns two clips and a weight.** It finds the rungs bracketing the speed and
mixes them by where the speed falls between their strides. Outside the ladder it takes the end rung
alone and stretches it, exactly as before.

One thing that is easy to get wrong and is worth the words: the rate is matched against what the
*blend* covers, which is the interpolated **stride over the interpolated duration** — not the
interpolation of the two clips' speeds. Those differ whenever the cycles are different lengths,
which is always. On this export the naive figure is 4% out at the midpoint, and that 4% would be
foot slide introduced by the thing meant to remove it.

**Idle is deliberately not a rung.** A shared normalised phase makes both clips complete a cycle
together — right for two gaits, nonsense for a 12.3s ambient idle against a 1.0s walk, which would
play the idle twelve times too fast. Standing up and moving off stays an ordinary crossfade. The
skating this exists to fix happens between two *cycles*.

### What it bought, measured

Sweeping the panel across the ladder (1.58 → 5.19 units/s), the worst playback rate is **1.044** —
4.4% from 1.0. The discrete nearest-rung version it replaced had its worst case at the crossover
between two rungs, where both candidates need the same stretch: √(2.92/1.58) = **1.36**, or 36%.

```
speed   clip                    blend           weight   rate
1.58    Walking                 -                 -      1.00
2.30    Walking                 Running_Slow     0.54     1.04
3.02    Running_Slow            Running_Fast     0.05     1.01
4.47    Running_Slow            Running_Fast     0.68     1.04
5.19    Running_Slow            Running_Fast     1.00     1.00
```

Still open from this step: the rate floor (`PUPPET_RATE_MIN`) still clamps below the walk's own
speed, because idle is not in the space. Putting it in properly needs a follower that runs at its
own rate rather than on the shared phase — a small addition to the blend, and the right time to
make it is when there is an air set to blend as well.

### Step 1a — the ladder's ends, and requests that arrive faster than a crossfade

Three defects showed up the moment the blend space met real acceleration, and they share one
cause: **the archer changes speed faster than a crossfade lasts.** `ARCHER_RUN_ACCEL` is 90 u/s²
and `ARCHER_RUN_FRICTION` is 120 — 1.5 and 2.0 units *per tick*. The whole ladder is 1.58 → 5.19,
so a start or a stop crosses all of it in two or three ticks, against a 9-tick fade.

**A request arriving mid-crossfade was refused.** `Object::TransitionToAnimation` warned and
returned, on the reasoning that honouring a third clip means either mixing three or snapping.
In practice a stop from a run asks for `Walking` and then `Idle` on consecutive ticks, so the
refusal fired every time — and because `SyncArcherAnimation` recorded `playing_clip` whether or
not the request took, and only asks on a *change*, nothing ever asked again. She finished the
fade into a walk she no longer wanted and looped it on the spot for the rest of the session.

It now **retargets**: keep whichever of the two clips the pose is currently nearer, and fade from
that one to the new destination. A one-deep queue was the obvious alternative and is worse — it
would honour the stale walk in full and start the idle two blend-lengths after she stopped moving.
Latency is not better than a small pop; it is a character that visibly disagrees with the game.
Measured in play, every retarget on the real input path lands at factor **0.00** (the request
arrives the tick after the fade begins), so nothing is discarded at all. Forced retargets at 0.14
and 0.78 keep `Idle` and `Walking` respectively — the nearer side each time, so the discarded
fraction is min(f, 1−f) and never exceeds 0.5. The real answer for that worst case is step 4.

`TransitionToAnimation` now returns `bool`, and the archer only records `playing_clip` when it
took. "Only ask on a change" and "assume it worked" are each reasonable and together permanent.

**Both ends of the ladder fell out to a crossfade they did not need.** The app asked "is there a
follower?" when it should have asked "can the pose be carried over?". Past the fast run the weight
is already 1.0 and the pose already *is* the fast run, so fading to it *from* the slow run went
backwards into a clip that was no longer visible and out again — a lurch at exactly full sprint.
The test is now whether the new pair shares a clip with what is on screen, in **either** slot.

**And coming back down, `SetBlendPair` had no case for it.** The clip that had been playing alone
becomes the *follower*, carrying most of the weight, and the phase was being seeded from the new
leader's stale playhead — jumping the dominant clip. It now solves for the phase that keeps the
survivor still, `phase = now − offset`, symmetric with the slid-up case above it.

Measured after: a full sweep 0.5 → 9.0 → 0.0 in puppet mode produces exactly **two** crossfades,
`Idle → Walking` and `Walking → Idle`. Everything between them is continuous. It was four.

---

## Step 5 — the air set. PARTLY BUILT (2026-09-22).

`Jumping_Up`, `Falling_Idle` and `Standing_DrawArrow` arrived in one export. The first two are
wired; the third has nowhere to go until step 2.

### The jump clip is a whole jump, and the game only has room for part of it

This is the finding worth acting on. `Jumping_Up` is authored as a complete standing jump — the
phases are unmistakable in the hip's height, sampled straight off the root track:

| phase | t | hip y |
|---|---|---|
| standing | 0.03 | 0.463 |
| **anticipation crouch** | 0.34 | **0.260** ← lowest |
| launch / extension | 0.43–0.63 | 0.296 → 0.634 |
| **apex** | 0.79 | **0.746** ← highest |
| falling | 0.93–1.13 | 0.690 → 0.456 |
| **landing absorb** | 1.33 | 0.285 |
| recovery to standing | 1.93 | 0.463 |

This game's jump leaves the ground **on the tick the button goes down** — there is no anticipation
window, and there cannot be one without adding input latency to a platformer. So 0.342s of the
clip describes something that has already happened by the time she is airborne, and playing it
from zero would have her tuck into a crouch while already travelling upwards.

The rise therefore starts at the **launch**, measured as the lowest hip height *before the apex*:

```
Clip Jumping_Up   1.933s long; launch at 0.342s (hip 0.260), apex at 0.785s (hip 0.746)
Jump rise: 0.443s of clip into 0.390s of flight -> 1.13x.
           Anticipation 0.342s and landing 1.148s are not played by the rise.
```

**1.13x is the first one-shot in this app that fits without hitting its clamp** — the kick wants
4.9x and the climb 9.3x. The rise time is not a typed constant either: `PUPPET_RISE_TIME` is
`ARCHER_JUMP_SPEED / ARCHER_GRAVITY`, so retuning the jump moves the fit with it.

Finding the launch as "the lowest point **before the apex**" rather than the global minimum
matters more than it looks: the landing absorb dips to 0.285 against the anticipation's 0.260, only
7% apart. A global minimum is one re-export away from finding the landing instead and playing the
clip from near its end.

Measured through a real jump — clip, rate and the hip's own height, sampled live:

```
world.y    vy     clip            hip.y
   1.65  14.30    Jumping_Up      0.4117
   3.27   8.00    Jumping_Up      0.6335
   3.97   0.30    Jumping_Up      0.7106     apex 0.746
   3.34  -7.96    Falling_Idle    0.4309
   1.44 -16.46    Falling_Idle    0.4387
   0.90   0.00    Idle            0.5038
```

The hip rises monotonically from the moment she leaves the ground — the crouch is skipped, not
merely shortened — and the apex pose lands within 0.04 of the clip's own peak.

### What is still missing, in the order it would pay off

1. **The landing.** She goes from `Falling_Idle` straight to `Idle` on a 0.15s crossfade, with no
   absorb at all. The absorb *is authored*, sitting unused at t ≈ 1.15–1.93 of `Jumping_Up`. It
   needs either splitting into its own clip or playing the tail from an offset on touchdown, and
   it wants a soft/hard split on impact speed.
2. **An apex.** `Falling_Idle` is a held pose (its hip moves 0.0005 units across 0.733s), so the
   top of the arc has no hang and the fall has no acceleration in the pose. One or two frames of
   apex would do more here than a longer fall clip.
3. **A run-jump.** The air set ignores `ground_speed` entirely — a sprinting jump plays the same
   standing rise. The rules test asserts this so the day a variant is authored it fails loudly
   rather than the new clip never being reached.

`Falling_Idle` being static also means it costs nothing to loop and nothing to extract, which is
why it is marked `f_looping` and nothing else.

---

## Step 0 — the seam. BUILT.

The thing the original plan was missing: it was all mechanism, and said nothing about **who decides
what plays**. Without that the decision logic lands as a pile of `if` statements in
`ApplicationArcher.cpp`, and the animation prototype cannot exist separately from the physics one.

`apps/archer/Puppet.h` is the answer, and it is the same split `Stage.h` makes:

- **`ArcherAnimParams`** is the seam — signed speed, ground speed, vertical velocity, facing,
  grounded, mode, action, action phase, aim angle, draw power. Everything the animation is allowed
  to know.
- **`DescribeArcher(stage, out)`** fills it from the rules. **A debug panel fills the same struct by
  hand.** `Puppet::Tick` cannot tell which, which is the whole point.
- **`Puppet`** answers: which clip, at what rate, which way to face, and whether the clip it chose
  is a real answer or a placeholder standing in for something not yet authored.
- **It names no engine type**, so `mingw32-make.exe rules` builds and tests it with no core, no
  window and no GPU, exactly like `Stage`. 157 checks including the rate matching, the clamps, the
  turnaround and the seam itself.

Three sources, switched live in the Archer panel or over MCP (`archer_anim`):

| source | what fills the parameters | what it is for |
|---|---|---|
| `game` | the rules | normal play |
| `panel` | sliders | judging a cycle at a chosen speed, with no level in the way |
| `clip` | nothing — one clip on loop | checking an export, including the clips the game has no use for |

In `panel` and `clip` mode the archer ignores the keyboard and stands where she was left; the rest
of the game keeps running. `archer_anim` with no arguments reports every clip's duration, its own
measured travel speed, and the playback rate it would need to plant the feet at a full run.

**What it showed immediately:** on the first export, at `ARCHER_RUN_SPEED` the walk played at 1.80
(the clamp) while wanting **5.70** — the missing run cycle, as a number, before anyone had to
squint at the feet. On the export with the two runs that reads **1.73**, inside the clamp. At any
clip's own speed the rate is exactly 1.00 and the feet are planted. The turnaround measures
90° → 54 → 18 → −18 → −54 → −90 over exactly five ticks, through the middle, while the locomotion
clip keeps running underneath it.

---

## The steps, and what each costs in animation

Ordered so nothing blocks on the step after it.

| # | Step | Code | Animation |
|---|---|---|---|
| 0 | **Parameter seam + puppet mode** | **done** | **none** |
| 1 | **Signed-speed blend space + phase sync** | **done** | **none** — the phase alignment turned out to be measurable rather than authored |
| 2 | Upper-body mask layer | layer via per-bone `animation_mask`, spine-up | **draw / hold / loose**, standing, masked-safe |
| 3 | Additive aim pitch | 1D additive, or procedural spine+shoulder after the pass | **one aim-up and one aim-down reference pose** |
| 4 | Inertialization | replaces the crossfade; deletes four states and the mid-blend refusal | none |
| 5 | Air set | jump/fall driven off `vel_y` and `f_on_ground` | **jump_start / rise / apex / fall / land_soft / land_hard** |
| 6 | Cancel windows + input buffer | `{start,end,what_may_interrupt}` ranges, 6-10 tick buffer | tuning |

Inertialization sits at 4 by position but its real trigger is **the moment the action layer gets a
second member** — once step 1 exists locomotion has no transitions left to interrupt, and every
remaining interruption is an action interrupting an action.

### 7. Retiming the mechanics to fit real clips

The expensive one, and it runs the other way: the feel constants were tuned with no animation in
sight, and several are too fast to animate.

| rules | ticks | seconds | the clip | note |
|---|---|---|---|---|
| `KICK_TICKS` | 14 | 0.23 | `Kick_Front` is **1.13s** | needs 4.9x to fit; clamped to 2.5x today, so the boot overruns the hit by ~13 ticks |
| `LEDGE_CLIMB_TICKS` | 18 | 0.30 | `Climb` is **2.80s** | needs **9.3x**; clamped to 2.5x, so the clip is still playing long after she is standing. The likeliest answer is that a 0.30s mantle was never a mantle |
| `BOW_DRAW_TICKS` | 36 | 0.60 | none yet | fine as is — that is a real draw |

Each of these is a decision about how the game *feels*, not a bug. A 1.0s climb is a different game
from a 0.3s climb. `Puppet::choice.wanted_rate` reports the gap live so the argument can be had
against a number.

### 8. Bone sockets

The arrow currently leaves a Stage-computed offset (`BOW_SHOULDER_UP` / `BOW_SHOULDER_FWD`). Once
there is a rig it should leave the hand — `mixamorig:LeftHand` holds the bow, `mixamorig:RightHand`
draws. The same mechanism carries the knife. Small, and it is the difference between a model
playing an animation and a character holding a thing.

### 9. Clip event markers

Foot plants for dust and sound, and the loose frame for the arrow. There is a real decision inside
the second one: fire on key release (responsive, can desync from the visual) or on the animation's
release frame (correct, adds latency). For a fast-paced game, fire on release and warp the clip to
catch up — but make it deliberately.

**Not planned: foot IK.** Side view on flat blocks does not pay for it.

---

## What is authored, and what is standing in

Read off the running game — `Puppet::choice.f_placeholder` is true whenever nothing is authored for
the state, and the panel lists it.

| state | clip today |
|---|---|
| standing | `Idle` ✓ |
| walking | `Walking`, rate-matched ✓ |
| running | `Running_Slow` / `Running_Fast` off the ladder, 1.73x at a full sprint ✓ |
| backing up | the same rung played backwards ✓ |
| kicking | `Kick_Front`, mistimed against `KICK_TICKS` (wants 4.9x) ✓ |
| climbing | `Climb`, badly mistimed against `LEDGE_CLIMB_TICKS` (wants 9.3x) ✓ |
| rising | `Jumping_Up` from its measured launch, fitted at 1.13x ✓ |
| falling | `Falling_Idle`, looped ✓ |
| landing | **placeholder** — straight to `Idle`; the absorb exists in `Jumping_Up` and is unused |
| hanging | **placeholder** — idle |
| on the rope | **placeholder** — idle |
| drawing / loosing | `Standing_DrawArrow` exists but nothing selects it — it needs step 2's mask layer |

The two mistimings are the largest decisions; landing and the mask layer are the largest holes.

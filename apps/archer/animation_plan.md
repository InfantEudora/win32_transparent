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

`apps/archer/assets/meshes/archer.glb` — one skin, one skinned mesh, twenty-eight clips, one 4096x4096
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
| `Kick_Front` | **1.43** | no | in place | — | replaced 2026-09-22, then trimmed by 12 frames; the strike stayed at tick 42 |
| `Climb` | 2.80 | no | a mantle: +1.01 up, 1.50 forward | — | — |
| `Crouch` | 4.00 | no | in place | — | — |
| `Stretching` | 11.07 | yes | in place | — | — |
| `WarmUp` | 14.67 | yes | in place | — | — |
| `Dance` | 8.67 | yes | in place | — | — |
| `Twirl` | 4.00 | yes | 0.22/s | 0.44/s | 20.3x |
| `Stretching2` | 4.00 | yes | in place | — | — |
| `Jump_ToAir` | 0.27 | no | in place | — | the rise |
| `Falling_Idle` | 0.73 | yes | a held pose | — | the fall |
| `Jump_FromAir` | 0.40 | no | in place | — | lands at 0.267s |
| `FallingIdle_ToLanding` | 1.10 | no | in place | — | lands at 0.300s |
| `Jumping_InPlace` | 1.93 | no | in place | — | a whole jump; preview only |
| `Jump_Forward` | 2.00 | no | 0.39/s | 0.78/s | a whole jump; not wired |
| `Standing_DrawArrow` | 1.07 | no | in place | — | needs the mask layer |
| `Running_Jump` | 0.93 | no | 2.30/s | 4.64/s | the running jump; climbs 0.333s |
| `Running_JumpForward` | 0.93 | no | 2.30/s | 4.64/s | **byte-identical to `Running_Jump`** |
| `Hanging_Braced` | 2.37 | yes | a held pose | — | holding a ledge |
| `Hanging_Rope` | — | yes | a held pose | — | gripping a rope, both hands overhead |
| `Running_ToStop` | 0.93 | no | 0.42/s | 0.85/s | plants at 0.267s |
| `Kick_FrontSpin` | 1.13 | no | in place | — | the old `Kick_Front`; a real pivot |
| `Walk_ToHandstand` | 4.00 | no | 0.33/s | 0.67/s | set dressing |

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
| Retarget to a third clip mid-blend | Yes, since 2026-09-22 | `TransitionToAnimation` keeps the nearer side |
| Parametric blending (blend space) | Yes, since 2026-09-22 | `Object::SetBlendPair`, `Puppet::Choose` |
| Phase / foot sync between clips | Yes, measured per clip | `Object::blend_phase_offset`, `Puppet::clip_phase` |
| Additive poses | No | — |
| Time-ranged cancel windows | No | — |

That retarget row used to read **Refused**, and the refusal was the wall this whole plan was written
around: a request for a genuinely new clip mid-blend was dropped with a warning, because honouring
it means either blending three clips or snapping. Correct for a two-clip crossfade, and exactly the
wall a fast-paced game hits — see **Step 1a** for what it cost in practice and what replaced it.
The industry's answer was to stop crossfading clip pairs at all, not to extend the crossfade, which
is what steps 1 to 4 are.

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
watches the toe bone's height; its lowest point is the plant. Measured in-app at **0.83 / 0.70 /
0.65** for walk / slow run / fast run. The difference between two clips' values *is* the correction.

> Those figures read **0.81 / 0.67 / 0.60** until 2026-09-22, and the gap against the `.glb` was
> written off here as "two methods agreeing to within one sample step". It was not sampling noise,
> it was a **systematic bias**, and it is worth knowing about before writing another measurement
> like these. `ObjectAnimation::GetClosestKeyframe` returns the first keyframe at or *after* the
> time asked for — a **ceiling, not a nearest** — so a uniform-grid scan does not see a curve, it
> sees each keyframe's value repeated across the samples leading up to it. Taking the sample index
> where an extreme first appears therefore reports a time up to one whole keyframe interval EARLY:
> 0.033s at 30fps, which is what the 0.02-of-a-cycle discrepancy was. Every one of these
> measurements now asks at the **keyframe times** instead, which is exact and cheaper than the grid
> it replaced.

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

## Analog input, and the blend space finally earning its keep. BUILT (2026-09-22).

The locomotion ladder has had almost nothing to do since it was built, and the reason was the
keyboard. Left and right can only ask for a *full* run, so every speed between standing and 9.0
existed for the two or three ticks acceleration took to cross it — the blend space was being asked
to track a step function and its careful weights were never held anywhere in the middle.

**Nothing in the rules had to change.** `target_vx` was already
`ClampF(in.move_axis,-1,1) * ARCHER_RUN_SPEED`, so the magnitude has always been honoured; only the
input was quantised. One `AddGamePadMap(0,INPUT_ARCHER_MOVE,6000)` and one `GetAxis` added to the
two key reads is the whole change.

Swept with the stick held, which is the first time these numbers have been reachable at all:

```
stick    vx     clip             blend            w     rate
 0.15    1.35   Walking          -                      0.85
 0.25    2.25   Walking          Running_Slow     0.50  1.04
 0.35    3.15   Running_Slow     Running_Fast     0.10  1.02
 0.50    4.50   Running_Slow     Running_Fast     0.70  1.04
 0.65    5.85   Running_Fast     -                      1.13
 0.80    7.20   Running_Fast     -                      1.39
 1.00    9.00   Running_Fast     -                      1.73
```

**1.02 to 1.04 across the whole blended band** — the feet very nearly plant, held there for as long
as the thumb holds the stick rather than for two ticks in passing. That is what step 1 was for, and
this is the first time it has been visible.

### The rate floor turns out not to bite

The open item from step 1 was that `PUPPET_RATE_MIN` (0.60) clamps below the walk's own speed
because idle is not a rung, so a very slow walk would slide. With the walk at 1.58 units/s the floor
is reached at 0.95 units/s — and the dead zone puts the slowest a real stick can ask for at about
18% of 9.0, which is **1.65 units/s**. The clamp is below anything the hardware can request, and at
that slowest walk the rate is 1.04. Worth fixing one day for completeness; not worth fixing now.

### Driving it without a stick

`archer_run` takes an `amount` (0..1) which pushes the stick that far through `HoldAxis` instead of
pressing a key. A key and a stick are genuinely different requests rather than two spellings of one
— a key can only ask for everything — so both paths are kept and the tool exposes both.

---

## Stopping — three ways, and how they are told apart. BUILT (2026-09-22).

`Running_ToStop` is wired. The other two ways of stopping are not authored yet, and the interesting
part is that discriminating between them costs nothing.

### The discriminator is arithmetic, not a threshold

Ground deceleration is `ARCHER_RUN_FRICTION`, so **one tick can remove at most 2.0 units/s**. That
single fact separates all three cases, measured in the running game:

| | vx per tick | what it is |
|---|---|---|
| let go of the key | 9 → 7 → 5 → 3 → 1 → 0 | exactly `PUPPET_FRICTION_STEP` each tick |
| run into a wall | **9 → 0** | `MoveAndCollide` zeroes `vel.x` outright |
| run into a crate | **9 → 4** | clamped to `ARCHER_PUSH_SPEED`, and she keeps moving |

A drop of about one friction step is her letting go; anything larger is something being in the way.
`PUPPET_FRICTION_STEP` is written as `ARCHER_RUN_FRICTION * ARCHER_DT` rather than typed, so
retuning the friction moves the discriminator with it.

Stage does compute `f_hit_wall` and then throws it away — declared at `Stage.cpp:410`, passed to
`MoveAndCollide`, never read. Publishing it would be the exact answer rather than this inferred one,
and it becomes worth doing when a wall-stop clip wants an impact speed to choose a soft or hard
variant with. It is not needed while the only question is which clip to play.

### It fires at the START of the deceleration

Not at the end. Waiting for her to come to rest would begin a plant-and-settle after the settling
was over — and would leave the blend space to race `Running_Fast` → `Running_Slow` → `Walking` →
`Idle` in the five ticks the stop takes. Firing on the first tick replaces that scramble with one
authored clip.

### The fit is the worst in the file, and it is a game-feel decision

```
Clip Running_ToStop  0.933s long; plants at 0.267s against the rules' 0.075s stop
                     -> wants 3.56x, capped at 2.50x
```

The clip is authored for a much gentler stop than this game has. Its own entry speed is **3.41
world units/s** decelerating over about 0.70s — roughly 4.9 u/s². The rules enter at **9.0** and
stop in **0.075s** at **120 u/s²**, twenty-four times harder, covering 0.267 units where the clip
covers 0.80.

So this is the same conversation as the kick and the climb, and the choice is the same shape:
trim the clip to its plant, or bring `ARCHER_RUN_FRICTION` down. At 40 u/s² she would slide a full
unit over 13.5 ticks, which is a readable skid; at 120 there is no room for one and the clip can
only be a plant.

### A crossfade can eat the beat it is blending to

Worth knowing generally. At 2.50x the clip reaches its plant 6.4 ticks in — **inside** the 9-tick
default crossfade. Measured with the default, the hip at the plant read **0.453** against the
clip's authored **0.345**: more than half of the thing the clip exists for had been blended away,
and nothing about that is visible in the clip or in the rate.

`Object::SetBlendTime` takes a per-transition override with a wildcard source, so one line fixes it:

```cpp
archer_model->SetBlendTime("",ARCHER_CLIPS[CLIP_STOP].name,0.067f);
```

Four ticks. Re-measured, the hip now descends 0.4623 → 0.4608 → 0.4574 → 0.4489 → 0.4434 → 0.4318
→ 0.4177 → 0.3950 → 0.3516 → **0.3523** at the plant, tracking the authored curve instead of
flattening it. **The rule: a crossfade must be shorter than the first beat of the clip it enters**,
or the clip's opening statement is averaged with the pose it is leaving.

---

## Step 5 — the air set. BUILT (2026-09-22).

Four composable clips, not one baked jump. The whole chain runs: `Jump_ToAir` → `Falling_Idle` →
`Jump_FromAir` or `FallingIdle_ToLanding` → back to the ladder.

| state | clip | how it is played |
|---|---|---|
| rising | `Jump_ToAir` 0.267s | at rate 1.0, from frame 0, then holds |
| falling | `Falling_Idle` 0.733s | looped |
| routine landing | `Jump_FromAir` 0.400s | entered at 0.267s, its contact frame |
| hard landing | `FallingIdle_ToLanding` 1.100s | entered at 0.300s, its contact frame |

### Why four pieces beat the one baked jump

`Jumping_InPlace` is the same jump in a single 1.933s clip, and it can only be used by slicing it:
find the anticipation, find the apex, fit the span to the flight, throw the rest away. It works —
it was wired that way first — but every use costs a measurement, and the landing half is
unreachable because it is glued to a rise that has already been played.

The four-piece set needs none of that, and the reason is a property of the clips rather than of
the code: **`Jump_ToAir` ends on exactly the pose `Falling_Idle` holds.** The handover at the apex
is between two frames that already match, so the crossfade there has almost nothing to blend.

That is also why the rise is **not fitted to the climb**. It is a TRANSITION into the airborne
pose, not a depiction of the rise, so it plays at its own speed and holds what it arrives at. The
rise lasts 0.390s and the clip 0.267s, so it finishes about two thirds of the way up and holds the
pose the fall is about to loop. Stretching it to fill the climb would only be a slower tuck. A cut
jump rises for as little as 0.18s, so the clip is often not finished at all — which is the right
way round, because the readable part of a tuck is the start. The rules test asserts rate 1.0 so
that nobody later "fixes" it into a fitted window.

### The landing clips start in the air, and the toe is the only thing that knows when

A landing authored on its own begins mid-fall, because that is what a landing is to an animator.
`FallingIdle_ToLanding`'s hip descends 0.7253 → 0.4701 (standing height) before the absorb bottoms
out at 0.2857. By the time this game plays it she is **already standing on the ground** — Stage put
her there, and that is what fired the landing. Played from frame 0 she sinks half a body into the
floor and pops back out.

So each landing is entered at its own contact frame, measured at load:

```
Clip Jump_FromAir           0.400s long; feet land at 0.267s, leaving 0.133s to play
Clip FallingIdle_ToLanding  1.100s long; feet land at 0.300s, leaving 0.800s to play
```

**Measured on the TOE, not the hip**, and `Jump_FromAir` is the case that proves why: its hip only
ever rises (0.4293 → 0.4646), so the hip track says contact is at t=0 — while the toe says 0.267s.
She is extending her legs downward to reach the floor, which moves the foot and the hip in opposite
directions. The hip is also useless on the other clip for the opposite reason: it keeps moving long
after contact, because that motion *is* the absorb.

Contact is the FIRST sample within a hair of the toe's lowest point, not the lowest point itself —
that would find the middle of the plant rather than its beginning.

A flag could not have solved any of this. `extract_vertical_root_motion` pins a whole axis, and on
`FallingIdle_ToLanding` the same axis is a fall for 0.300s and then a performance for 0.800s.

Measured stepping through a real hard landing, reading the hip bone live:

```
0.3621  0.2925  0.2866  0.3124  0.3587  0.4079  0.4436  0.4596  0.4623  0.4627
                ^ deepest, against the clip's authored 0.2857          ^ standing, authored 0.4628
```

It opens already below standing height — the 0.52 world units of descent in front of it were
skipped, not compressed — bottoms within 0.001 of the authored absorb, and recovers to standing.

### Which landing, and when there is none

Chosen on impact speed, against the arcs this game actually produces: a tapped jump lands at 8.6, a
full jump at 18.7, a 5.5-unit drop at 25, and terminal velocity is 34.

- below `PUPPET_LAND_VEL` (5.0) — **no landing at all**. Stepping off a kerb is not a landing, and
  playing a recovery for one would put a hitch in ordinary walking.
- up to `PUPPET_HARD_LAND_VEL` (25.0) — `Jump_FromAir`. A routine jump is deliberately in this
  band: if the dramatic landing played every time you used the jump button it would stop reading as
  dramatic.
- above it — `FallingIdle_ToLanding`, which only a real drop reaches.

**The landing cancels the moment she moves.** The rules never stopped her — she can run the tick
she touches down — so an animation holding her through a 0.825s recovery would be the animation
layer overruling the game. Landing at a full run goes straight to `Running_Fast`, measured. That is
the cheap version of step 6's cancel windows and the honest one until those exist.

The landing is the Puppet's only piece of remembered state, and the only thing in it that is an
EVENT rather than a function of the current parameters — which is why it lives in `UpdateLanding`
and why the impact speed has to be read from the tick *before* contact. Stage zeroes `vel_y` in the
same tick it sets `f_on_ground`, so by the time a landing is visible the number that decides how
hard it was has already been thrown away.

### The running jump, and why it is latched rather than derived

`Running_Jump` is **one whole arc**, not four pieces, and it does not need to be composable: every
frame of it is airborne, so there is no anticipation to skip and no landing glued to the end. It is
entered at takeoff, played once, and holds its last frame — already a descending pre-landing pose,
and a better thing to hold than a static float — if she is still in the air when it runs out.

```
Clip Running_Jump           0.933s long; climbs for 0.333s against the game's 0.390s -> 0.85x
```

**Only the climb is fitted.** The clip's descent is 0.566s against the game's 0.327s, so no single
rate serves both halves; the climb is the half with the push in it and the half whose length the
rules actually guarantee (`PUPPET_RISE_TIME` is `ARCHER_JUMP_SPEED / ARCHER_GRAVITY`). At 0.85x a
full jump lands about two thirds of the way through the clip, mid-descent.

**Chosen by a threshold, not a blend.** Two gaits can be mixed because they are the same cycle at
different speeds. `Jump_ToAir` is 0.267s of tuck and `Running_Jump` is a 0.933s arc — a shared
playhead between them would mean nothing. Two discrete sets, chosen once, is the honest shape.

**And latched at takeoff, which is the part that would be easy to get wrong.**
`ARCHER_AIR_FRICTION` is 14 u/s², enough to take her from a full run to a standstill *inside a
single flight*. A choice re-made each tick from the current `ground_speed` would therefore cross
`PUPPET_RUN_JUMP_SPEED` in mid-air and swap a running jump for a standing one halfway through the
arc. Measured in flight, `vx` decaying 9.00 -> 8.07 -> 7.13 with the clip unchanged:

```
y= 3.96 vy=  1.00 vx= 9.00  Running_Jump   rate=0.85
y= 3.69 vy= -5.12 vx= 8.07  Running_Jump   rate=0.85
y= 3.19 vy= -8.90 vx= 7.13  Running_Jump   rate=0.85
y= 2.70 vy=  0.00 vx= 4.43  Running_Slow   rate=1.04     <- landing cancelled, she keeps running
```

The latch is re-taken on the next takeoff, and it fires for walking off a ledge as well as for
jumping — the animation cannot tell those apart and should not try. What it needs to know is how
fast she is travelling, not why.

### Still open

1. **No apex hold.** `Falling_Idle` is a held pose — its hip moves 0.0005 units across 0.733s — so
   the standing set's fall has no acceleration in it and the top of the arc has no hang.
2. **`Standing_DrawArrow`** (1.067s) is previewable and unselected. It is a whole-body clip for
   something that must happen *while* she runs, so it needs step 2's mask layer, not a state.
3. ~~**The rope** is the last placeholder state.~~ Done — `Hanging_Rope`, and see the rope section
   below for the two solver bugs that drawing her tilt made visible.
4. **`Running_Jump` and `Running_JumpForward` are byte-identical** — every frame of the root track
   matches, as do the duration, the travel and the net turn. One of them can come out of the
   export. `Jump_Forward` is a genuinely different, floatier arc (0.776 rig over 2.000s against
   2.150 over 0.933s) and is worth keeping to compare against.

---

## The rope, and a kick that needs the ground. BUILT (2026-09-22).

Two small things that turned out to be about the same seam from opposite sides.

### A kick is something you do with your weight on the ground

An air kick used to be allowed, and the note in `Stage.h` argued for it: a flying kick keeps its
arc. What killed it was the retiming. When `KICK_TICKS` was 14 an air kick was a flourish; at 86 it
is **1.43 seconds of hanging motionless in mid-air** — longer than a whole jump — playing a clip whose
wind-up, plant and recovery all want a floor to push against. So `!f_on_ground` joined the busy list
next to hanging, climbing and the rope.

**Gating the start was only half of it.** The plant is friction rather than a freeze, so a kick
thrown at a full run carries about a unit of slide, which is easily enough to go over a lip — and
a kick that STARTED on the ground would then finish in the air, which is the same picture for the
same second and a half. So the move ends where the ground does. Measured with the second half
removed, she kicked in mid-air for **22 ticks**; with it, for **1**, and that one tick is the
ordering rather than a hole (`TickKick` runs before `TickArcher`, so the tick she goes over the edge
on has already had its kick update). The test counts them rather than forbidding them, because 1 is
the ordering and 90 is the bug.

### Hanging on a rope, and which way is up

`Hanging_Rope` is a different grip from `Hanging_Braced` rather than the same pose twice: a ledge is
braced against with bent arms and the feet on the wall, a rope is hung from with both hands
overhead. Selecting it is one line in `Puppet::Choose`. **Which way up she hangs is not, and could
not be** — `Stage::TickArcher` stands aside for the whole of `MODE_ROPE` so that the swing can be
emergent, so there is nothing in the rules that knows her tilt and nothing that should. It is a view
quantity, read off the collider the solver is already swinging.

Composed OUTSIDE the yaw, which matters: the tilt is about the WORLD's Z. Inside the yaw it would be
about her own z and would flip every time she turned to face the other way, so she would lean out of
the swing instead of into it.

### Looking at the collider was worth doing for its own sake

The first measurement of the new roll said the request could not be satisfied as asked, because the
collider was not worth aligning to. **The archer on a rope is two pendulums, not one:** the rope
swings, and she swings about her own grip inside it — and only the rope's had any damping. Swept
through one arc:

```
                             her tilt    the rope's    error
before   one arc, worst        88 deg        53 deg     35 deg   ...and still growing
after    the same arc          42 deg        39 deg      2 deg
after    pumped much harder    72 deg        67 deg      5 deg
```

The ERROR is the figure that means anything, because the absolute angle is just how hard the swing
was pumped. Before, it grew without bound and she was simply windmilling inside the rope; after, it
stays within a few degrees at any amplitude.

Two things were wrong and both were invisible while nothing drew her tilt:

1. **The joint anchored wherever it fell.** A ball-and-socket takes its anchor on each body from
   where that body happens to be, and she was left standing where she caught it — so the rope
   attached through her chest, and, if she caught a link BELOW her, below her centre of mass. That
   is not a pendulum at all, it is an inverted one: she would slowly turn over and hang upside down,
   and nothing in the solver was wrong about it. She is now moved so the link is at the top of her
   box, which is what catching a rope means. Checked afterwards: her hands sit **0.026 units** from
   the link she is holding.
2. **Nothing damped her own spin.** Her natural period about her hands is 2.2s, so critical damping
   is about 5.6; `ROPE_HANG_DAMPING` is 3.0, a little over half. Deliberately not critical — a body
   that snaps rigidly into line with the rope reads as a plank, and the few degrees of lag between
   her and the rope, which lead on the push and trail at the top, are the part that looks alive.

A third thing fell out of writing it down: a kinematic body keeps whatever rotation it was last
given, and nothing else ever writes the archer's orientation — so without a reset on release the
collider box would stay leaning at whatever angle she let go at, for the rest of the level. Both
ends now reset it, and the release measures **drawn 0.00, collider 0.00** the tick after.

**The grip point and the reach point are different numbers on purpose.** `Stage::FindRopePoint`
measures reach from `ARCHER_HALF_H * 0.6`, chest height, because that is where hands rest; the joint
anchors at `ARCHER_HALF_H`, the top of her, because a hanging grip is overhead. Using the reach
point for both hung the rope through her neck — visible immediately once the model leaned with it,
and invisible for as long as it did not.

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
| `KICK_TICKS` | **86** | **1.43** | `Kick_Front` is 1.433s | **DONE 2026-09-22**, and done twice — the rules moved to the clip, then followed it when it was trimmed. Plays at 1.00x |
| `LEDGE_CLIMB_TICKS` | 18 | 0.30 | `Climb` is **2.80s** | needs **9.3x**; clamped to 2.5x, so the clip is still playing long after she is standing. The likeliest answer is that a 0.30s mantle was never a mantle |
| `BOW_DRAW_TICKS` | 36 | 0.60 | none yet | fine as is — that is a real draw |

**The kick is settled, and it went the other way to everything else here.** Every other fit in this
document stretches a clip to suit the rules. The kick could not: 14 ticks against a 1.633s clip
needs 7.1x, and a kick at seven times speed is not a fast kick, it is a glitch — a kick has to have
a wind-up to read as one at all. So the clip set the pace and `KICK_TICKS` moved to it.

The active window moved with it, and it was **measured rather than guessed**:

```
Clip Kick_Front  1.433s long; the boot connects at 0.700s (tick 42.0 of 86),
                 and the rules' active window is ticks 40..44
```

`MeasureKickClip` poses the model at every keyframe and asks which foot is furthest from the hips —
by REACH, not by the root track, because the kick is the one move whose whole point happens at the
end of a limb while the hips do not move at all (0.000 units of travel across the clip). Both feet
are checked and the furthest wins, since nothing here knows which leg was authored to kick.

The rules cannot read that number — `Stage.h` names no engine type and has never seen a `.glb` — so
the measurement does not *set* the window, it *checks* it. The app logs the strike beside the window
every start and appends **"THE WINDOW DOES NOT COVER THE STRIKE"** when they stop lining up, which
is what a re-export with a different impact frame looks like.

Measured after: the kick plays at **rate 1.00, wanted_rate 1.00** — the first one-shot in this app
to need no stretching at all — and connects, `"Kick connected: 1 props"`.

**What a re-export does and does not fix by itself — and this has now happened for real.**
Trimming frames off the END of the clip does not move the strike, so `KICK_ACTIVE_FROM/TO` stay
right; but `KICK_TICKS` is the one number in this app that cannot re-measure itself, and leaving it
too long makes the Puppet *stretch* the shortened clip to fill the window — a kick in slow motion,
which looks like a rate bug and is not one. So the app prints the answer rather than leaving it to
be noticed. Twelve frames came off the end, and the next start said:

```
[warn] Kick_Front is 86 ticks but KICK_TICKS is 98, so it will play at 0.88x.
       Set KICK_TICKS to 86 in Stage.h.
```

Typed in, and the kick measures **rate 1.00 across 56 ticks of it** again. The window needed no
change at all — tick 42 of 86 instead of tick 42 of 98. Trimming the FRONT would move the strike
too, and the window check catches that separately.

The warning was first confirmed by deliberately mis-setting `KICK_TICKS`, which is why it was
already there to fire when the real trim arrived.

**The cost, which is real and is the next decision.** `f_planted` roots her for the whole of
`kick_ticks`, so a kick is a **1.43-second commitment**, and 0.73s of that is recovery *after*
the boot has landed. That is a heavy, committal move, which may well be what a wall-breaking kick
should be. If it wants to be lighter, the fix is to unroot at `KICK_ACTIVE_TO` and let the recovery
be cancelled by moving — which needs the Puppet to drop the clip at the same moment, or the
animation would be overruling the rules. That is the same cancel-on-move the settle already does.

> While testing this: a kick connected and the crate moved 0.3 units. Not the kick — the crate sat
> at 3.00 with the stack's left edge at 3.70 and the step's face at 5.0, so the whole cluster was
> jammed and the one thing the demo exists to show could not happen. The lone crate moved to 0.60,
> which opens 2.7 units in front of it; it now flies **0.60 → 3.79**. The stack stays put because
> two crates reach 1.65 against the step's 1.80 top, so it is the way up there.

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
| stopping from a run | `Running_ToStop`, fired on the first tick of the deceleration ✓ |
| stopping against a wall | **placeholder** — nothing; the ladder falls to `Idle` |
| pushing a crate | **placeholder** — `Running_Slow` on the spot at the push speed |
| kicking | `Kick_Front` at **1.00x**; `KICK_TICKS` retimed to the clip, twice ✓ — and only on the ground |
| climbing | `Climb`, badly mistimed against `LEDGE_CLIMB_TICKS` (wants 9.3x) ✓ |
| rising | `Jump_ToAir`, at rate 1.0, holding the pose the fall loops ✓ |
| falling | `Falling_Idle`, looped ✓ |
| landing | `Jump_FromAir`, or `FallingIdle_ToLanding` above 25 u/s; each entered at its contact frame ✓ |
| running jump | `Running_Jump`, latched at takeoff, fitted to its climb at 0.85x ✓ |
| hanging | `Hanging_Braced`, looped ✓ |
| on the rope | `Hanging_Rope`, looped, rolled to match the collider she is swinging as ✓ |
| drawing / loosing | `Standing_DrawArrow` exists but nothing selects it — it needs step 2's mask layer |

Nothing the archer can DO falls through to the idle any more, and a sweep over the modes in
`stage_test.cpp` says so rather than one line per mode, so it keeps saying it when a mode is added.
The remaining placeholders are all things she does WHILE doing something else, or things with no
clip authored yet. `LEDGE_CLIMB_TICKS` is the last mistiming; the mask layer is the largest hole.

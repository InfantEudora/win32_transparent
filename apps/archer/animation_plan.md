# Archer Animation Plan

How other engines handle heavy blending and mid-clip changes, what this engine already has, and the
order worth building it in for the archer prototype.

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

## Where the engine is today

The state machine lives in `Object` (`core/Object.h:302-347`, `Object::ApplyAnimation`) and is a
**two-clip crossfade**: one clip fading out, one fading in, mixed by a single factor.

| Capability | State | Where |
|---|---|---|
| Crossfade between two clips | Yes | `Object::TransitionToAnimation`, `Animation::Lerp` |
| Per-transition blend times | Yes, sparse `(from,to)` table with wildcard | `Object::SetBlendTime` / `LookupBlendTime` |
| Root motion, position + yaw, per-axis opt-in | Yes | `Animation::SampleRootMotion`, `LerpRootMotion` |
| Clip-level interruptibility | Yes, one bool for the whole clip | `Animation::interruptible` |
| Reverse / park a clip mid-play | Yes, one signed rate | `Object::SetAnimationRate` |
| Per-bone mask | Yes, but static and un-layered | `ObjectAnimation.cpp:95-100` |
| **Retarget to a third clip mid-blend** | **Refused** | `TransitionToAnimation`, with a note explaining why |
| Parametric blending (blend space) | No | — |
| Phase / foot sync between clips | No | — |
| Additive poses | No | — |
| Time-ranged cancel windows | No | — |

The refusal is the important row. `TransitionToAnimation` will rewind a blend if asked to go back
where it came from, but a request for a genuinely new clip mid-blend is dropped with a warning,
because honouring it would mean either blending three clips or snapping. That is the correct call
for a two-clip crossfade — and it is exactly the wall a fast-paced game hits. The industry's answer
was to stop crossfading clip pairs, not to extend the crossfade.

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

## 3. Additive / aim offsets for the bow pitch

Aiming up and down should not be clips. Unreal's *AimOffset* is a blend space of **additive** poses
(deltas from a reference pose) driven by aim yaw/pitch. Side view means only pitch matters, so this
is a 1D additive — or, to start, a procedural rotation applied to spine and shoulder after the
animation pass. The `RootPose` swing/twist split is the same kind of decomposition, so the shape of
this is already familiar ground in this engine.

## 4. Parametric blending instead of clip-to-clip transitions

For locomotion, discrete states plus transitions is the wrong shape — it is the combinatorial
explosion `docs/animation_state_machines.md` opens with. A **blend space** (Unreal) / **blend tree**
(Unity) / `AnimationNodeBlendSpace1D` (Godot) blends idle/walk/run continuously on a speed
parameter. There is no transition to interrupt, because there is no transition; you move a float.

For a side-view game, **one 1D blend space on signed horizontal speed** (-run … -walk … idle …
walk … run) is remarkably strong, and it makes decelerate-and-reverse read correctly with no turn
clip at all. That single change removes most of the turnaround problem.

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
   turntable.
2. **Signed-speed blend space** (§4), so a reverse is a deceleration through zero rather than a turn
   event at all. Best fit for "fast paced", because there is no event to interrupt.
3. **Authored pivot clips** with extracted yaw root motion — which the twist extraction handles
   natively, and is the nicest-looking option. But they are blocking one-shots, which is the thing
   fighting the pacing. Usually reserved for stop-and-turn-from-a-sprint, gated on speed above a
   threshold.

### One caution on root motion

Driving *locomotion* from root motion tends to feel laggy in a platformer. Root motion is the right
answer for the ledge climbs and pivots — `Stage.h:178` already notes a climb lerp standing in for a
clip it expects to get — but for run and jump, most platformers drive velocity from code and slave
the animation to it through the blend-space parameter. Worth deciding deliberately rather than by
default, because the archer inherits a root-motion locomotion path from the isoanimation work.

---

## Suggested order for the prototype

Chosen so that each step is either additive to what exists or deletes more than it adds, and so
nothing is blocked on the step after it.

| # | Step | Why here |
|---|---|---|
| 1 | Signed-speed 1D blend space for locomotion | Removes most turnaround and most transitions outright; `Animation::Lerp` is the primitive |
| 2 | Upper-body mask layer for bow actions | Per-bone `animation_mask` already supports a crude version; removes most remaining interrupt requests |
| 3 | Additive aim pitch | Small, and the bow needs it regardless of the rest |
| 4 | Inertialization to replace the crossfade | Deletes the four transition states and the mid-blend refusal; do it once 1-3 have shown what transitions actually remain |
| 5 | Cancel windows + input buffer | Tuning work, and it wants real clips and real timings to tune against |

Phase sync (§5) is not in the list because it becomes necessary exactly when step 1 blends two
cycles, and is a call-site change inside that step.

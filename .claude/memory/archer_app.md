---
name: archer-app
description: "apps/archer, a side-view archer platformer prototype - the Stage/view split, the hybrid body, the Puppet animation seam, and the agreed slice order (bow, ledge, props-block, kick, rope-swing and the animation seam done; tightrope and knife parked as nice-to-haves)"
metadata: 
  node_type: memory
  type: project
  originSessionId: 1f02e4da-fa81-4634-9800-ae5b6cfa5ba7
  modified: 2026-09-22T10:17:52.344Z
---

`apps/archer` - a side-view platformer about an archer, 3D assets, Windows, keyboard only.
Started 2026-09-20. The user is making the assets and animations; the first iteration has none,
and everything renders as scaled primitives from `core/Primitives.h`.

**Structure**, following breakout/bomber: `archer/Stage.{h,cpp}` is the RULES and names no engine
type (`make rules` builds it against `stage_test.cpp` alone - 176 checks, ~1 s, no GPU);
`ApplicationArcher` is the view and wiring. 60 TPS.

**The seam, which is the thing to understand.** Simulated by hand in Stage: the archer's own
motion, and the flight of an arrow. Left to reactphysics3d: crates, targets, brick debris, the
rope. The archer is by hand because coyote time, variable jump height and deliberately-worse air
control are *designed* responses a solver cannot express. The arrow is by hand because the aim arc
drawn on screen IS `Stage::PredictArc` running the arrow's own integrator - the rules test asserts
they agree tick for tick, and that promise is the whole argument for not handing arrows to rp3d.

**The archer's rigid body collides with NOTHING** (mask 0): Stage resolves the archer against
everything itself, so the solver must not get a second opinion. Since 2026-09-20 that includes the
props - the user decided crates should block. The app hands Stage each live prop as six plain
numbers before every tick (`RefreshObstacles` -> `AddObstacle`, rebuilt from scratch, never kept in
sync), Stage stops the archer against them exactly as against a wall and reports which was leaned
on, and the app turns that into a shove (`ApplyPushes`, velocity SET not added, so it cannot
accumulate). Standing on a crate falls out of resolving the Y axis too. The body still earns its
keep: the arrow raycast needs something to exclude, and the rope slice takes it over as DYNAMIC.

**Two traps in that obstacle resolution, both measured:** (1) the X pass must run even when the
archer is not moving horizontally, because a crate can come to *them* - when it only ran for a
non-zero step, the Y pass (gravity always runs) resolved the side overlap as a landing and the
archer rode up a stack of crates without jumping, 0.90 -> 1.70 -> 2.50. (2) the Y pass needs the
same feet-were-above guard the one-way platforms use, as belt and braces for multi-obstacle cases.
A rules test that declares a *stationary* box tests neither - the motion is the mechanism.

**Decisions the user made** (2026-09-20, asked up front): hybrid controller (kinematic on foot,
dynamic on rope); bow aiming = hold J to draw, Up/Down tilt, release to loose, angle relative to
facing; level hand-coded in the rules module rather than authored in Blender or parsed from a file;
bow first of the four mechanics.

**Slice status:** base traversal + bow, ledge hang/climb, props-block-you, kick + breakable walls,
the ROPE SWING and step 0 of the ANIMATION are all DONE and verified in-app. The user PARKED the
tightrope and the knife as nice-to-haves on 2026-09-21 - "the core mechanics are in" - and moved
to animation. Keys are J bow / K kick / L knife with E for the rope; the knife mapping exists with
no rules behind it.

**The rope swing** is the ONE place the solver owns the archer. A pendulum is what a constraint
solver is good at and what a hand integrator is bad at - the appeal is that a bad release drops you
and a good one throws you, and none of that survives being scripted. `MODE_ROPE` makes the body
DYNAMIC, gives it a real collision mask (`ARCHER_MASK_ON_ROPE`) since Stage is no longer resolving
anything, and joints it to a link; `Stage::TickArcher` steps aside and the app writes pos/vel back
each tick (`SyncArcherFromRope`). Release reads the solver's velocity out into `stage.vel`, which
is the whole payoff. The rules keep only the DECISIONS - grab reach, `ROPE_MIN_HOLD_TICKS` (the
press that caught it is still down when TickRope first runs), the cooldown, and whether the release
was a jump (adds `ROPE_JUMP_BOOST`).

Rope-building gotchas: links must have `setIsAllowedToSleep(false)` or a hanging rope sleeps on the
first frame and the archer swings into a bar of iron; and links collide with NOTHING, including
each other - jointed neighbours overlap by construction, so a self-colliding chain asks the solver
to hold them together and push them apart at once, and buzzes.

**Technique matters, by design:** release mid-arc over the second gap and you fall in; release at
the FAR EXTREME of the arc (x 41.1, y 6.9) and you land on ground C at x 41.1. That is the skill
element, not a tuning fault - worth knowing before "re-tuning" the anchor at x 36.75.

**The kick** (K) is a separate verb from the walking shove and has to be: a shove is
ARCHER_PUSH_SPEED and moves a crate at walking pace, a kick is KICK_SPEED with lift and punts one
about ten units. Wind-up, a five-tick active window, then a cooldown - and the window CLOSES on the
first connect, or the boot hits the same crate five times. A grounded kick plants the feet AND
freezes the facing (the boot's box is built from `facing`, so turning mid-kick would swing it
through 180 degrees). Air kicks keep their arc.

Two things learned building it: (1) **broken bricks must stop being obstacles** or the rubble pile
is just the wall again - it bulldozed the archer backwards 51.19 -> 54.90 over four kicks and the
hole was never passable; `PropView::f_broken` makes them rubble that still falls and piles but no
longer blocks. (2) a kick frees **the whole wall**, not just the bricks touched, or the rest hangs
in the air over the hole.

**The ledge grab is automatic** - no key. Only `BLOCK_LEDGE` is catchable, so the level states
where you can hang. The rule that makes it work is **grab only while falling**
(`vel.y <= LEDGE_GRAB_MAX_RISE`): the hands cross a lip before the feet clear it, so allowing a
grab while rising would steal every jump you could otherwise have landed. Holding away refuses it;
Space climbs (an uninterruptible tick-counted lerp, shaped so a root-motion clip drops straight
in); S lets go, with a cooldown so the drop cannot re-grab. The archer turns yellow while hanging
because with no animation the colour IS the state readout in a screenshot.

**A per-tick snapshot cannot report whether the sim is PAUSED**, and this bit hard on 2026-09-22.
PublishSnapshot runs at the end of RunSimulationTick, which only runs on a pass that ticks - so
while paused nothing is published and the snapshot keeps reporting the last TICKING pass, on which
it was by construction not paused. `paused` read false for a game that had been sitting still for
minutes, and a stalled tick counter read as a hung physics thread until sim_pause false was tried
on a hunch. archer_state now reads IsPhysicsPaused() live. Breakout's snapshot is the same design
and probably has the same hole.

**Testing it over MCP:** `archer_place` teleports the archer (the level is 84 units with two gaps
- do not fly the approach by script), and `archer_hold` holds any one control for N ticks, which
is the general form that reaches `down`/`action`/`knife` without a tool per key. To catch the high
ledge: place at (43.1, 0.9) then `archer_jump` with run right.

**The animation, from 2026-09-21.** Plan and measurements in `apps/archer/animation_plan.md`; read
it before touching any of this. `apps/archer/Puppet.{h,cpp}` is the SECOND rules module and makes
the same promise `Stage` does - no engine type, built and tested by `make rules`. It owns what
`Stage` deliberately does not: which clip plays, at what rate, and which way the model faces.

`ArcherAnimParams` is the seam. `DescribeArcher(stage,out)` fills it from the rules; the debug
panel and the `archer_anim` MCP tool fill the SAME struct by hand, and `Puppet::Tick` cannot tell
which. That is what lets the animation prototype run with no level, no gravity and no input, which
is what the user asked for. Three sources: `game`, `panel` (sliders), `clip` (one clip on loop,
which is how an export gets checked - every clip in the file, including the ones the game has no
use for). The user re-exports OFTEN and the clip set changes each time: re-measure before trusting
any number here.

**The asset** is a 65-joint Mixamo rig, 0.8911 units tall in bind pose, MEASURED at load and scaled
2.020x to stand ARCHER_MODEL_HEIGHT - never typed in, so a re-export at another size corrects
itself. Clip speeds are measured the same way from each clip's own root track, because the number
that matters is the gap between them and ARCHER_RUN_SPEED. As of the 2026-09-21 evening export the
ladder is Walking 1.58/s -> Running_Slow 2.92/s -> Running_Fast 5.19/s against a game that tops out
at 9.0, so a full sprint stretches the fast run **1.73x** - the morning export had only a walk and
slid 5.7x, which is what that measurement was FOR. `Puppet::Choose` picks the rung needing the
least stretch, judged in RATIO not in units/s, which is deliberately the same search the blend
space (step 1) wants. `Puppet::choice.wanted_rate` against `.rate` reports the remaining slide live.

Clips load with BOTH root-motion extract flags off - the rules own the motion and the animation is
slaved to it, which is what a platformer wants. The two clips the RULES time disagree badly with
their animation and that is a gameplay decision waiting to be made, not a bug: Kick_Front is 1.13s
against KICK_TICKS' 0.23s (wants 4.9x) and Climb is 2.80s against LEDGE_CLIMB_TICKS' 0.30s (wants
9.33x). Both are clamped at PUPPET_ACTION_RATE_MAX and the gap is reported.

**Two silent traps, both now their own memories: [[renderer-skinned-shader-null]] and
[[root-yaw-always-extracted]].** The second turned out to be a CORE bug and was fixed there on
2026-09-22 (Animation::extract_yaw_root_motion) rather than worked around here - the app-side
version could only choose between turning and not turning, never between those and "keep the hip
rotation in the pose", which is what a complex clip needs. The archer keeps the ArcherModel
subclass to consume the turn and an f_turns column that sets the core flag per clip.

**Step 1, the BLEND SPACE, is done (2026-09-22).** Two core additions: Object gained a SUSTAINED
blend (`blend_animation`/`blend_factor`/`blend_phase`/`SetBlendPair`) as distinct from its timed
crossfade - two clips sampled every tick at a weight the caller owns, sharing ONE normalised
playhead advancing at the interpolated duration, with `blend_phase_offset` shifting the follower so
footfalls line up. `SetBlendPair` re-bases the phase onto whichever clip survives when the PAIR
changes, which is what stops a pop at every rung. `Puppet::Choose` now returns two clips and a
weight.

PHASE SYNC NEEDED NO AUTHORING - `MeasureClipPhases` poses the model through each clip and watches
the toe bone's lowest point. Measured 0.81/0.67/0.60 for walk/slow-run/fast-run in-app against
0.83/0.69/0.62 computed straight out of the .glb. Two traps worth remembering: the rate must be
matched against the interpolated STRIDE over the interpolated DURATION (not the lerp of the two
speeds - 4% out here, and it is 4% of the slide the blend exists to remove); and IDLE is not a rung,
because a shared phase would play a 12.3s idle at a 1.0s walk's cycle rate. Worst stretch across the
ladder went from 36% (discrete, at the crossover) to 4.4%.

**THE ARCHER CHANGES SPEED FASTER THAN A CROSSFADE LASTS, and three bugs came out of that one
fact.** `ARCHER_RUN_ACCEL` 90 u/s^2 and `ARCHER_RUN_FRICTION` 120 are 1.5 and 2.0 units PER TICK,
against a ladder 1.58..5.19 wide and a 9-tick fade - so a start or a stop crosses the whole blend
space in two or three ticks and asks for two or three different clips in a row. Fixed 2026-09-22:
(1) core now retargets mid-blend instead of refusing, see [[animation-state-machine-in-object]],
and the archer only records `playing_clip` when the request took; (2) the app's test for using
SetBlendPair is now "does the new pair share a clip with what is ON SCREEN, in EITHER slot", not
"is there a follower" - the old test dropped BOTH ends of the ladder into a crossfade, and past the
fast run that faded backwards into a slow run that was no longer visible; (3) `SetBlendPair` gained
the case where the single playing clip becomes the FOLLOWER (`phase = now - offset`), which is the
top of the ladder on the way back DOWN and had been seeding from the new leader's stale playhead
while the follower carried 83% of the weight. A 0.5 -> 9.0 -> 0.0 sweep in puppet mode now produces
exactly two crossfades, `Idle -> Walking` and `Walking -> Idle`; it was four.

**ROOT-MOTION TRANSLATION WAS NEVER EXTRACTED** until 2026-09-22 - `extract_horizontal_root_motion`
and `extract_vertical_root_motion` sat at their `false` default on every clip while only the yaw was
set. Stage owns her position and writes it every tick, so an un-extracted stride is drawn ON TOP of
it: Walking's hip runs z 0.000 -> 0.756 rig (1.53 world) across one second, then snaps back at the
wrap, and the snap on dropping to Idle reads as the character jumping backwards. Now driven by two
new columns, `f_extract_move` / `f_extract_lift`.

THEY ARE NOT `f_travels`, and assuming they were is the trap: f_travels asks "should the blend space
measure this stride", extraction asks "is the rules layer already walking it". Climb answers no then
yes (a mantle's speed is meaningless, but it really does carry her forward AND up, both extracted);
Twirl answers yes then no (measured and reported, but only the preview plays it, so its 0.879-unit
step back stays as the performance the Blender comparison asked for). Vertical is its own column
because extraction pins the axis to the BIND pose - setting it on a gait does not remove drift, it
flattens the 0.02-unit footfall bob. Verified by reading the hip bone live over MCP (it IS in
`object_list`, as `mixamorig:Hips`): Walking pinned flat on x/z and still bobbing on y, Climb pinned
on all three, Twirl free on all three.

Worth knowing for tuning: `ARCHER_RUN_SPEED` is 9.0 but the fastest clip covers 5.19, so at a full
hold she is always ABOVE the ladder playing Running_Fast alone at rate 1.73. The blend space only
does real work while accelerating through it. A sprint clip, or a lower top speed, is what would
change that - an authoring decision, not a bug.

**Also in the view now:** a parallax backdrop (apps/archer/assets/images/background1.png on one
unlit quad 40 units back, BACKGROUND_FOLLOW is the fraction of the camera's motion it copies)
and mouse-wheel zoom on CAMERA_DISTANCE_MIN..MAX. The zoom places the camera from UpdateView
rather than waiting for the tick, so it still works while the simulation is paused - which is
exactly when you want to look closely at an animation.

**The rest of that hunt:** Also found: the engine skins with
three bone influences, not four (29% of this mesh's vertices have a fourth; the first three average
0.98, so it is a few percent of shrink rather than a problem); and the export's material arrives at
metallic 0.6, which renders BLACK in this app because the skybox is off - BuildArcherModel turns it
down to 0.10 before the renderer takes its copies, and does it there rather than in Blender because
a re-export puts the exporter's number back.

Found on the way: [[addphysics-gravity-off]] and [[renderer-skinned-shader-null]]. Also added `body` to
`PhysicsWorld::RaycastHit` in core (additive, default NULL) so an arrow can tell what it hit.
Related: [[per-app-build-layout]], [[shell-heredoc-limit]], [[running-app-is-user-driven]].

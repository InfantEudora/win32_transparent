---
name: archer-app
description: "apps/archer, a side-view archer platformer prototype - the Stage/view split, the hybrid body, and the agreed slice order (bow, ledge and props-block done; kick, rope, knife open)"
metadata: 
  node_type: memory
  type: project
  originSessionId: 1f02e4da-fa81-4634-9800-ae5b6cfa5ba7
  modified: 2026-09-20T19:06:05.569Z
---

`apps/archer` - a side-view platformer about an archer, 3D assets, Windows, keyboard only.
Started 2026-09-20. The user is making the assets and animations; the first iteration has none,
and everything renders as scaled primitives from `core/Primitives.h`.

**Structure**, following breakout/bomber: `archer/Stage.{h,cpp}` is the RULES and names no engine
type (`make rules` builds it against `stage_test.cpp` alone - 91 checks, ~1 s, no GPU);
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

**Slice status:** base traversal + bow, ledge hang/climb, props-block-you, and kick + breakable
walls are all DONE and verified in-app. Open, in the user's stated order: rope (balance and swing),
then knife. Keys are J bow / K kick / L knife, in a row; the knife mapping exists with no rules
behind it. The rope anchor waits over the second gap at x 36.75.

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

**Testing it over MCP:** `archer_place` teleports the archer (the level is 84 units with two gaps
- do not fly the approach by script), and `archer_hold` holds any one control for N ticks, which
is the general form that reaches `down`/`action`/`knife` without a tool per key. To catch the high
ledge: place at (43.1, 0.9) then `archer_jump` with run right.

Found on the way: [[addphysics-gravity-off]]. Also added `body` to
`PhysicsWorld::RaycastHit` in core (additive, default NULL) so an arrow can tell what it hit.
Related: [[per-app-build-layout]], [[shell-heredoc-limit]], [[running-app-is-user-driven]].

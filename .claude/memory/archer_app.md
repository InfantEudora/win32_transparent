---
name: archer-app
description: "apps/archer, a side-view archer platformer prototype - the Stage/view split, the hybrid body, and the agreed slice order (bow done; ledge, kick, rope, knife open)"
metadata: 
  node_type: memory
  type: project
  originSessionId: 1f02e4da-fa81-4634-9800-ae5b6cfa5ba7
  modified: 2026-09-20T16:22:56.072Z
---

`apps/archer` - a side-view platformer about an archer, 3D assets, Windows, keyboard only.
Started 2026-09-20. The user is making the assets and animations; the first iteration has none,
and everything renders as scaled primitives from `core/Primitives.h`.

**Structure**, following breakout/bomber: `archer/Stage.{h,cpp}` is the RULES and names no engine
type (`make rules` builds it against `stage_test.cpp` alone - 48 checks, ~1 s, no GPU);
`ApplicationArcher` is the view and wiring. 60 TPS.

**The seam, which is the thing to understand.** Simulated by hand in Stage: the archer's own
motion, and the flight of an arrow. Left to reactphysics3d: crates, targets, brick debris, the
rope. The archer is by hand because coyote time, variable jump height and deliberately-worse air
control are *designed* responses a solver cannot express. The arrow is by hand because the aim arc
drawn on screen IS `Stage::PredictArc` running the arrow's own integrator - the rules test asserts
they agree tick for tick, and that promise is the whole argument for not handing arrows to rp3d.

**The archer's rigid body collides with NOTHING** (mask 0). Stage resolves the archer against the
level, so the solver must not get a second opinion; and since Stage does not know props exist, the
archer walks *through* a crate rather than being stopped by it, so solver-driven pushing would mean
resolving a deep overlap against infinite mass every tick. Props are shoved by `KickProps()`
instead, edge-triggered and ground-only, which SETS a bounded velocity rather than adding force.
The body still earns its keep: the arrow raycast needs something to exclude, and the rope slice
takes it over as a DYNAMIC body.

**Decisions the user made** (2026-09-20, asked up front): hybrid controller (kinematic on foot,
dynamic on rope); bow aiming = hold J to draw, Up/Down tilt, release to loose, angle relative to
facing; level hand-coded in the rules module rather than authored in Blender or parsed from a file;
bow first of the four mechanics.

**Slice status:** base traversal + bow DONE and verified in-app. Open, in the user's stated order:
ledge jump/hang/climb, kick + breakable brick walls, rope (balance and swing), knife. The level
already contains tagged geometry for each - a grabbable-only ledge at 4.2 (feet reach 3.20, hands
5.00, so it is unreachable until hanging exists), a `BLOCK_BREAKABLE` cracked wall, a 3x7 static
brick wall, and a rope anchor over the second gap.

Found on the way: [[addphysics-gravity-off]]. Also added `body` to
`PhysicsWorld::RaycastHit` in core (additive, default NULL) so an arrow can tell what it hit.
Related: [[per-app-build-layout]], [[shell-heredoc-limit]], [[running-app-is-user-driven]].

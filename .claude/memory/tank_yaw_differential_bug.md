---
name: tank-yaw-differential-bug
description: "OPEN BUG: a tracked vehicle on the rp3d VehicleConstraint will not yaw when both tracks are driven in the SAME direction at different magnitudes - only opposed commands turn it. What is already ruled out by measurement, and the agreed plan to isolate it in rp3d's own testbed"
metadata:
  node_type: memory
  type: project
  modified: 2026-09-06T00:00:00.000Z
---

**Symptom (2026-09-06, still open).** `TankCharacter` will not yaw when both tracks are commanded in the SAME direction at different magnitudes, no matter how large the difference. Measured with a clean scripted probe, holding each case for 80 ticks from a settled reset:

| left / right command | forward speed | yaw rate |
|---|---|---|
| +1.00 / +1.00 | 0.933 m/s | 0.000 |
| +1.00 / +0.80 | 0.841 m/s | 0.000 |
| +0.75 / +0.25 | 0.491 m/s | 0.000 |
| +1.00 /  0.00 | 0.709 m/s | 0.000 |
| +1.00 / −1.00 | 0.000 m/s | −1.420 |

Speed clearly responds to the commands, so the two sides ARE receiving different forces - but the difference produces no yaw at all. Only OPPOSED commands turn the hull. That is the whole bug: a real tank steers by driving one track harder than the other, and this one can only pivot.

**Ruled out by measurement** (each with a clean probe - see the gotchas below, which invalidated two earlier attempts):
- **Not the input path.** `steer_input` arrives exactly as sent; confirmed with `throttle_input`/`steer_input`/`brake_input` fields added to `tank_telemetry` for this purpose.
- **Not `lateral_friction`.** 0.5 / 0.2 / 0.05 give byte-identical results. It is not a Coulomb yaw threshold being missed.
- **Not rolling resistance / track drag.** 0 / 150 / 800 / 2000 N all identical.
- **Not the per-side track-speed averaging** in `TankCharacter::UpdatePhysicsState` - that only rewrites wheels which are NOT grounded.
- **Not the lever mixing**, which was separately wrong and is now fixed (`left = throttle + steer`, `right = throttle - steer`, clamped per side).

**Best remaining lead:** how the constraint's longitudinal tire force acquires a yaw lever arm about the hull's vertical axis. A per-wheel force applied at the contact point, offset in local X, must produce a yaw torque; something is either applying it through the centre of mass or cancelling it.

**Agreed plan (user, 2026-09-06): isolate it in reactphysics3d's own testbed rather than in the game.** The fork already has a Vehicle scene (scenes 18 Spring / 19 Vehicle / 20 Upright, see [[rp3d-vehicle-constraint-plan]]), built from `C:/code/reactphysics3d` with `-DRP3D_COMPILE_TESTBED=ON` via CMake+Ninja. A minimal scene - one box body, four or six wheels, one track commanded harder than the other - takes the game's 16 wheels, terrain, suspension tuning and input plumbing out of the picture entirely, and makes it obvious whether the constraint yaws at all under a pure drive differential. If it does, the bug is in `TankCharacter`; if it doesn't, it is in `VehicleConstraint` itself and fixable in the fork.

**Related open item from the same session:** making `GovernedDriveForce` speed-proportional (so the lever controls magnitude, not just direction - see [[deterministic-sim-plan]]) also cost the half-magnitude pivot: `+0.5/−0.5` now yaws 0 where it used to work. Very likely the same underlying threshold, so the testbed scene should cover opposed-at-partial-magnitude too.

**Probe gotchas that produced two wrong conclusions before this was pinned down - do not repeat:**
- A `tank_steer`/`tank_drive` hold lasts `duration_ms` worth of TICKS. A hold longer than the steps taken stays active into the NEXT test case and silently contaminates it. Always `tank_drive direction=stop` then `tank_reset` between cases, and size the duration to the steps about to be run.
- `vehicle_mcp`'s `App` has no `stop()`, so a probe script leaves `wind.exe` running. The next build then fails with `cannot open output file wind.exe: Permission denied`. Kill strays before building.

**In-game test rig added 2026-09-07.** `TankCharacter::direct_track_control` (checkbox in the Tank debug UI) bypasses the throttle/steer mix entirely: left stick Y commands the left track, right stick Y the right, straight into the same signed per-side commands the mix would have produced. Arrow keys work too (up/down both tracks, left/right hold one back). This is the manual counterpart to the scripted probe above and sidesteps its contamination gotchas. Note MCP `tank_drive`/`tank_steer` do NOT work while it is on - they feed `throttle_input`/`steer_input`, which the direct path ignores.

**RE-MEASURED 2026-09-07 against the new tire model - MOSTLY FIXED.** Same probe, via the new
`tank_track_drive` MCP tool (paused, reset + 40 settle steps + 80 driven steps per case), yaw taken
as the angular velocity projected on the hull's own up:

| left / right | forward speed | yaw rate (was) | yaw rate (now) |
|---|---|---|---|
| +1.00 / +1.00 | 0.933 | 0.000 | -0.0000 |
| +1.00 / +0.80 | 0.833 | 0.000 | **-0.0367** |
| +0.75 / +0.25 | 0.481 | 0.000 | **-0.0393** |
| +1.00 /  0.00 | 0.690 | 0.000 | **-0.0758** |
| +1.00 / -1.00 | -0.013 | -1.420 | -1.4876 |
| +0.50 / -0.50 | -0.008 | 0.000 | -0.0004 |

A same-direction differential now yaws at all, and monotonically in the size of the difference -
that part of the bug is gone, fixed by `corneringStiffness` in the rp3d fork's new tire model, not
by anything in this repo. TWO THINGS REMAIN:
- **The yaw is weak.** One track fully stopped gives 0.076 rad/s (~4.4 deg/s) against the pivot's
  1.49 - a factor of 20. A real tank with one track held swings round it noticeably. Worth raising
  `corneringStiffness` (default 7.0) and re-running before concluding anything about the geometry.
- **Opposed-at-partial-magnitude is still dead:** +0.50/-0.50 yaws -0.0004, i.e. nothing, while
  +1.00/-1.00 works. This is the separate `GovernedDriveForce` regression already noted below and
  it did NOT come back with the new tire model.

**Superseded note (kept for context).** The rp3d fork's `VehicleConstraint` gained `corneringStiffness` (slip-angle-based lateral force, default 7.0) in the Sep 7 library build. Its own header says a skid-steered vehicle can now hold a steady arc on a torque difference alone - which is exactly the missing behaviour - so re-measure the table above before doing any more work on this. The bug may already be gone.

Related: [[rp3d-vehicle-constraint-plan]], [[rp3d-local-fork-hinge-motor-patch]], [[deterministic-sim-plan]].

---
name: tank-suspension-upright-torque-todo
description: "Tank roll-over root causes all found and fixed (CoM lever arms, damping stability, Coulomb friction) - do NOT add the upright-stabilizing torque backstop that was once planned"
metadata:
  type: project
  originSessionId: 7f3cf564-76f6-4dc6-a05b-331303a726eb
  modified: 2026-08-30T21:08:53.144Z
---

**Superseded: the planned "add an explicit upright-stabilizing torque backstop" workaround is NOT needed and must not be added.** The roll-over it was meant to paper over had three specific causes, all found and fixed on 2026-08-30 (branch `tank-physics-suspension-fixes`, commits `3aeb5ab` and `0758973`).

**1. Lever arms measured from the body origin, not the centre of mass.** `TankCharacter::UpdatePhysicsState` used `r = mount_world - body_world_pos`. rp3d's `getTransform()` returns the body's **transform origin**, which is not its centre of mass; the hull's box collider is centred well above the hull origin, so measured live they are 0.28m apart (`center_of_mass_local.y = 0.4012` vs wheels at `y = 0.1225`). True `r.y = -0.279` where the code used `+0.1225` — wrong sign, 2.3x magnitude. Only the *lateral* component of a contact's point velocity depends on `r.y` (`-w*r.y`), so springs behaved and it hid there, while `lateral_friction` pumped roll instead of damping it. Fix: `com_world = body_world_pos + rotation * physics->GetCenterofMass()`.

**Generalise this:** any code here building a lever arm from `Physics::GetBodyWorldPosition()` has the same latent bug whenever a body's colliders aren't centred on its origin. `Physics::GetCenterofMass()` returns **local**-space, so rotate it into world space first.

**2. Velocity-damping coefficients past the explicit-integration limit.** `-c*v` integrated explicitly gives `v_next = v*(1 - c*dt/m)`: stable only while `c*dt/m < 2`, ring-free while `< 1`. Every grounded wheel adds its own `c` to the same hull. Crucially the bound differs per mode: suspension force is vertical with wheels spread horizontally, so it damps a **linear** mode bounded by MASS (`c_per_wheel < m/(N*dt)` = `82/(10*0.02)` ≈ 410); lateral force is horizontal with wheels 0.279m *below* the CoM, so it feeds a **roll** mode bounded by roll INERTIA, which is far tighter. `suspension_damping` 900 → 400, `lateral_friction` 4000 → 400 → 120. Real mass is 82 kg not the 100 kg `ApplicationTank::Init` targets (density from full hull volume, applied to a vertically trimmed box).

**3. Friction not coupled to normal load.** Each contact's thrust/grip/lateral forces now share one Coulomb budget `friction_coefficient * spring_force` via a friction circle (combined demand, not per-axis). Before, a rebounding wheel with `spring_force = 0` still applied full lateral force — self-reinforcing, since a roll unloads exactly the wheels still pushing hardest against being reloaded.

Also: `max_roll_speed` now projects yaw out before clamping — it was silently governing steering rate (pinned at exactly 3.000 rad/s during a pivot while real roll was ~0.2).

**Verified end state:** 8 s at rest with `|angvel| = 0.00000` and total spring force 804.5 N against 804.5 N of weight; pivot holds a steady 3.2 rad/s yaw self-limited by scrub; 3.05 m in 3 s driving over terrain. All three clamps are dead code in normal play — treat any future engagement as an alarm that something upstream regressed, never as the fix.

**Method that actually worked, and the lesson:** every earlier fix was a clamp stacked on the previous one, and the tank still flipped. What broke it open was building the per-wheel telemetry FIRST (`tank_telemetry` now returns a `wheels` array with grounded/compression/compression_rate/point_speed/spring/drive/longitudinal/lateral force each, plus `center_of_mass_*`, `wheels_grounded`, `point_speed_clamped` and a `tuning` echo) and then reading tick-by-tick traces. Both remaining bugs were obvious within one dump each — e.g. a contact compressed 0.0006m showing a 3.6 N spring term beside an 1800 N damping term. Reach for that instrument before theorising.

**Known open items:** (a) friction is still explicit/velocity-proportional; solving it implicitly (force that exactly cancels slip over one timestep, then clipped to the budget) would be unconditionally stable and remove the tuning cliff, but needs the real `dt` plumbed in (hardcoded 0.02 at the top of `UpdatePhysicsState`). (b) `Scene::UpdatePhysics` decrements `pending_physics_steps` *before* running the tick, so `tank_step` can return mid-tick — visible as duplicate rows in a trace. (c) `tank_drive`/`tank_steer` latch on `GetTickCount64()` wall-clock while `tank_step` advances simulated ticks, so holds don't work under single-stepping; re-assert input per tick, or unpause. (d) `Scene::UpdatePhysics` steps the world BEFORE `UpdatePhysicsState`, so forces land one tick late.

**Context:** [[mcp_native_tools_setup]] has the MCP server background. Note `make` compares mtimes at second granularity here — a source edited in the same second as its `.o` gets silently skipped; `rm` the object to force a rebuild if a change seems not to take effect ([[build_toolchain_location]]).

---
name: pinball_stage0_review
description: "Pinball (apps/pinball, \"Orbit Outpost\") - stage 0 reviewed/re-laid and stage 1 (ball, flippers, plunger, colliders) built 2026-09-13; Table.h is the machine, tools/pinball_plan.py and pinball_run are the acceptance tests; the rp3d traps that cost time"
metadata: 
  node_type: memory
  type: project
  originSessionId: 3a7bcb0c-73e8-48d0-b66a-aa1f9d113ca8
  modified: 2026-09-13T14:18:22.219Z
---

`apps/pinball` is a pinball machine on the engine. Stage 0 (static layout) was built by another
agent (commit `d59ab93`), reviewed and re-laid by me on 2026-09-13; stage 1 (physics: ball,
flippers, plunger, every static collider, tunnel guard, MCP tools) built the same day. Findings
and rationale: `docs/pinball_findings.md` (§1-4 the review, §5 stage 1); the layout and every
physics number: `apps/pinball/Table.h`; mechanisms: `apps/pinball/Mechanisms.h`; the design:
`apps/pinball/pinball_design.md` (its §1.5 numbers are the FIRST draft, kept for reasoning).

**Why:** the first layout looked fine and did not work - a ball from the plunger could not reach
the play area, the inlanes were under a ball wide, the ramps had 0.18 between their rails. All
invisible in the coordinates. Stage 1 then found the faults only a moving ball shows: the orbit
needed an outer guide, ramp sides must reach the deck, the upper flipper's pivot must sit off the
lane's centre.

**How to apply:**
- `Table.h` is the single source. After moving ANYTHING: `python tools/pinball_plan.py` with the
  app running (reads `pinball_layout`'s `plan`; must say "clear"), then a paused
  `pinball_plunger` + `pinball_run` trace to see the ball actually go round. Do not resurrect a
  copy-the-numbers checker. Clear widths = centre-to-centre minus half of each wall.
- Scripted physics tests: `sim_pause`, `pinball_place_ball`, `pinball_flipper`/`pinball_plunger`
  (HoldKey; return at once while paused), `pinball_run {ticks, sample}` -> path + events. The
  `escapes` counter is the tunnelling detector and reads 0 across 90 u/s wall shots.
- rp3d traps, each documented at its define in Table.h: **twist friction** (torque about the
  contact normal, bound mu*N with no lever arm) freezes a small ball rolling along a wall ->
  rails have friction 0 (friction mixes as a geometric mean, so 0 wins); **restitution mixes as
  MAX** -> ball 0.12, lively surfaces say their own; a **hinge motor's torque cap is per tick**,
  so a light bat can't hit hard -> flipper mass 2, torque 2000; joints hang off a static anchor
  Object with no collider; `setIsAllowedToSleep(false)` on every driven body.
- `toradians()`/`todegrees()` in core/type_helpers.h now parenthesise their argument (my fix); an
  expression argument used to be silently wrong (a 64 deg sweep became 101 rad).
- Interior rail colliders are at visual thickness (0.14); the 0.6 rule is cabinet-only; the
  tunnel guard (raycast above 1 radius/tick) is what protects them.
- Parts: `tools/pinball_parts_blender.py` (headless Blender 4.5) -> `assets/meshes/parts.glb`,
  loaded per part with primitive fallbacks; engine (x,y,z) is Blender (x,-z,y). Mirrored flipper
  = same +X bat rotated by 180 - angle.
- Trap: with the mingw PATH set, `python` is MSYS's (no PIL) - run tools with the Windows one
  (also in CLAUDE.md).
- Next: stage 2 (switches on slings/pops/targets, drain as trigger, switch log), stage 3 (habitrail
  wires as capsule chains, saucers/subway). Related: [[shared_build_output_coordination]],
  [[per_app_build_layout]], [[rp3d_local_fork_hinge_motor_patch]].

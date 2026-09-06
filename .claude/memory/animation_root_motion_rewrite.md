---
name: animation-root-motion-rewrite
description: "Root-motion + animation-graph rewrite in core/ObjectAnimation.*/PlayerCharacter.cpp - status, per-clip flags, and the box-climb feature it unblocked"
metadata: 
  node_type: memory
  type: project
  originSessionId: 6178d8ef-0231-44b1-9b33-dfc07a6bd857
  modified: 2026-09-05T15:56:24.173Z
---

Rewrote the character animation system (`core/ObjectAnimation.h/.cpp`, `core/Object.h/.cpp`, `core/skeleton/PlayerCharacter.cpp`, `ApplicationIsoAnimation.cpp` `Init()`) to fix root-motion bugs (e.g. idle→hanging drifting in all 3 axes) and remove the old `AnimationGraph`/`AnimationTransition` class hierarchy, which had become a non-gatekeeping (auto-created edges) wrapper around a blend-time table. Confirmed compiling and working correctly against real content (`RunupClimbing` run-up-and-climb clip verified to match the Blender reference).

**New model**: `Animation` gets `extract_horizontal_root_motion` / `extract_vertical_root_motion` (both default `false`, per-axis opt-in — replaces the old single `modifies_root_object` bool), `interruptible`, `auto_continue_to` (replaces graph auto-continue edges), and `root_track`/`SetRootBone()`. Rotation is always split via swing-twist decomposition about +Y (twist = facing/yaw, extracted to world; swing = lean/tilt, stays on the bone) regardless of the position flags — see `docs/animation_root_motion.md` for the design writeup. `Object` gets `transition_to` + a sparse `animation_blend_overrides` table replacing `animation_graph`.

**Why:** [[rp3d_vehicle_constraint_plan]] established the pattern of driving gameplay from extracted animation deltas (root motion → capsule velocity) for vehicles; the same approach is being extended to the ApplicationIsoAnimation character testbed, motivated by wanting a capsule-collider-driven locomotion controller and eventually ragdoll physics for the character.

**Non-obvious gotcha found while wiring up new clips**: rotation (yaw) extraction is unconditional, but position extraction is opt-in per axis. If a clip needs `extract_horizontal_root_motion` but it isn't enabled yet, the character's world rotation still gets the extracted yaw while translation stays read in the (now-rotating) bone-local frame — this produces a "spiraling away from the run-up direction" artifact that looks like a Blender/Mixamo authoring bug but is actually just a missing flag on the new clip. Always enable both `extract_horizontal_root_motion` and `extract_vertical_root_motion` for any new locomotion/climb clip up front, not just horizontal, or expect this exact symptom.

**How to apply**: When adding a new animation clip that should move the character (running, climbing, jumping), set the appropriate `extract_horizontal_root_motion`/`extract_vertical_root_motion` flags on it in `ApplicationIsoAnimation.cpp`'s `Init()` before testing — most existing clips (`Idle`, `Boxing`, etc.) intentionally leave both `false` (cosmetic bone-local motion only). The old `target_y_location` lerp-to-height snap in `PlayerCharacter::ApplyAnimation` has been removed (it fought real vertical root motion once `extract_vertical_root_motion` was enabled).

**In progress** (from `ApplicationAnimation Todo.md`): character climbing onto a box (`RunupClimbing`/`MidJumpToHang`/`StandToFreeHang` animations), plus a hands/feet-only preview skeleton (already exists as separate `hands`/`feet` `PlayerCharacter` instances in `ApplicationIsoAnimation.cpp`) that needs extending to visualize where hands/feet land at the end of an animation cycle, to help pick animations/timing for landing on a specific spot. Next planned step once that's in place: motion warping (scale the extracted root-motion delta against a target transform, e.g. the box edge) so the character doesn't need to stand at an exact authored distance to climb — discussed as the standard technique (Unreal's Motion Warping plugin is the reference implementation), building directly on the `SampleRootMotion`/`LerpRootMotion` delta-extraction API.

Longer-term direction discussed: add a capsule collider to drive locomotion from these extracted deltas (queried against physics for collision), and eventually a passive/active ragdoll built from the same rp3d rigid-body + joint machinery as the vehicle constraint work, using swing-twist cone-twist joints (a PR at tomazos/reactphysics3d fork, not yet merged) for realistic shoulder/hip limits.

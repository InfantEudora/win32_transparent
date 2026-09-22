---
name: archer-terrain-plan
description: BUILT 2026-09-22 - marching-cubes terrain for apps/archer; SDF field built FROM the StageBlock blockout so colliders never change; core/MarchingCubes + apps/archer/Terrain, plan at apps/archer/terrain_plan.md
metadata:
  type: project
---

Steps 1-5 of `apps/archer/terrain_plan.md` are BUILT and measured as of 2026-09-22. Code is
`core/MarchingCubes.{h,cpp}` (field to mesh, generic) and `apps/archer/Terrain.{h,cpp}` (blocks to
field). Test bay is four 7-unit variants at x -40..-12, guarded by `ARCHER_TEST_BAY` in Stage.h.
F2 shows the blockout under the terrain.

The blockout stays the collision. Only `BLOCK_SOLID` melts. Three gotchas worth not rediscovering,
all found by measurement rather than by reading:

- **Round OUTWARD, not inward.** `sdBox(p, (hw, hh - r, depth)) - r`, NOT the textbook
  `(hw - r, hh - r, depth - r)`. The textbook form keeps the footprint and rounds inward, so the
  last r of every ledge sags below the collider and the archer floats exactly where a jump is
  tightest. Rounding and smooth-union push in OPPOSITE directions: rounding shrinks (dips),
  smooth-union grows (rises). Measured worst_dip 0.0000 on all four variants.
- **Any field sampled for its gradient must be continuous in every axis.** The noise attenuation
  first skipped a block when outside its footprint, which made the field jump by a whole noise_amp
  across that plane and produced garbage normals there. Marching cubes reads normals off the
  gradient, so a C0 break is visible geometry, not a rounding error.
- **`NewGame` runs on the PHYSICS thread**, so it must never remesh (`SetMeshData` calls GL). It
  does not need to - the terrain is a pure function of the blocks - but it must re-hide the fresh
  block objects `BuildBlocks` just made. See `ApplyBlockoutVisibility`.

Also: the test bay must be `BLOCK_SOLID` only, or `stage_test.cpp`'s `HighLedge()` hijacks the
hang-slice assertions onto a piece of scenery. `make rules` stays green (187 checks).

**Why:** the usual marching-cubes objection is that you cannot collide against the result;
deriving the field from the boxes removes the objection instead of working around it.

**How to apply:** mesher changes go in core and are verified by `MarchingCubesSelfTest` (closed-
surface check over an analytic sphere - a wrong table row cannot survive it). Field changes go in
Terrain.cpp and are verified by `TerrainStats::worst_dip`, which must stay 0. Related:
[[archer-app]], [[engine-forward-is-minus-z]], [[comment-density-ceiling]].

---
name: archer-terrain-plan
description: AGREED 2026-09-22 - marching-cubes terrain for apps/archer; the SDF field is built FROM the StageBlock blockout so colliders never change; plan at apps/archer/terrain_plan.md
metadata:
  type: project
---

Agreed direction for the archer level visuals, 2026-09-22. Full plan in
`apps/archer/terrain_plan.md`; this is the part that is a decision rather than a document.

The blockout stays the collision. The marching-cubes density field is an SDF union of the same
`StageBlock` AABBs, so the mesh is a pure function of the blockout and the two cannot drift.
`Stage.h` is not touched. Only `BLOCK_SOLID` melts into terrain - LEDGE, PLATFORM and BREAKABLE
keep their colour-coded boxes, because this app's stated rule is that colour means a rule, and
because a breakable inside one mesh would force a mid-game remesh.

Two constraints found by measurement that are easy to violate later:

- The iso-surface must meet `block.Top()` EXACTLY (hard `max` against an unsmoothed top plane).
  Smooth-min pulls convex corners inward and the archer then stands 0.2 units in the air -
  invisible in a screenshot, obvious under the feet.
- The test bay left of the start must be `BLOCK_SOLID` only. `stage_test.cpp`'s reach loop skips
  SOLID and BREAKABLE, but `HighLedge()` takes the FIRST `BLOCK_LEDGE` above feet reach, so a
  stray test ledge silently hijacks the hang-slice assertions.

**Why:** the usual marching-cubes objection here is that you cannot collide against the result;
deriving the field from the boxes removes the objection instead of working around it.

**How to apply:** new code goes in `core/MarchingCubes.{h,cpp}` (field to mesh, generic, beside
[[per-app-build-layout]]'s core rules) and `apps/archer/Terrain.{h,cpp}` (blocks to field,
app-specific). Mesh upload is render-thread only - `Init` or `PreRender`, never
`RunSimulationTick`. Related: [[comment-density-ceiling]], [[archer-app]].

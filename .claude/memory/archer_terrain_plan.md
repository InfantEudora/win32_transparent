---
name: archer-terrain-plan
description: "BUILT 2026-09-22 - marching-cubes terrain for apps/archer; SDF field built FROM the StageBlock blockout so colliders never change; core/MarchingCubes + apps/archer/Terrain, plan at apps/archer/docs/terrain_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 39fa8ccf-f5eb-4248-82ec-ef317508c311
  modified: 2026-09-24T09:51:32.532Z
---

Steps 1-5 of `apps/archer/docs/terrain_plan.md` are BUILT and measured as of 2026-09-22. Code is
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

2026-09-24: the four comparison bays became TWO (ground, and an island above
`ARCHER_TEST_BAY_SPLIT_Y`), selected by `TerrainRegion` (block centre in x AND y), all on default
`TerrainParams`. Every `block_N` is now a child of an identity-transform `blockout` object - core
now allows a body under an all-identity parent chain (`IsIdentityChain` in Object.cpp). The panel's
"Regenerate terrain" / MCP `archer_terrain_regenerate` reads block centre+size back off the objects
(editor moves), calls `Stage::KeepBlockLayout` so restarts keep them (in memory only - not saved to
disk), and remeshes in `PreRender`. Gotcha found: `Object::Hide()` DEACTIVATES the body -
`ApplyBlockoutVisibility` used it and switched off every melted box's collider; use SetVisibility.

Also: the test bay must be `BLOCK_SOLID` only, or `stage_test.cpp`'s `HighLedge()` hijacks the
hang-slice assertions onto a piece of scenery. `make rules` stays green (187 checks).

**Why:** the usual marching-cubes objection is that you cannot collide against the result;
deriving the field from the boxes removes the objection instead of working around it.

**How to apply:** mesher changes go in core and are verified by `MarchingCubesSelfTest` (closed-
surface check over an analytic sphere - a wrong table row cannot survive it). Field changes go in
Terrain.cpp and are verified by `TerrainStats::worst_dip`, which must stay 0. Related:
[[archer-app]], [[engine-forward-is-minus-z]], [[comment-density-ceiling]].

**2026-09-26: the bay is a level now, and blocks have a shape through the slab.** The four test
variants were replaced by a real layout (mound, hill, three stepping stones up to the island, two
unreachable floaters; stage_test's TestBayClimb plays the climb and searches every jump). Each
block is body + grass cap (same pinned top, overhanging lip, drips) + belly if floating, at its
own `StageBlock::z`/`depth`; grass = vertices the cap owns. See terrain_plan.md section 9.
Gotcha: a block's cap and its own body must be a HARD min - smooth-unioning two surfaces that
coincide on the top face lifts the whole top by k/4. Plants now grow on the terrain too.

**2026-09-26 back wall + rocks:** Backdrop.{h,cpp} = a tall teal-grey CAVE back wall (not a bank - the
user's mock set that) behind the ground bay's floor: noise ridge line, forward ridges, pines on top
(children of terrain_back_<bay>). Boulders.{h,cpp} = rocks at inside corners only, big pushed to the back.
Both pure, in make rules. Traps: weak multiply-xor hash gave a plateau (use lowbias32); BuildTerrain runs
before BuildArcherModel so anything at model_scale placed there is half size. terrain_plan.md 10-11.

---
name: vine-rope-system
description: "Vines and the swinging rope share one base - core/Spline (centripetal CR + arc length + rotation-minimising frames) and core/SplineDeform (tile an authored mesh along it); steps 1-2 (static decorative vines) BUILT 2026-09-24, plan in apps/archer/vine_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 95091de0-2e6d-47f4-a5dd-59fe26a32d0c
  modified: 2026-09-24T16:09:17.929Z
---

Agreed with the user 2026-09-24: a rope and a vine are one system - a curve with three operations
on it (Deform a tile along it, Derive new curves like a helix wrap, Scatter instances). Vines are
placed EXPLICITLY for now (generated along terrain features later); most decorative ones static,
a few may sway; baking all static vines into one mesh is the later cost optimisation.
Plan and ideas (grip-at-a-point, rope arrow, cutting, drapes) in `apps/archer/vine_plan.md`.

BUILT (steps 1-2): `core/Spline`, `core/SplineDeform` (tile along glTF +Z, matching end rings,
length measured, copies stretched to fit), `apps/archer/Vine.{h,cpp}` (engine-free, in `make
rules`), `PlaceHash.h` (Hash01 moved out of Foliage.cpp), `ApplicationArcher::BuildVines` (built
once at Init, render thread). Tests: `tools/spline_test.cpp` (standalone g++ line in its header)
and 22 vine checks in `make rules`. Three vines on the main level, verified in-app from
placeholders.

The user is splitting the Blender vine into pieces: nodes `vine_trunk`, `vine_leaf_1`,
`vine_leaf_2` in archer.glb, each falling back to Vine.cpp's placeholder when missing. Leaf
convention: origin at stem, blade +Z, face +Y. The app warns if the trunk tile is not longest
along +Z.

STEP 3 DONE 2026-09-24 differently: the user MODELLED the wrap (`vine_curl`, strands + a curl round
one trunk tile), laid as an overlay at the TRUNK's period via SplineDeformParams::tile_start /
tile_length - its own bounds overhang the period (slanted strands) and would drift 2.6%. No
placeholder for it. The step vine now sits in FRONT of the lane (z +1.1). All pieces load from the
glb and look right in-app.

STEP 4 DONE 2026-09-24: the swinging rope is ONE skinned mesh - RopeMesh.{h,cpp} (engine-free,
quadratic B-spline weights over link centres, no weight painting), a plain core Skeleton + one
Bone per link built ONCE per level at Init/BuildExtraLevel (render thread, never from BuildRope,
which NewGame runs on the physics thread), UpdateRopeSkin copies link poses pre-step (one tick
behind the solver ON PURPOSE, same instant as her). Rope parts rope_segment/ring/collar/tassel;
rope_ring has an unapplied node scale 2, read via the new GLTFLoader::GetNodeScale. The user built
a separate Rope scene (scene_set "Rope" over MCP) for the rope and its animation.

ROPE MODEL PIVOT FIXED 2026-09-24: the model was rolled about its FEET while the collider rolls
about its centre (error grew with swing, 1.3 at 90 deg). Debug view: MCP `archer_debug_view`
(collider / rope_links / rope_attach beads: red link, blue body top, yellow model hands);
archer_state animation.rope_joint_gap / rope_hands_off. After the fix: gap 0.000, hands a
CONSTANT ~0.19 above the attachment - Hanging_Rope's grip vs the box top; user to choose how.

ROPE CLIMBING BUILT 2026-09-24 (animation_plan.md "Climbing the rope"): Rope_Climbing is NOT in
place (hips rise 0.569 world/cycle; gripping hand stationary in rig space) -> playhead SET from
distance climbed (Puppet::ClimbTimeAt over the measured rise), model lowered by PuppetChoice::lift,
nothing extracted (core extraction pins hips to BIND). Stage::rope_s grip distance; joint re-made
with local anchors; at a LINK CROSSING carry the anchor over (joints stretch 0.13-0.20 each under
her 70kg, snapping kicked her). archer_hold gained 'up'/'aim_down'. The "chain stretch" was NOT the mass ratio - hanging is +3%. Three climb bugs, fixed 2026-09-24 with the
ROPE TEST BENCH (MCP `rope_test`: per-joint gaps, loaded length, peak, iterations per scene, link
mass, cut a joint; panel Rope section): (1) re-made joint left the climb step to the solver, which
moved the 1.2kg LINK not her 70kg -> nudge HER the step first; (2) carried anchor offset
accumulated per crossing -> fades over ROPE_GRIP_BLEND; (3) ARCHER_MASK_LEVEL didn't accept her
category, so ON THE ROPE SHE NEVER HIT THE FLOOR -> fixed. Iterations 12/10 -> 30/30 -> 60/60 cut
hanging stretch 2.9% -> 1.1% -> 0.4%; 5kg links 0%. Mount: climb hands average on the box top.

LOOP JUMP + CUT FIXED 2026-09-24: core poses the rig BEFORE RunSimulationTick, so anything placed
from a pinned playhead must use LAST tick's pin (climb_base_posed). Climb playhead now follows the
distance's time capped at PUPPET_CLIMB_RATE_MAX (hip pauses used to pass in 1 tick). A cut:
RopeMeshInput::cuts splits skin weights per section (re-uploaded from PreRender when the chain's
cut joints differ), grab points only above the first cut, Stage lets go past the last point
(f_rope_lost), loose links get ARCHER_MASK_ROPE_LOOSE (they fell through the floor forever).

Next: the rope ANIMATION with the user (crude today) - grip-at-a-point + hand IK onto the curve.

Gotchas: `archer_place` blocks (HTTP timeout) while the sim is paused - the next sim_step
delivers it. To frame a close screenshot use `archer_camera orbit` then `camera_set`; the side
camera overrides camera_set. Blocks span z -1.5..1.5, so a vine at z -1 is hidden behind a
block's front face from a straight-on camera.
Related: [[archer-app]], [[shell-heredoc-limit]].

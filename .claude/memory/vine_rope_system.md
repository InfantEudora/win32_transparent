---
name: vine-rope-system
description: "Vines and the swinging rope share one base - core/Spline (centripetal CR + arc length + rotation-minimising frames) and core/SplineDeform (tile an authored mesh along it); steps 1-2 (static decorative vines) BUILT 2026-09-24, plan in apps/archer/docs/vine_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 95091de0-2e6d-47f4-a5dd-59fe26a32d0c
  modified: 2026-09-30T10:12:00.055Z
---

Agreed with the user 2026-09-24: a rope and a vine are one system - a curve with three operations
on it (Deform a tile along it, Derive new curves like a helix wrap, Scatter instances). Vines are
placed EXPLICITLY for now (generated along terrain features later); most decorative ones static,
a few may sway; baking all static vines into one mesh is the later cost optimisation.
Plan and ideas (grip-at-a-point, rope arrow, cutting, drapes) in `apps/archer/docs/vine_plan.md`.

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

LOOSE LEGS BUILT 2026-09-25 (animation_plan.md "Loose legs on the rope"): core/DynamicChain
(verlet dynamic bone, engine-free, tools/dynamic_chain_test.cpp 20 checks) as a post-pose pass
ArcherModel::ApplyLegChains; Puppet owns leg_weight/leg_lead_deg (pump, Stage::rope_pump)/
leg_gravity. Lessons: planar chain must use the CHARACTER's plane (she yaws on every swing
reversal), carry her yaw rigidly (DynamicChain::Carry), and pass the clip's own motion through
(follow=1 with her world rotation as frame) or a soft chain smears the clip. Feet gripping the
rope DURING the climb = foot IK/sockets, a separate later step (user agreed).

Next: the rope ANIMATION with the user (crude today) - grip-at-a-point + hand IK onto the curve.

Gotchas: `archer_place` blocks (HTTP timeout) while the sim is paused - the next sim_step
delivers it. To frame a close screenshot use `archer_camera orbit` then `camera_set`; the side
camera overrides camera_set. Blocks span z -1.5..1.5, so a vine at z -1 is hidden behind a
block's front face from a straight-on camera.
Related: [[archer-app]], [[shell-heredoc-limit]].

2026-09-29: FACETED TRUNK FIXED - TrunkDeform had f_flat_normals on (left from the placeholder);
vine_trunk/vine_curl are smooth in the glb. SplineDeform now carries normals through the deform's
inverse transpose (taper/twist/stretch) and has f_weld_seams (tile end-ring normals disagree ~11
deg). Verified in-app. GROWING VINES PLANNED (not built) in vine_plan.md sections 7-14: arrow kinds
as RULES state (Stage::arrow_kind, Arrow::kind, keys 1-5 recorded, DrawArrowHud via UIOverlay),
SplineDeform `grown` reveal laid for full length (no popping), engine-free walker GrowVine, species
table (roots/bamboo/vine/thorny/grape). User decided same day: every plant has roots (shown only
out of undersides); normal arrow in an underside = roots then a tuft on the top ABOVE (confirmed),
into a wall/top = a small tuft; walls grow creepers by arrow kind; cap 32 with withering leaves
blown off. Section 15: platform edges become a derived Stage list (StageEdge) - today fear,
FindGrabbableLedge and Foliage each find edges their own way; the user wants edges easy to look up
for a coming teeter / fall-and-catch animation (fear of heights). Fall-and-catch AGREED: any edge
with a real drop, but only the one she just left, inside the coyote window. Nothing open; the plan
is approved (order in section 14).
STEP 1 (arrow kinds) DONE 2026-09-29, details in vine_plan.md step 1: Stage::arrow_kind survives
Reset (recording state carries it), keys 1-5 + d-pad, DrawArrowHud bottom right, BuildArrowDress
tints by material index.
STEP 2 (SplineDeformParams::grown reveal, cone tip) DONE 2026-09-29, 11 spline_test checks.
STEP 3 (StageEdge list, apps/archer/StageEdges.cpp) DONE 2026-09-29: rebuilt by Reset,
KeepBlockLayout and a per-tick live-block COUNT (not the full fingerprint - PredictLanding ticks a
Stage copy 30x/tick); archer_edges MCP + archer_debug_view edges; fear/Foliage not rewired yet.
STEP 4 (GrowVine walker + VineSpeciesFor(VINE_SPECIES_VINE)) DONE 2026-09-29, engine-free in
Vine.cpp, 16 checks incl. a 200-seed sweep.
STEP 5 DONE 2026-09-29: vine arrows grow vines on undersides in-game (StartGrownVine/StepGrownVines
physics thread, DrawGrownVines in PreRender, grown_shared under grown_mutex, 32 slots + 512
leaves/kind pooled, vine_grow cue is a rock_crumble PLACEHOLDER). Growth cost 5.6ms/frame debug ->
fixed by per-distinct-height frame sharing in DeformAlongSpline (~2.2ms now). To test in-app:
archer_place x 10.7 (NOT 9.2 - lands on the step), arrow_2, draw 45 wait false + up 17 -> hits
slab one's underside (11.6..12.4, 3.8). User: 2.2ms accepted; a GPU deform later maybe.
STEP 6 DONE 2026-09-29: TerrainField.{h,cpp} (engine-free field moved out of Terrain.cpp, verified
identical by the tri/dip/rise stats), TerrainSurface, VineField/VineBlockField/VineLevelField,
start marched out of drawn rock, VinePath::f_rooted (no start taper). Bay test shot: place
(-15, 3.4) on the hill, draw 50 wait false + up 27 -> stone two underside (-14, 6.4).
STEP 7 DONE 2026-09-29: VINE_SPECIES_ROOTS + GrowRoots, Foliage ScatterTuft, app growth is a
PLANT (strands with look/species/start + tufts), StartGrowth decides per strike, two rings of 32
(vine / small). The user often runs their own archer on 8765 (PID differs): stop MY instance by
PID, never `taskkill //IM archer.exe`.
2026-09-29: roots/tufts DOUBLED (user). STEP 8 DONE: creepers = VineSpecies hug habit (hug/
hug_reach/hug_gap/climb in WalkStrand; "over the lip" only when normal up AND heading level),
VINE_SPECIES_CREEPER for a vine arrow into a wall/top. In-app test: place x 12.5, hold left 3,
arrow_2, draw 45 wait false + aim_down 10 -> step's right face (9, 1.49). Next: step 9 withering
(leaves to LeafSwarm) + growths falling with crumbling blocks. Also done 2026-09-29 on the user's ask:
the aim only tilts while drawn and returns to neutral after BOW_AIM_RETURN_TICKS of moving.
2026-09-30 STEP 8b DONE (user asked, before step 9): normal arrow into a WALL grows roots (a
sideways tuft looked wrong); tops keep the tuft. BAMBOO arrow (ARROW_BAMBOO, key 3):
VINE_SPECIES_BAMBOO (gravity -3 = up) + GrowBamboo clump (3-5 canes, per-cane gravity so wall
canes fan) + ScatterBambooLeaves (sprays at stalk nodes, rolled 90 deg to face camera);
bamboo_stalk tile / bamboo_tip pooled Object riding the front (VineParams grow_tip_* 0/1) /
bamboo_leaf (VINE_LEAF_BAMBOO past VINE_LEAF_KIND_COUNT); bamboo_end unused. In-app tests: ground
= place x 14.5, left 3, arrow_3, draw 45 nowait + aim_down 30 -> lands x -8.1; underside = place
x 10.7, draw 45 nowait + up 17 -> (15, 6.4). sim_pause takes {"paused":false} to resume. The
lockd broker can restart and drop claims mid-session: re-check lock_list before editing again,
and never write a held file by script (the hook only guards Edit/Write). Next is still step 9.
Growth SOUNDS wired 2026-09-30 (user's wavs): vine_grow / bamboo_grow / roots_grow cues, one per
strike by what grew; roots_grow gain 0.3.
cue_replay prints "STATE DIFFERENT" in CAPS - grep -i before trusting a --write.

2026-09-26: four big CAVE vines added in the terrain bay (DeclareVines, #if ARCHER_TEST_BAY), two behind her and two in front at z 2+; hanging-start vines need up=+Z; see vine_plan.md "The cave vines".

---
name: archer-hair-chains
description: Archer hair physics BUILT 2026-09-28 - three 3D DynamicChains off mixamorig:Head, wind + flutter, scalp sphere; the four traps a platformer body sets for hair (game gravity, compounding cones, jolts, drag) and the crown-weighting issue left for the artist
metadata:
  type: project
---

Rig: hair_back.1/.2/.3 (chain) + hair_side.L/.R (one bone each), children of mixamorig:Head; weighted
on archer_hair only. ArcherModel::BuildHairChains measures leaf tips off the mesh (vertex * the leaf's
inverse bind matrix, max local +Y) and the scalp sphere off Head-weighted vertices in Head space,
shrunk until every rest point is outside. ApplyHairChains runs LAST in ApplyAnimation (after the aim),
every tick, carries her yaw turns like the legs, and turns each bone onto its simulated segment with a
quat (3D, unlike the planar legs). Wind = WindField::Velocity at the head (full strength up there).

Core DynamicChain gained (tools/dynamic_chain_test.cpp, 29 checks): `spheres` (push out, re-length),
3D `cone` limit measured against the POSE direction (against the parent-carried pose the cones ADD UP:
50/segment let a 3-bone tip turn 150 and stand the hair up), and `max_accel` - the root's
acceleration felt up to a cap, the rest carried with no momentum; under a cap damping is relative to
the felt root (damped in the carried frame -> hair trails UP at a landing; damped vs the world ->
drag streams it straight up on a fall). "Hand on a share of the root's motion" (inert) cannot work.

Traps, all measured: hair at 9.81 is out-fallen by her 42-57 u/s^2 game gravity and floats over her
head at every apex -> hair_air_gravity = her gravity while airborne; takeoff/landing are 18-19 u/s in
one tick -> max_accel 60 (just above falling gravity); the swing readout must be the WORST segment.
Defaults from a jump sweep: stiffness 0.12, damping 0.15, follow 0.5 (follow barely matters).

OPEN, artist side: 69% of verts mostly on hair_back.1 sit ABOVE its root (the crown), none mostly on
Head - so any back swing lifts the whole hair cap off the scalp. Crown should be 100% Head.
Related: [[archer-app]], [[wind-system-plan]].

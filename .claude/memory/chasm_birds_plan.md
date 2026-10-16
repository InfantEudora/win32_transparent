---
name: chasm-birds-plan
description: "Chasm birds: birds_plan.md AGREED 2026-10-17; steps 0-2 + homes BUILT same day (bird in GLB, Birds.{h,cpp}, ApplicationChasmBirds.cpp, chasm_birds tool, wiki/birds.md); sound, clouds, title flock next"
metadata:
  node_type: memory
  type: project
  originSessionId: 2a27b5d5-13c2-4f25-8145-e56a2a256841
  modified: 2026-10-16T23:42:42.128Z
---

Plan agreed 2026-10-17 (apps/chasm/docs/birds_plan.md - its "As built" section is the current state), for the demo:
black flocks, the first animated thing that is NOT state, flushed out of a tree being cut or by a villager walking by.
The user gathers the magpie/crow/raven sounds.

The user's answers that day: birds FREEZE on pause (game clock); the props SCRIPT made the first bird, the user
reshapes it; visible birds' calls are CUE-TABLE events (exception noted in sound_plan.md); size about HALF A VILLAGER
(read as 0.43 beak to tail, "we'll see how they turn out"); a TITLE flock is step 5; other ideas noted for later.

Step 0 BUILT by a worker window: asset `bird` (80 tris, one primitive, keys WingsUp 0 / WingsDown 1 / Folded 2,
Folded sits the body up 15 deg), palette row 11 PAL_CREATURES, props tool exports morphs and --merge remaps targets
incl. sparse accessors; previews art_source/chasm/previews/bird_*.png. Open: a reshaped bird can only reach the GLB
by a whole export (no "re-merge these nodes" yet).

Steps 1-2 + step 4's homes BUILT by me the same day, checked in a muted test copy on village_year5:
- Birds.{h,cpp} engine-free (BirdSky, flocks settle/travel/circle/scatter, BirdWorld asks ground/roost/standing);
  ApplicationChasmBirds.cpp: UploadBirds from PreRender, one Object per bird on one mesh, perch points from each
  tree LOOK's mesh, scares = SignalWork's chop test + movers since last tick + EVENT_TREE_FELLED (prop = amount).
- My calls, for the user to overrule: 3 COLONY flocks homed round the colony and FOLLOWING THE AXE (lures = felling
  sites remembered 90 s) - without that nothing was ever flushed (scare_gap 14-50 vs reach 4-8); clock = real time x
  physics_time_factor (tick count stutters at bird speed); cohesive flush (one way out, not a burst).
- Measured: 42-49 us/frame for 84 birds in debug. At zoom 55 they read over open ground/water, nearly lost over dark
  pine; colour reads blue-purple (sheen cell). Size knob BIRD_DRAW_SCALE.
- chasm_birds: status (flushes, scare_gap, lures, step_us), spawn, flush, clear, reset, bench, show/hide.

**Why:** birds must never draw from the shared RRandom or enter save/hash/recording ([[rrand-shared-stream-todo]]).
**How to apply:** next per the plan: sound (step 3, BirdCall already made, dropped in UploadBirds), hide over clouds +
seasons (4), title flock (5). Traps hit: `near` is a Windows macro (rename params); a heredoc'd Python writing C "\\n"
gives a real newline (CLAUDE.md). Blender traps (worker, Blender 4.5): a new bmesh shape layer invalidates held BMVerts;
bm.to_mesh drops shape layers (add keys with shape_key_add + foreach_set); glTF-imported objects use QUATERNION
rotation. Related: [[chasm-sound-plan]], [[chasm-stone-props]], [[chasm-villager-animation]], [[chasm-demo-menus]],
[[spider-leg-shape-keys]], [[chasm-test-instance-autosaves]].

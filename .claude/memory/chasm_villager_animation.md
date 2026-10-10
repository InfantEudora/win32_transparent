---
name: chasm-villager-animation
description: "Chasm villagers going skinned - user's 7-bone rig on adult_male, crowd bench measured 2026-10-10 (cost is CPU, ~2.3 us/tick + ~7 us/frame per figure); export-settings trap"
metadata:
  node_type: memory
  type: project
  originSessionId: 0d964184-f477-47d6-845e-a14ed3a2d9dd
  modified: 2026-10-10T15:09:51.247Z
---

2026-10-10: aiming at 100-200 villagers + zombie waves on screen. Shape keys (the spider way) were
the first suggestion; the user pointed out that a skinned mesh CAN be shared - bones are Objects but
only fill the bone SSBO, and the renderer batches by mesh - so skinning is the direction now. Bomber's
per-actor mesh copy is Bomber's choice, not an engine limit. Sockets = zero-weight bones with props as
child Objects (the archer's bow on a hand bone is the precedent).

**The rig:** `character_armature` in `chasm_props.glb` - Hips, Spine, Head, Arm.L/R, Leg.L/R - with
one clip `Walking` (12 keys, 1 s). Only `adult_male` is weighted so far; the other three figures not.

**Measured** with `apps/chasm/ApplicationChasmCrowdBench.cpp` (MCP `chasm_crowd_bench {count, static}`,
port 8769, camp spot, play's max zoom 55), release, each mode from a fresh start:
- skinned, per figure: ~2.3 us per TICK (clip sampling, sim thread) and ~7 us per FRAME render CPU
  (bone gather, done once per skinned pass - shadow, G-buffer prepass, colour); GPU ~1 us.
  At 200: tick 0.42 -> 0.89 ms, frame work 4.4 -> 5.7 ms. At 800: frame work 10.6 ms.
- static plain Objects with today's figure: ~free even at 800.
- debug (-Og) costs the same as release - the time is overhead (list walks, allocation, recursion,
  three passes), not arithmetic. Engine fixes if it ever matters: gather each skeleton's palette once
  per frame for all passes, cache world matrices, index keyframes. Not needed at 200.

**Built 2026-10-10** (`ApplicationChasmWorkersView.cpp`): any figure whose node has a skin is drawn as a
per-person Skeleton (found via the new `GLTFLoader::GetNodeSkinName`, so no rig is named in code);
unrigged figures stay static. Clips are played ROTATION-ONLY (positions dropped) so the man's clip
serves every rig with the same bone names - advice to the user: other figures keep their own height,
get their own armature (copy of the man's, joints moved, same bone names), and the action stays on
the man's rig only. Loads are child Objects of bones (log/sack on Spine, pail on Arm.R), their verts
pre-moved by the bone's inverse bind. Walk rate = leg speed / (0.71 * fit); standing = bind pose
unless the file has an `Idle` clip; hidden people get no clip. Checked in game: walk, log on shoulder.

**Trap:** the Export collection's exporter saved in `chasm_props.blend` had `export_skins` and
`export_animations` OFF (seen 15:59); a one-click export or `blender_chasm_props.py --add` drops the
rig. The script's `--force` settings were updated to include them. See [[chasm-game-plan]].

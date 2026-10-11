---
name: chasm-villager-animation
description: "Chasm villagers going skinned - user's 7-bone rig on adult_male, crowd bench measured 2026-10-10 (cost is CPU, ~2.3 us/tick + ~7 us/frame per figure); export-settings trap"
metadata:
  node_type: memory
  type: project
  originSessionId: 0d964184-f477-47d6-845e-a14ed3a2d9dd
  modified: 2026-10-10T22:33:02.091Z
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

**Re-rig 2026-10-10 evening:** 15-bone rigs `male_armature` + `female_armature` (Arm.Upper/Lower,
Hand, Leg.Upper/Lower, Foot, .L/.R), `Walking` 0.625 s with a 3 cm hip bob, the action on BOTH rigs
(90 channels, copies differ up to 12 deg). Core fixes made: tracks grouped per glTF NODE
(`ObjectAnimation::target_node`; by-name grouping merged both rigs' keys into one track), LinkObjects
binds own-node tracks first then by name to unclaimed bones, GetSkinnedVertex renormalizes the 3 kept
weights (the dropped 4th pulled verts toward the origin, up to 0.18 model units; archer too). Chasm:
pail on Hand.R, borrowed tracks only are rotation-only, walk speed measured from Foot.L/R at Init
(~0.81 u/s at rate 1). adult_female has 52 verts weighted to nothing (front-left hip) - art fix.
Also fixed: Object::ApplyAnimation tested `!current_animation` before LOAD_DEFAULT_POSE, so
SwitchToAnimation(NULL) never loaded the reference pose - stopped villagers froze mid-stride (verified:
standing Hips back to 0.763). Checked in game: man + woman walk side-on, hip bob 0.729-0.758.

**Shoot clip 2026-10-11** (from Mixamo `Shooting30fps`) via `tools/blender_retarget.py`: world-space
retarget + per-bone RDP to LINEAR keys, 1,463 keys vs 30,200, 7.3 KB in the glb, loads in chasm.
On male_armature only, in a muted NLA track `Shoot` (that is how the exporter picks it up). The clip
turns the body left-side-on; the bow arm points along rest forward (-Y), so face the target and play
it. Not yet played by any game code. Export is now UNSAMPLED (`export_force_sampling=False` in the
collection exporter, the remembered dialog settings and `blender_chasm_props.py`): unsampled keys keep
their frame time and slide-to-zero does nothing, so clips are keyed from frame 0 (Walking shifted to
0,3,7,11,15 - 0.625 s, loop key included). Scene fps restored to 24 (the FBX import had set 30).
File quirks: timeline marker `F_12` binds the scene camera to male_armature (renders come out empty);
the `adult_female` MESH has Walking assigned (harmless exporter warnings); the Mixamo rig + its
`adult_male.001` sit in collection `Retarget sources`, outside Export.

**Trap:** the Export collection's exporter saved in `chasm_props.blend` had `export_skins` and
`export_animations` OFF (seen 15:59); a one-click export or `blender_chasm_props.py --add` drops the
rig. The script's `--force` settings were updated to include them. See [[chasm-game-plan]].

---
name: chasm-clip-channels-export
description: "Unsampled export ships only keyed channels; a clip without a bone's channel leaves it stale; tools/blender_action_report.py; Zombie_Scratch hips crouch (2026-10-19)"
metadata:
  node_type: memory
  type: project
  originSessionId: f3b8cf58-c883-4dd4-b5a6-08e3c9d2dac8
  modified: 2026-10-19T09:09:24.857Z
---

Measured 2026-10-19 (scratch exports of chasm_props.blend, compared channel by channel):

- **The Export collection's exporter is UNSAMPLED**: a channel reaches the .glb only where the action has
  a curve. The File > Export dialog in that file remembers sampling ON, and the shipped 2026-10-18
  chasm_props.glb was sampled (all 3 channels on every bone, Shoot's reduced keys back to ~105 a channel).
- **A clip with no channel for a bone does not reset it**: SwitchToAnimation keeps the pose, so the bone
  stays where the last clip left it. Borrowed tracks (KeepOwnPositions turns their positions off) do the
  same for position. FIXED 2026-10-19 (user's go-ahead): KeepOwnPositions writes the bone's own
  reference_position into a borrowed track's keys instead of switching positions off. A/B on the HEAD export
  in an isolated copy, title town: before, men walking the borrowed Walking sat at their Idle's 0.7393 and women
  idling in the borrowed Idle froze at their walk's last bob (0.735-0.757); after, both read rest 0.7631.
  The 11:00 re-export puts all four clips on all three rigs, so nothing borrows today - the fix is the net.
- No root bone is set in Chasm: Hips translation is applied in place, never extracted to motion.
- `tools/blender_action_report.py`: per-action report (rest/const/~/moves per bone property) and `--clean`,
  which deletes only scale-at-1 and non-root location-at-0 curves and THINS rest rotations/constants to two
  keys - deleting a rest rotation dropped the channel from the .glb (see above). Verified output-identical.
- **Clips keyed from frame 1 export starting at 1/fps** - export_anim_slide_to_zero is ON in the collection
  exporter and does nothing unsampled (re-checked Blender 4.5.5). ObjectAnimation::Sample holds the first key
  before it, so every loop freezes one frame. The user moved all clips to frame 1 on 2026-10-19 and plans a
  Hips location curve in every clip; offered: back to frame 0, or shift clips to zero at load.
- State that day: fps back to 24 by 10:41; as saved then Idle was on no rig (not exported at all), Walking
  on male_armature only (16-frame cycle, 1-17), Shoot on all three rigs. Spine never moves in Zombie_Scratch.

See [[chasm-villager-animation]], [[chasm-zombies-plan]].

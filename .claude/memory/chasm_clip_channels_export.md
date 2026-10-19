---
name: chasm-clip-channels-export
description: "Unsampled export ships only keyed channels; a clip without a bone's channel leaves it stale; tools/blender_action_report.py; Zombie_Scratch hips crouch (2026-10-19)"
metadata:
  node_type: memory
  type: project
  originSessionId: f3b8cf58-c883-4dd4-b5a6-08e3c9d2dac8
  modified: 2026-10-19T15:05:27.824Z
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
  ~~The 11:00 re-export puts all four clips on all three rigs, so nothing borrows today~~ WRONG: the export
  writes each rig's copy as a SEPARATE glTF animation under the same name, and LoadAnimation read only the
  first (male_armature's), so the woman and zombie borrowed EVERY clip. Seen by the user as fallen zombies and
  women lying flat at hip height. FIXED 2026-10-19: `GLTFLoader::LoadAnimation(name, f_every_copy)` (default
  first-only; dozer.glb's 3 `EngineIdle` on Body/Exhaust/Tracks is the only other repeated name, untouched);
  every Chasm rig clip loads with `true`, so LinkObjects binds each rig its own copy. Check a glb's layout with
  a per-animation armature listing before claiming what a rig plays.
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

**2026-10-19, the user's tool test** (final 16:33): blender_retarget.py put `Die_Forward` (from Die_ForwardMixamo on
`Armature`, 81 fr, 3.33 s) and `Zombie_Run` (from the user's re-downloaded IN-PLACE `Zombie_RunMixamoInPlace` on
`Armature.001`, 20 fr, loops within 1.1 mm) on male_armature, muted NLA tracks, own-name slots, keyed from FRAME 0
then the user shifted them by hand to 1 and removed the Mixamo source armatures (Export now holds only the 3 rigs).
The user's Mixamo downloads are 24 FPS (pass --source-fps 24; the tool's default is 30). --clean on Die_Backward:
10,950 -> 4,315 keys; on retargeted clips a no-op.
**FRAME 1 is the user's convention** (2026-10-19 evening, "Blender's default"): blender_retarget.py now keys from 1
(`--first-frame`, default 1); blender_chasm_props.py's note says so. The export's "Set All glTF Animation Starting
at 0" is ON in chasm (dialog + collection exporter) but BROKEN unsampled in Blender 4.5 - FIXED by our add-on
[[gltf-fixes-addon]] (2026-10-19). GLTFLoader keeps times as they are (no engine shift wanted - the user's call).
The user's hand shift first caught only the selected bone; they redid it - by 16:59 all clips start at 1 together.
The Mixamo rigs and male_armature.001 (Die_Backward) sit IN the Export collection (flagged). Pre-run copy in that
session's scratchpad only.
**Blender 4.4+ SLOT trap (found by the user the same hour)**: assigning a new action to an armature makes its slot from
the armature's LAST slot name - the retarget's keys landed in a slot "Walking" (OBWalking) and the Action Editor showed
an empty clip. Fixed in blender_retarget.py: `act.slots.new(id_type="OBJECT", name=name)` before assigning. The user's
file names every clip's slot after the clip; assigning one in the editor then needs that slot PICKED from the dropdown
(Blender auto-picks only a slot matching the armature's last one). Any script making actions should name the slot.

See [[chasm-villager-animation]], [[chasm-zombies-plan]].

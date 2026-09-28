---
name: blender-export-speed
description: Why the archer glTF export took 4 minutes (Mirror before Armature re-evaluated every sampled frame, every action sampled over the longest action's range) and the one-checkbox fix; tools/blender_export_profile.py measures it
metadata:
  type: project
---

Measured 2026-09-28 on elfarcher_withprops.blend (code/test/, one up from the repo) with
tools/blender_export_profile.py, which reruns the file's own saved glTF settings (scene
glTF2ExportSettings, active collection "Export") under cProfile and takes `--set key=value` overrides.

- 237 s, 96% animation sampling. Images/meshes/materials < 5 s - the 4096 bake was NOT the cause.
- The glTF add-on in ACTIONS mode samples EVERY action over the min..max range of ALL actions
  (sampling_cache.get_range) - 39 x 440 frames (WarmUp's length) = 17160 frame_sets for ~3800 real frames.
- Each frame_set cost 11.15 ms; Mirror-before-Armature on archer_body/archer_face was 8.8 ms of it
  (mesh re-mirrored on every pose). Hair GN and the other collections cost nothing measurable.
- FIX: export_optimize_disable_viewport=True ("disable viewport for other objects") -> 58 s, output
  identical (same binary, same accessors, animation channels only reordered).

**Why:** the slowdown arrived with the Mirror-modifier workflow the bone rename was for ([[archer-app]]).
**How to apply:** if the export slows again, run the profile tool before guessing; the longest clip
sets the cost of every clip, so a very long clip (WarmUp) is expensive for the whole export.
When benchmarking frame_set by hand in 4.4+, assign action_slot too, or nothing animates (0.1 ms/frame).

SHAPE KEYS + MIRROR (same day): the dialog with Apply Modifiers DROPS every shape key of a mesh with a
non-Armature modifier (glTF add-on primitive_extract.py:153) and bakes the current key values in -
archer_face's MouthOpen (value 0.4) was exported as a 40%-open mouth. tools/blender_export_glb.py
exports with the file's saved settings after baking pre-Armature modifiers per key (never saves the
.blend); shared settings code in tools/blender_gltf_saved.py. Engine side was already fine: skinned
shader applies morph targets. Character panel "Face" -> mouth open (ARCHER_FACE_KEY_MOUTH_OPEN = 0).
IN-BLENDER VERSION: tools/blender_gltf_keep_shape_keys.py is an add-on (install from disk) using the glTF
exporter's own user-extension hooks - pre_export_hook swaps in a baked mesh with keys, post_export_hook
(plus a bpy.app.timers fail-safe) swaps back; dialog section via io_scene_gltf2.exporter_extension_layout_draw.
Verified headless: scene identical after export, face output byte-identical to blender_export_glb.py.
Enabling in a background test needs addon_utils.enable(); --addons with BLENDER_USER_SCRIPTS did not take.

---
name: gltf-fixes-addon
description: tools/blender_gltf_fixes.py - ONE add-on for all our Blender glTF exporter fixes (shape keys through modifiers; start-at-0 unsampled); add new quirks to it
metadata:
  node_type: memory
  type: project
  originSessionId: 0458ec6c-789b-4dff-b918-cd2b316d95b8
  modified: 2026-10-19T15:05:26.552Z
---

Built 2026-10-19 at the user's request ("Let's stick them all together. Wouldn't be surprised if we run
into another quirk later on"). `git mv` of tools/blender_gltf_keep_shape_keys.py -> tools/blender_gltf_fixes.py.
A NEW EXPORTER QUIRK GOES IN HERE as one more fix: its own switch in `scene.gltf_fixes` (PropertyGroup
GltfFixesProperties), its own hook in glTF2ExportUserExtension, a numbered section in the docstring.

1. Keep shape keys through modifiers (pre/post_export_hook) - moved unchanged; re-verified on a copy of
   elfarcher_withprops.blend: off -> archer_face 0 morph targets, on -> MouthOpen+Blink, scene restored.
2. Start at 0 unsampled (gather_gltf_hook): Blender 4.5's "Set All glTF Animation Starting at 0" is
   applied ONLY in sampled/*/sampler.py; the fcurves/ (unsampled) path ignores export_settings['slide'].
   The fix moves each glTF animation back by its earliest STORED (float32) key time, copying inputs
   (gather_accessor is @cached - shared), samplers copied if shared across differently-starting animations.
   Verified on chasm_props: first keys exactly 0.0, every other byte identical. Runs only when the option is
   on: archer's file has it off (and samples) -> archer exports unchanged.

Headless: tools/blender_export_glb.py enables the add-on for the run via addon_utils.enable(default_set=True)
and disables after (userpref.blend untouched, checked), switching its shape-key half off (own bake).
Test with `--factory-startup` so the user's installed add-ons stay out. The stock exporter prints an
AttributeError traceback (gltf_action_filter, on_export_action_filter_changed) at file load - not ours.

User's install (as of 2026-10-19): the OLD add-on was installed+enabled in
%APPDATA%/Blender Foundation/Blender/4.5/scripts/addons; they must remove it and install this one.
Related: [[chasm-clip-channels-export]], [[blender-export-speed]].

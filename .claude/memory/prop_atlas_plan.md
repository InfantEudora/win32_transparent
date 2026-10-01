---
name: prop-atlas-plan
description: "Re-baking archer's 11 Tripo prop/terrain textures into one atlas; step 1 (island analysis, tools/blender_uv_atlas.py) BUILT 2026-09-30, packing+bake next"
metadata:
  node_type: memory
  type: project
  originSessionId: 68bb062d-4097-42ca-9cdd-5ced41676db9
  modified: 2026-09-30T18:58:50.679Z
---

Goal (user, 2026-09-30): the props under Export (Props_Eport + Terrain_Export, NOT Character - the
character already has its own baked atlas elf_image_baked) use 11 separate 4096^2 Tripo textures;
fold them into ONE atlas. Flat-colour UV islands collapse onto palette swatches, detailed ones are
packed and re-baked (moving UVs alone cannot pack, the pixels must move too).

Steps agreed: 1 analyse (report + preview) -> 2 palette swatches -> 3 pack detailed islands at a
shared texel density (Blender's own uv.pack_islands) -> 4 Cycles bake old UV -> new `atlas` UV map,
one material, export via tools/blender_export_glb.py. All in Blender's bundled Python + numpy, no
external tools. Never save the .blend from the scripts; keep the original UV map ("Attribute").

Step 1 first run: 62 objects, 10850 islands, 6172 flat -> ~409 swatches; everything at source
resolution ~ one 4096^2 (90%), at half resolution one 2048^2. Outlier: bamboo_stalk at ~3180
texel/m vs ~600 typical costs 2.4M texels - normalising density fixes it.

Watch: fantasy_environment_props_3d_model_basecolor is NOT packed and loads from a WinRAR temp dir.
The analysis adds colour attribute `atlas_class` (active) - run --mode clear before exporting.

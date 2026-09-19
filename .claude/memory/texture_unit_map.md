---
name: texture-unit-map
description: "The engine-wide texture unit layout lives in two mirrored files, and Android's 5 material units is deliberate because textures get atlased"
metadata: 
  node_type: memory
  type: project
  modified: 2026-09-19T08:17:10.811Z
  originSessionId: cdc79902-e744-48b9-8692-c116f8f0ff26
---

The texture unit map is `core/TextureUnits.h` and `shared_assets/shaders/texture_units.glsl` -
**two files that must agree number for number**, one per language. Reordered 2026-09-19: everything
the engine reserves is packed into units 0-10 and materials take everything above
(`TEXUNIT_MATERIAL_FIRST` = 11). Both files document the layout itself; don't duplicate it here.

**5 MATERIAL UNITS ON ANDROID IS A DECISION, NOT A LIMITATION TO FIX.** `GL_MAX_TEXTURE_IMAGE_UNITS`
is 16 on the test device, so materials get 11..15 there against 11..31 on the desktop. The user's
answer when shown that number: *"That's still enough to work with. Most of these games textures can
be packed into a single texture."* Atlasing is the intended route, not a bigger budget.

**Why:** a future session reading `NUM_MATERIAL_UNITS 5` will read it as tight and go looking for
units to reclaim. The four worth reclaiming (7-10: skybox, cloud shadow, field shadow, lowres
composite) were already identified as unused on low-end devices - that is the agreed escape hatch
if it is ever actually needed, and it has not been taken.

**How to apply:** pack an app's textures into one sheet before widening the range. An index into
the shader's `material_texture[]` is NOT a texture unit (it is `unit - TEXUNIT_MATERIAL_FIRST`) -
that off-by-one has drawn blood twice, and both shaders carry the note.

Related: [[android-port-merge]], [[per-app-build-layout]].

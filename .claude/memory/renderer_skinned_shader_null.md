---
name: renderer-skinned-shader-null
description: Renderer::skinned_shader is NULL by default - an app that draws a skinned mesh must assign it or the character silently does not render at all
metadata: 
  node_type: memory
  type: reference
  originSessionId: 1f02e4da-fa81-4634-9800-ae5b6cfa5ba7
  modified: 2026-09-21T19:54:42.286Z
---

`Renderer::skinned_shader` is **NULL by default** and is not wired up by `Application`. Every app
that draws a skinned mesh assigns it itself, in `Init`:

```cpp
renderer->skinned_shader = new Shader(shader_skinned_vert_name,shader_lit_frag_name);
```

`Renderer::Init` builds the *deferred* twin (`deferred_shader_skinned`, from
`skinned_vert_filename` + the deferred frag) on its own, so the log shows
`default_skinned.vert, deferred.frag` compiling and linking fine and it all looks wired up. It is
not - that program fills the G-buffer; `skinned_shader` is what the colour pass and the skinned
depth pass use, and without it nothing skinned reaches the screen.

**There is no warning and no error.** The GLB loads, the skin resolves, the bone count is right,
the clips play and advance, `object_get` reports the object visible and correctly placed, and the
deferred skinned program even shows up in the GL debug output as "being recompiled based on GL
state" - so it is clearly being *used*. Nothing appears. It reads exactly like a failed asset
load, which is why the archer cost an hour checking the mesh bounds, the bind pose, the skin
weights, the accessor offsets and the material before the shader.

**How to tell quickly:** grep the app's stderr for `Load and compile`. An app that draws skinned
meshes compiles `default_skinned.vert` TWICE - once with `deferred.frag` and once with
`default.frag`. Only one means this line is missing. `apps/bomber` (whose comment names this trap),
`apps/animation` and `apps/isoanimation` all have it; `apps/archer` did not.

Two neighbouring gotchas from the same hunt, both in [[archer-app]]:
- the engine skins with **three** bone influences per vertex, not four (`GLTFLoader::GetSkinnedVertex`);
- an exported PBR material with `metallic` high and no textures renders **black** in any app that
  turns the skybox off, because there is no environment to reflect.

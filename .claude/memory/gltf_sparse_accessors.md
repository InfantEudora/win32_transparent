---
name: gltf-sparse-accessors
description: core/GLTFLoader now supports glTF sparse accessors (added 2026-09-22) - Blender exports MORPH TARGETS that way, and it used to be a Fatal that killed the app at load
metadata:
  type: project
---

`GLTFLoader::ResolveVec3Accessor` materialises a FLOAT VEC3 accessor - sparse or not - into a dense
array, and `GetMorphVertexAt` reads from it. Added 2026-09-22 because apps/archer's bow could not
load: the app died on startup with

    fatal GLTFLoader : We don't support sparse accessors in GLB files yet.

**Blender exports every shape key as a sparse accessor.** A sparse accessor stores only the
elements that differ from a base, as indices plus values, and it may carry **no `bufferView` at
all** - which is legal and means the base is implicitly all zeros. The archer bow's `Drawn` key
declares 30/125/336/50 elements and overrides 1/1/1/20 of them.

So: any .glb with a shape key hit a hard `Fatal` before this. If a model with morph targets ever
fails to load again, check this path first rather than the exporter.

Two things worth not re-deriving:

- An absent morph attribute yields ZEROS rather than an error. A morph target is a delta, so zero
  is its identity - a key that moves vertices but exports no NORMAL is normal, not broken.
- **`GLTFLoader`'s debugger is constructed at `DEBUG_WARN`** (`core/GLTFLoader.cpp:5`), so its Info
  lines - the morph target list, the sparse-resolve line - never reach stderr. A silent log does
  NOT mean the path did not run; the sparse fix had to be confirmed by looking at the bent bow.

**Why:** this is a core change every app links, made for one app's asset. Nothing else in the tree
used morph targets at the time, so nothing else could have regressed.

**How to apply:** morph targets are per-Object via `Object::SetShapekey(index, factor)` with
`NUM_MORPH_FACTOR_SLOTS` of 4, and the engine can also animate them - the loader imports
`ANIM_TARGET_PATH_WEIGHTS` and `ObjectAnimation.cpp` applies it. Related: [[archer-bow-plan]],
[[archer-app]].

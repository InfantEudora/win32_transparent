---
name: asset-origin-convention
description: "Models are laid out spread across the Blender scene for authoring; the engine loads every model as if it sat at the origin, and that applies to armatures too"
metadata: 
  node_type: memory
  type: project
  originSessionId: c39cca72-76bf-40ae-b38c-9a45ea3638fd
  modified: 2026-09-18T22:14:19.490Z
---

One source .blend/.glb holds many models, **laid out side by side so the artist can see them all at
once**. Those authoring positions ARE exported, and the engine deliberately **ignores** them — every
model is loaded as though it sat at the origin, because where a thing sits in the .blend is an
authoring convenience and the app decides where things go at runtime.

This applies to **armatures as well as mesh nodes**, and that is the part that is not obvious.

**Why:** a skinned mesh is exported in SCENE space, so its vertices already carry the armature's
offset. Attaching root bones straight to the Skeleton makes the skin matrix `S * inverse(armature)`,
and that inverse is exactly what cancels the offset baked into the vertices. The two cancel out.

**How to apply:** do not reproduce the armature node's transform in the runtime hierarchy, even
though glTF's `globalJointTransform * inverseBindMatrix == identity` test then fails. That failing
test is correct here. The Android merge (commit a6be8a4) "fixed" it by inserting the ancestor chain
as Objects, which made the rig spec-correct and the game wrong: the offset stopped being cancelled
and, living in the skeleton's local space, rotated with the actor — bomber's enemies drew a tile off
their square and turning them changed which side. Only `enemy_armature` (z +1.280) exposed it;
`character_armature` and `turd_armature` sit at the origin, so the test data could not express it.

The reasoning is written out at length in `GetSkeleton` in core/GLTFLoader.cpp. Related:
[[skinned-rig-bone-indexing]], [[android-port-merge]].

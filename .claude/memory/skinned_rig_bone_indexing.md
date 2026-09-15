---
name: skinned-rig-bone-indexing
description: "A bone's identity is its position in skin.joints, not the order it was visited; GetSkeleton loads every root, Renderer lays matrices out by that index (fixed 2026-09-15)"
metadata: 
  node_type: memory
  type: project
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T16:08:19.780Z
---

Three coupled things in skinned-mesh loading, all fixed 2026-09-15 and all previously held together
by the accident that every rig in the project was a single chain.

**The invariant:** a bone's index is its POSITION IN `skin.joints`. The `JOINTS_0` vertex attribute
indexes it, and `inverseBindMatrices` is parallel to it. Anything that uses a different ordering -
tree-walk order, for instance - only works while those two happen to coincide.

**What was wrong:**
1. `GLTFLoader::GetSkeleton` started at `skin->joints[0]` and recursed through its CHILDREN, so a
   joint that was a SIBLING of the first was never loaded. glTF does not require joints to form one
   tree; `skin.skeleton` is optional and often absent.
2. `Bone::bone_index` was a depth-first visit counter, not the joint index.
3. `Renderer` uploaded each instance's bone block in `Skeleton::GetAllBones()` traversal order while
   the shader indexed it by joint index.

**Symptom, and why it is nasty:** a multi-root rig loaded one bone, every clip track bound to
nothing (so the clip "played" and nothing moved), and the MESH TORE APART - the shader read
`bone_data[instance * bone_count + bones.x]` with `bone_count` short while vertices still carried
the full joint range, so it read into the next instance's block. Nothing above Trace level said so.

**It is not always the artist's fault:** Blender's exporter adds its own root-level `neutral_bone`
to some rigs, so a second root appears in files nobody built that way. That is what settled the
"fix the art or fix the engine" question in favour of the engine.

**Now:** every joint with no joint parent is loaded as a root; recursion only follows children that
are joints of that skin (a mesh parented under a bone no longer becomes a bone); `bone_index` is the
joint index; `skeleton->num_bones` is the joint COUNT (it is the shader's stride, so it must be the
list length even if a bone is missing); and the renderer writes each bone into `bone_base +
bone_index`.

Exercised by `apps/bomber`: the enemy (`Hips -> Torso -> Head, Arm.R`, one chain) and the turd
(`Base`, `Flies`, `neutral_bone`, three roots). See [[bomber-app]],
[[animation-state-machine-in-object]] and `apps/bomber/engine_notes.md` §12.

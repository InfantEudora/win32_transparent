---
name: root-yaw-always-extracted
description: "FIXED 2026-09-22: Animation::extract_yaw_root_motion now gates the root bone's yaw the way the two position flags gate its translation; before that the twist was stripped from the pose unconditionally and discarded, so hips never rotated on any non-PlayerCharacter object"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 1f02e4da-fa81-4634-9800-ae5b6cfa5ba7
  modified: 2026-09-22T08:07:26.214Z
---

`Animation::SampleRootMotion` used to gate the root bone's POSITION on
`extract_horizontal_root_motion` / `extract_vertical_root_motion` and gate its YAW on nothing:

```cpp
out.yaw = WrapAngleDelta(cur.twist_angle - prev.twist_angle);   //ALWAYS
bone->SetRotation(cur.swing * bone->reference_rotation);        //ALWAYS swing-only
```

The twist came off the bone whether or not anything would put it back, and
`Object::ApplyRootMotion`'s base implementation does nothing — only `PlayerCharacter` overrode it.
So on every other object **the hips never rotated at all**: not "the character does not turn", the
rotation was deleted from the POSE. Measured in apps/archer: exactly 0.00 degrees of hip twist on
every clip, while every other bone animated freely. `Dance` was losing 40 degrees of it.

**Now `Animation::extract_yaw_root_motion` is the choice**, defaulting to false beside the position
flags and for the same stated reason. `PinnedBoneRotation` mirrors `PinnedBonePosition`: extracting
gives `swing * reference`, not extracting recomposes `swing * twist * reference` (the ordering
`DecomposeSwingTwistY` produces) and the clip plays exactly as authored. `LerpRootMotion` reads
each side's own flag. `CopyConfigFrom` copies it.

**EXTRACTING THE YAW DOES NOT COMPOSE WITH A TRANSLATION LEFT ON THE BONE.** The character's
rotation is applied above the root bone, so R(yaw) * T(p) = T(R(yaw)*p) * R(yaw) - an authored
offset still on the bone gets ROTATED rather than translated, and a clip that walks while turning
orbits a point. apps/archer's Twirl authors a 0.879-unit step back while spinning; with its yaw
extracted the hips traced a circle growing to 1.759 world units (0.879 x the 2.020 model scale),
with it left on the bone they travel in a straight line to 1.759 against an authored 1.776. A clip
whose yaw is extracted must turn ON THE SPOT, or extract its translation too. Counter-rotating the
leftover offset in core would need the twist at the clip's START, which SampleRootMotion does not
have, so the combination is documented and left to the caller to avoid. The archer's rules test
enforces it: f_turns and f_travels may not both be set.

**It has to be per clip, and it is authored knowledge, not a threshold.** A pivot's hip yaw IS the
clip; a run cycle's hip yaw is the gait, and extracting THAT wags the whole body (23 degrees on the
archer's `Running_Fast`). A gentle turn and a hip swing are the same shape at different amplitudes,
so nothing can tell them apart by measurement. apps/archer carries an `f_turns` column that sets the
flag at load, and reports each clip's MEASURED net turn beside it so the two can be checked.

Consuming it still needs an override — `Object::ApplyRootMotion` does nothing by design. The
archer's is a one-method `Skeleton` subclass that keeps the turn and drops the travel (Stage owns
where she is), passed to `GLTFLoader::GetSkeleton`'s `optional_target`, which fills in whatever it
is given rather than allocating its own. Add the clip's turn to the facing the game asks for rather
than replacing it.

**apps/animation and apps/isoanimation were deliberately not updated** (the user's call — they are
animation test beds and are checked by hand). They set no yaw flag, so clips there no longer turn
the character and their poses are now correct; one line per clip restores turning where a clip
means it. isoanimation's five `extract_horizontal_root_motion` lines are where it would go.

**Still asymmetric:** the root bone's rotation write ignores `animation_mask`, where
`ApplyIntervalOnto` respects it for every other bone. Matters for per-bone layer masking.

`tools/gltf_clip_dump.py` reads a .glb directly - clips with duration/travel/net turn/total yaw
movement, the rig, the material, the skin weights, and `--clip NAME` for the root track frame by
frame. No Blender, no engine, standard library only. The NET column says whether a clip is a pivot;
the YAW MOVE column says whether extracting would sweep its travel (a spin that returns nets zero
and still sweeps). A big number on a clip that plainly does not spin is a swing-twist artefact -
ill-conditioned when the hips leave vertical, Kick_Front reads -347 deg - and is harmless while the
clip's yaw is not extracted, because the recomposition is exact however the split behaved.

See [[archer-app]] and [[renderer-skinned-shader-null]], the other silent one from the same hunt.

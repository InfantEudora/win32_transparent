# Animation Root Motion & Orientation Blending

## The Core Problem

An animation like `TurnLeftInPlace` bakes a 90° rotation *into the bone data*. When it ends, the skeleton is rotated 90° in local space but the character's world transform hasn't moved. The next animation (e.g. `Idle`) starts from the reference pose — so there's a discontinuity.

## How Engines Solve It

**Root Motion Extraction** is the standard answer. The idea:
- Designate one bone (hips/root) as the "root motion source"
- Each frame, extract the *delta* between its current pose and the reference pose
- Apply that delta to the character's **world transform** instead
- Reset the bone back toward reference

This way the skeleton always stays near its reference pose locally, but the character moves/rotates in the world correctly. The next animation always starts from a clean reference.

The embryo of this already exists in `PlayerCharacter.cpp` at the end of `TurnLeftInPlace`:

```cpp
if (current_transition->f_hips_rotated){
    RotateBy(current_transition->hip_rotation);
    hip_bone->SetRotation(hip_bone->reference_rotation);
    hip_bone->animation_mask = 0;
}
```

The difference is that this is **hardcoded per-transition** via `f_hips_rotated` + `hip_rotation`. The general solution is to compute it automatically from the bone delta.

## Practical Generalisation

At the end of any non-looping animation flagged `f_end_orientation_different`, instead of a hardcoded quat:

```cpp
// Measure what the animation did to the hip bone
quat delta = hip_bone->GetRotation() * hip_bone->reference_rotation.inverse();

// Bake it into the character's world transform
RotateBy(delta);

// Reset the bone so the next animation starts clean
hip_bone->SetRotation(hip_bone->reference_rotation);
```

This means you no longer need to manually author `hip_rotation` on each transition — it's derived from the animation data itself.

## The Blend Window Problem

Even with root motion, there's still a **blend seam**: during the cross-fade, one animation is ending (rotated) and the other is starting (reference pose). Engines handle this by blending in the orientation-corrected space — you rotate the *incoming* animation's pose by the same delta so both are expressed in the same frame during the blend.

The existing `Lerp()` already lerps rotations with slerp, so if you apply the world correction *after* the lerp, the seam is hidden by the blend duration — which is exactly how Unity and Unreal handle it.

## Summary

| Step | What happens |
|------|-------------|
| Flag animation | Mark with `f_end_orientation_different` |
| End of animation | Extract hip delta vs reference rotation |
| Apply to world | `RotateBy(delta)` on the character root |
| Reset bone | `hip_bone->SetRotation(reference_rotation)` |
| Next animation | Starts from clean reference pose |

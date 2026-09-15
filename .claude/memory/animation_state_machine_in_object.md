---
name: animation-state-machine-in-object
description: "Object::ApplyAnimation owns the clip state machine (looping, one-shots, crossfades) since 2026-09-15; PlayerCharacter only adds root motion and its own layering"
metadata: 
  node_type: memory
  type: project
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T14:28:52.548Z
---

Moved 2026-09-15, at the user's request, out of `PlayerCharacter::ApplyAnimation` and into
`Object::ApplyAnimation`.

**Before:** `Object::ApplyAnimation` handled only `ANIMATION_STATE_LOOPING`, so any object that was
not a `PlayerCharacter` froze the moment it was asked to blend - `TransitionToAnimation` set
`ANIMATION_STATE_TRANSITION_START` and nothing advanced it. The whole ~200-line machine was a
`PlayerCharacter` private, tangled with `ProcessInputState()` and root-motion `MoveBy`/`RotateBy`.

**Now:** `Object::ApplyAnimation` owns looping, one-shots that hold their last frame,
`auto_continue_to`, crossfades and the rewind. Two virtuals are the seam:
- `ApplyRootMotion(delta)` - base does NOTHING. The delta is still computed, because computing it is
  also what poses the root bone; only the world movement is a character's business.
- `LoadDefaultPose()` - base resets every `Bone` below the object to its reference pose.

**The trap this closed:** `ApplyInterval` skips the root track (`SampleRootMotion` is meant to pose
it), and only `PlayerCharacter` called `SampleRootMotion`. So `SetRootBone` on a plain `Skeleton`
silently stopped that bone animating. `apps/isoanimation` calls `SetRootBone` right after
`AddAnimation`, so it reads like part of the recipe - it is only correct there because those are
`PlayerCharacter`s.

**Still not supported, deliberately:** retargeting a transition to a THIRD clip mid-blend.
`TransitionToAnimation` rewinds when asked to go back where it came from, and otherwise pauses.

**Gotcha for callers:** while a blend runs, `CurrentAnimationName()` is still the clip being LEFT.
Code deciding "am I already playing X?" must check `NextAnimationName()` too or it re-requests the
same transition every tick for the whole blend.

`Object.h` needs a `struct RootMotionDelta;` forward declaration - `Object.h` and
`ObjectAnimation.h` include each other, so whichever is reached first sees the other half-built.

The animation system is unfinished and the user expects to restructure it further; this was called a
starting point, not a conclusion. See also [[bomber-app]] and `apps/bomber/engine_notes.md` §13-14.

---
name: animation-state-machine-in-object
description: "Object::ApplyAnimation owns the clip state machine (looping, one-shots, crossfades) since 2026-09-15; PlayerCharacter only adds root motion and its own layering"
metadata: 
  node_type: memory
  type: project
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T14:57:15.810Z
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

**A clip does not need bones, a skin or an armature.** `LoadAnimation` builds one track per target
NODE and never asks whether that node is a joint; `AddAnimation` calls `LinkObjects(this)`, which
binds tracks by NAME against the object and its children. So a rigid prop animates through this same
path minus the skin - `apps/bomber`'s exit door is the first thing in the project to use it. Put the
clip on the PARENT, not on the thing that moves: `Scene::UpdateAnimations` walks
`renderer->objects`, and a child is drawn through its parent but is not in that list.

**A track writes an ABSOLUTE LOCAL transform** (`SetPosition`/`SetRotation`), so the animated object
must be one whose local transform nothing else owns - hence the door being an archway placed on its
cell with the leaf as a child. `Animation::SetPositionUpdates(track,false)` drops the position keys
if only rotation is wanted. Scale channels are SILENTLY DROPPED: nothing ever sets `f_scale`, so the
`debug->Fatal("TODO: Implement animation scaling")` in `ApplyIntervalOnto` is unreachable - which
matters because Blender's default keying set is LocRotScale and every clip in bomber carries one.

**The trap this closed:** `ApplyInterval` skips the root track (`SampleRootMotion` is meant to pose
it), and only `PlayerCharacter` called `SampleRootMotion`. So `SetRootBone` on a plain `Skeleton`
silently stopped that bone animating. `apps/isoanimation` calls `SetRootBone` right after
`AddAnimation`, so it reads like part of the recipe - it is only correct there because those are
`PlayerCharacter`s.

**The slots are `previous_animation` + `current_animation`, and CURRENT FLIPS TO THE DESTINATION THE
MOMENT A TRANSITION STARTS** (renamed same day; `transition_to` is gone). So `CurrentAnimationName()`
means "playing or becoming" in every state, `previous_animation` is non-NULL exactly while a blend
runs (it IS the "am I blending?" test, via `PreviousAnimationName()`), and the blend factor reads
0 = all previous, 1 = all current. There is deliberately NO `next_animation`.

`ANIMATION_STATE_LOOPING` is now `ANIMATION_STATE_PLAYING` - a one-shot being played was in that
state too, so the old name described the clip rather than the object.

**`Object::SetAnimationRate(float)` (added 2026-09-15) is how a clip runs backwards.** 1 normal, -1
backwards, 0 parked on the frame it is on and still posed every tick. It does NOT rewind, so a
reversal mid-clip reverses from where it got to - which is the whole reason a door needs no second
clip for shutting. A NON-ZERO rate un-pauses a one-shot that ran to its end (without that, reversing
out of an open door does nothing); rate 0 does not. It applies to `ANIMATION_STATE_PLAYING` ONLY -
a crossfade is a fixed-length blend and what "backwards" means for one is not obvious, so slowing a
walk down does not slow the blend into it. That is a decision, not an oversight.

**Still not supported, deliberately:** retargeting a transition to a THIRD clip mid-blend - refused
with a warning. Asking to go back where it came from still rewinds (TRANSITION_BACK), and that is
the one place the flip is not cosmetic: the rewind has to restore `current_animation` from
`previous_animation` when it lands. The clean fix for retargeting is a one-deep queue, agreed as
"when we feel we need it" - the user does not expect this game to need it.

`Object.h` needs a `struct RootMotionDelta;` forward declaration - `Object.h` and
`ObjectAnimation.h` include each other, so whichever is reached first sees the other half-built.

The animation system is unfinished and the user expects to restructure it further; this was called a
starting point, not a conclusion. See also [[bomber-app]] and `apps/bomber/engine_notes.md` §13-14.

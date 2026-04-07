# Animation State Machines

How game engines handle animations across different states (moving, crouching, swimming, stairs, etc.).

## Layer 1 — Simple State Machine (basic approach)
A flat graph of named states with transitions. Works fine for a handful of states but explodes when you multiply factors: `CrouchWalkForward`, `CrouchWalkLeft`, `CrouchWalkBackward`, `SwimForward`, `SwimLeft`... you end up with a combinatorial mess.

## Layer 2 — Blend Trees (Unreal/Unity standard approach)
Instead of one animation per state, you define a **blend space** — a 2D grid where axes are parameters like speed and direction, and the engine interpolates between animations placed at points on the grid.

```
         Strafe Left ←——————→ Strafe Right
                    ↑
              Walk Forward
                    |
              Idle (centre)
                    |
             Walk Backward
                    ↓
```

The character's velocity vector maps to a point in this space and the engine blends the surrounding animations. You only need ~5–9 animations to cover the full movement space. Unreal calls this a **BlendSpace1D/2D**, Unity calls it a **Blend Tree**.

## Layer 3 — Layered / Additive Animations
The environment state (crouched, swimming, on stairs) is handled as a **separate layer** stacked on top of the locomotion layer:

| Layer | Controls | Blend weight |
|---|---|---|
| Base | Locomotion blend tree (speed/direction) | 1.0 |
| Upper body | Aiming, holding weapon | 0.0–1.0 |
| Override | Crouch pose | 1.0 when crouched |
| Additive | Breathing, head sway | small |

Each layer only affects the bones it's assigned to — a crouch layer can bend the spine/legs without touching the arms. Upper body aiming can play independently of what the legs do.

## Layer 4 — State Machine + Blend Trees combined
Unreal's **AnimGraph** and Godot's **AnimationTree** combine both: the outer state machine handles discrete mode switches (grounded → swimming → climbing), and each state *contains* a blend tree for smooth locomotion within that mode.

```
[Grounded]──────[Swimming]──────[Climbing]
     |               |
 BlendSpace       SwimBlend
 (speed/dir)    (speed/dir)
```

Transitions between modes can be gated by conditions (entered water, grabbed ledge) and have their own blend time.

## Layer 5 — Motion Matching (newer, UE5 Lyra / Unity Motion Matching)
Skip the state machine entirely. Store a large database of animation clips. Each frame, find the clip in the database whose **pose and trajectory best matches** the character's current state and desired future path. Blends continuously. Produces very natural results but requires a large motion capture library.

---

## Application to this codebase

`AnimationGraph` is already Layer 1. To reach Layer 2, add a `BlendSpace` node that takes speed/direction floats and outputs a blended pose. The existing `Animation::Lerp()` is the primitive needed to implement that.

For environment states (crouch, swim, stairs) the cleanest addition to `CharacterState` would be an enum:

```cpp
enum LocomotionMode { LOCO_GROUND, LOCO_CROUCH, LOCO_SWIM, LOCO_CLIMB };
LocomotionMode locomotion_mode = LOCO_GROUND;
```

Each mode selects a different `AnimationGraph` (or a different sub-graph within one), and transitions between modes have their own blend time. That gives you the Unreal outer-state-machine behaviour without needing a full blend tree first.

# Archer Straw Man Plan

A kicking dummy: `straw_man` out of `archer.glb`, stood on a spring so a kick sets it wobbling and
it rights itself. Agreed 2026-09-26. Step 1 (the rigid version, in the range) is being built
first; the rest is the plan.

**It is the opposite of the archery targets.** A stand scores the arrows and shrugs off a kick; the
straw man scores the KICK - one point per kick that connects - and an arrow only sticks in it and
sets it swinging. She can walk straight through it.

---

## 1. The model

One unskinned mesh, origin at the foot of its stake, measured straight out of the .glb (mesh units,
x across, y up; at the character's `model_scale` of 2.02 multiply by that):

| part | y | x | world height |
|---|---|---|---|
| stake | 0.00 .. 0.36 | +-0.025 | 0.00 .. 0.73 |
| body + skirt | 0.36 .. 0.52 | +-0.085 | 0.73 .. 1.05 |
| arms | 0.52 .. 0.72 | -0.216 .. 0.241 | 1.05 .. 1.45 |
| head | 0.76 .. 0.96 | +-0.07 | 1.53 .. 1.95 |

1.95 tall against her 1.80, 0.17 deep. The arms are slightly asymmetric; the colliders follow them.

## 2. The mechanism: one spring hinge at the foot

A dynamic body jointed to the world at ground level, turning about **Z**, the camera axis - so every
wobble is in the screen plane, which is the one a side view reads. A torque spring pulls it upright;
hinge limits at +-65 degrees stop a hard kick folding it into the floor.

**Why the foot, and not the top of the stake.** The mesh is one piece. A pivot anywhere up the
stake swings the stake's lower end through the ground the opposite way. At the foot the whole thing
tips like a pole in soft soil, which reads; a bend belongs to step 3, with bones.

**Why a hinge and not a ball-and-socket with locks.** The hinge IS the plane constraint - it leaves
exactly one degree of freedom, the one the spring acts on, and the axis locks every other prop
needs are unnecessary.

### Tuned by frequency and damping ratio, not stiffness

`k` and `c` depend on the body's inertia, which depends on its mass, its colliders and its scale -
three things that change for reasons unrelated to how the wobble should feel. What should feel a
certain way is the wobble, so that is what gets typed:

    I     = inertia about the hinge axis through the pivot (measured off the body)
    w     = 2 pi f
    k     = I w^2 + k_gravity
    c     = 2 zeta I w

**`k_gravity` is the trap.** The pivot is BELOW the centre of mass, so this is an inverted pendulum:
gravity tips it further by `m g d` per radian (d = height of the centre of mass over the pivot).
Leave that out and the dummy rings slower than it was set to, and a soft setting falls over on its
own - the rope's inverted-pendulum lesson again (see archer_app memory). Folded into `k`, the
frequency typed is the frequency seen. For a hanging pendulum the same term comes out negative and
the formula still holds.

Starting point: **1.3 Hz, zeta 0.12** - the amplitude roughly halves every wobble (e^(-2 pi zeta)
= 0.47), four or five visible swings, about three seconds to rest.

## 3. The core piece: `core/physics/SpringHinge`

This is the third hand-rolled spring in the repo - `apps/ship/HingedDoor` (`-k angle - c rate` as a
torque) and the pinball plunger (the same as a force). So it goes into core:

- builds the `rp3d::HingeJoint` from a world pivot and axis, with optional limits;
- anchors to a given body, or to **the world** (NULL: it makes and owns a collider-less static
  body at the pivot);
- measures `I`, `m` and `d` itself at construction, so frequency + damping ratio is the whole API;
- `Tick()` once per simulation tick, before the step, applies the torque (rp3d clears forces after
  every step, so this is re-evaluated against the new angle each tick, exactly as the door does);
- reads its own angle from the two bodies' orientations, signed about the axis, rather than trusting
  which way round rp3d signs `HingeJoint::getAngle()` - the door does the same, for the same reason;
- turns sleep off on the swinging body: a sleeping body ignores its torque.

Moving the door and the plunger onto it is a follow-up, not part of this.

## 4. The rules (`Stage`)

The rules need to know only two things, and both are small:

- **`PROP_STRAWMAN`** - a kind of its own, NOT a `TargetVariant`. `UpdateTargets` marks any target
  "knocked over" past `TARGET_KNOCKED_DEG` and drops it from the obstacles; a dummy mid-wobble would
  trip that.
- **A non-blocking obstacle.** The kick finds props through the obstacle list, so the dummy must be
  on it - but she walks through it. `StageObstacle::f_blocks` (default true): the X pass, the Y pass
  and `CanStandAt` skip an obstacle that does not block; the kick sweep does not. `make rules`
  checks both halves.

## 5. The app

- **Built** like the archery stand: object at the feet, colliders typed from the measurements above
  (stake box from 0.15 up so it never rests on the floor it is hinged to; torso box; arms box),
  `STRAW_MASS` set after the colliders.
- **Collides with** the level, props and debris - a kicked crate knocks it - but **not the archer**.
  Her kinematic body is in every other prop's mask, which is how walking shoves a crate; leaving it
  out is what makes the dummy passable physically as well as in the rules.
- **The kick** is one tick of force at the boot's height, `AddWorldForceAt`, the way an arrow hit is
  applied - not a velocity set. The hinge turns a force at a height into a spin, lever arm and
  inertia included, which is the whole reason for having it.
- **Arrows** need nothing: `ResolveArrowsAgainstProps` already pushes any dynamic prop at the hit
  point and sticks the arrow to it, so a shot into the head swings it and the arrow rides along.
- **Score:** each connecting kick is +1 on the dummy, shown as a popup of its running count (the
  plain boards' popup), and summed into `kick_score` in `archer_state` and the panel.
- **Placement:** the range, at x 3 - a few steps right of where she starts.
- **The joint lives on the `PropView`**, so it moves with the level when scenes are swapped, and is
  destroyed in `NewGame` before the body it holds.

## 6. Steps

1. **Rigid, in the range** - everything above. **BUILT 2026-09-26**, measured in-app over MCP
   (`archer_state` now carries `kick_score` and a `strawmen` array with the angle both ways, the
   rate and what the spring measured):
   - SpringHinge made I 10.58, k 844 of which gravity 138 (the props' gravity is 18, not 9.81,
     so d = 1.28), c 20.7. **It rings at 1.30 Hz, exactly as set** - the gravity term is doing its
     job - and loses about 0.44 of its swing per cycle against the 0.47 zeta 0.12 predicts.
   - GetAngle and rp3d's joint angle agree to 0.01 degrees, so the limit signs are right.
   - Peak swing per kick: low 25.6, front 28.2, high 29.9 degrees - the near-equality predicted
     in step 2, now measured.
   - She walks from x 1.0 through it to 3.97; it moved 0.003 degrees.
   - Three arrows stick in its head and chest and swing it (-8.6 after one, -14 after two);
     neither score moves.
   - Restart and a scene round trip rebuild it with its spring; the next kick behaves the same.

   Panel sliders `straw hz` / `straw damping` / `straw kick` retune live (TickSprings notices).
2. **Feel** - tune f / zeta / the kick force by hand in the panel; decide whether the three kicks
   should differ. Today's spec speeds times boot heights come out nearly equal (13 x 0.65,
   17 x 0.45, 9 x 1.0) - so as crate-tuned numbers they swing a dummy about the same. A fixed boot
   impulse, with height alone deciding, would make the high kick the big one.
3. **Bend, if it reads as a plank** - the user rigs a small skeleton (stake -> torso -> neck, one
   bone per arm). Physics stays ONE hinge; the view spreads its angle along the spine bones and
   lets the arms lag it as a second-order follower - floppy arms for almost nothing. The Stage /
   Puppet split again: the solver owns the motion, the bones are slaved to it.
4. **Later, maybe** - a hard enough kick snaps it off the stake (the hinge is destroyed and it
   falls as a loose body); a hit sound off the limit stop; the door and plunger moved onto
   `SpringHinge`.

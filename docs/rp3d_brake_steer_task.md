# Task: braking while steering yaws the vehicle the wrong way

For whoever is working in `C:/code/reactphysics3d` on branch `vehicle-constraint`.

Written from the game side (`win32_transparent`), which drives the constraint through
`core/Vehicle.cpp` and links `libs/libreactphysics3d.a`. I did **not** edit the fork: your
working tree has uncommitted changes in the three files this touches
(`src/systems/SolveVehicleSystem.cpp`, `src/constraint/VehicleConstraint.cpp`,
`include/reactphysics3d/constraint/VehicleConstraint.h`), so this is a description rather than a
patch, to avoid colliding with work in progress.

## Read this first: keep the header and the .a in step

Currently they ARE in step — `libs/libreactphysics3d.a` and the game's copy of
`VehicleConstraint.h` are both stamped 2026-09-07 21:27 and the game runs, so whoever last
rebuilt did the right thing. This note is about keeping it that way, because the struct in
question is unusually easy to break.

Your header adds two `decimal` fields to `VehicleWheelSettings`:

```c
decimal slidingFrictionRatio;
decimal peakSlipRatio;
bool    enableSuspensionForcePoint;   // <- inserted immediately before this
```

That is the **same insertion point** that cost a debugging session on 2026-09-07. Verified by
`offsetof` against both copies: `enableSuspensionForcePoint` sits at 124 and
`sizeof(VehicleWheelSettings)` is 152, where before these two fields it was 116 and 144. The game
compiles against its own copy of the headers in `3rdparty/reactphysics3d/`, so if the `.a` is
rebuilt and that copy is not updated in the same step, the library reads `numContactSamples` out
of the game's `contactSampleHalfAngle` — the float `0.785398f` reinterpreted as `uint32` is
**1,061,752,795**, which lands directly in the raycast loop in `updateWheelContact`. The app hangs
on a white frame with no error and nothing in the log.

So: **whenever you rebuild the library, copy the changed headers into the game's
`3rdparty/reactphysics3d/` in the same step** (converting CRLF -> LF; the game's `.gitattributes`
is `text eol=lf`). Appending new fields at the END of the struct rather than the middle would make
this class of break far less likely and costs nothing here.

## The bug

Set the game's buggy up as:

| setting | value |
|---|---|
| suspension | 3 Hz all round |
| brake bias | 1.0 — only the front wheels brake |
| power split | 0.0 — rear wheel drive |

Accelerate straight to ~3 m/s, release the throttle, then brake and steer at the same time.

**The car turns hard AWAY from the steering input** — steering left yaws it right at
**-1.19 rad/s (-68 deg/s)**. Expected is either a mostly straight slide (understeer, front tires
saturated) or a gentle turn towards the steer. A hard turn the other way is not either of those.

Reproduce from the game repo with:

```
python tools/brake_steer_test.py            # the reported case, exits 1 while it reproduces
python tools/brake_steer_test.py --sweep     # brake-bias sweep, prints the diagnosis
```

## Evidence

Sweeping the brake bias moves the front tires from "cornering normally" to "no lateral force at
all", and the yaw flips sign exactly as it does:

| brake bias | peak yaw | front lateral force | front slip angle |
|---|---|---|---|
| 1.00 | **-1.19** | 0.0 N | 63.8 deg |
| 0.80 | **-0.70** | 18.4 N | 43.0 deg |
| 0.60 | +0.78 | 36.9 N | 2.7 deg |
| 0.50 | +0.90 | 14.7 N | 2.9 deg |

The row that matters is the first: the tire is slipping **63.8 degrees** and producing **exactly
zero** sideways force. It is asked to corner as hard as it possibly could be, and has nothing left
to do it with.

(Slip angle is read through `VehicleWheel::getLateralSlipAngle()`, which the game now surfaces per
wheel — `lateral_force` alone cannot tell "not asked to corner" from "asked and starved".)

## Mechanism

All of the code below is **committed**, not part of your uncommitted changes — your `mGripScale`
work multiplies into these limits but does not change the argument.

1. `initWheel` builds the friction basis from the **wheel's heading**: `mContactLongitudinal`
   along where the tire points (steered), `mContactLateral` across it.
2. `solveLongitudinalFriction` runs **before** `solveLateralFriction`, and the two share one
   budget through `remainingGripFactor`.
3. With all braking on the front axle, the brake asks those tires for more than the contact patch
   can transmit. Longitudinal consumes the budget; `remainingGripFactor` returns ~0, so
   `gripLimit` and therefore `maxImpulse` in `solveLateralFriction` collapse to zero. Hence 0.0 N
   of lateral force at 63.8 degrees of slip.

Zero cornering force by itself would only be understeer. **The reversal comes from where the
surviving force points.** It stays pinned to `mContactLongitudinal`, the *steered* heading. A
braking force `F` along a heading rotated by the steer angle `d` has a sideways component
`F·sin(d)` pointing **out of the turn**:

```
force in body axes = -F·cos(d)·forward  -  F·sin(d)·left
                     \_____________/       \____________/
                      slows the car          pushes the nose
                                             AWAY from the steer
```

Applied at the front axle, ahead of the centre of mass, that is a yaw moment away from the steer.
At 136 N and 35 degrees of lock it is ~78 N per wheel; over the front pair, against the buggy's
yaw inertia, that is the right order for the -1.19 rad/s observed.

A real tire that is sliding does not do this. Its friction opposes its **slip velocity**, which
here points almost straight down the road, so the sideways component — and the reverse yaw — is
almost nothing.

## Suggested fix

Make the friction direction follow the slip velocity as the tire saturates, instead of staying
locked to the wheel heading regardless of how much it is actually sliding.

The cheapest form that keeps the existing two-axis solver: blend the basis used for the friction
solve from the wheel heading towards the contact-plane slip velocity direction, by how far the
tire is past its grip limit. You already compute the ingredients — `mCombinedSlip` is exactly the
"how far past the limit" measure, and the slip velocity is already computed in `initWheel` for
`mLateralSlipAngle`. At `mCombinedSlip` near 0 the basis is the heading (rolling tire, current
behaviour, unchanged); fully sliding, it aligns with the slip velocity so the total force simply
opposes the motion.

Worth checking as part of this: `solveLongitudinalFriction` always running first gives that
direction first claim on the budget, and the alternating solve has a degenerate fixed point —
lateral 0 lets longitudinal take everything, which keeps lateral at 0. Splitting the budget by
slip magnitude rather than by solve order would make the split independent of iteration order.

**Note on your in-flight work:** `gripScaleAtSlip` / `slidingFrictionRatio` changes how *much*
grip a sliding tire has, not which *direction* the force acts in. At 63.8 degrees of slip it
should make the reverse yaw somewhat weaker, but the sign will not change — the force is still
pinned to the steered heading. The two changes are complementary, not alternatives.

## Acceptance

`python tools/brake_steer_test.py` (game repo) should end with the car yawing **into** the steer,
or at worst sliding roughly straight — anything but a hard turn the other way. The `--sweep` run
should not flip sign across the bias range. Please also re-run
`python tools/vehicle_mcp.py baseline --vehicle buggy` and the tank baseline before/after, since
this touches every tire in the solver, not just braking ones.

## Loose end, not part of this

A `brake_split_front = 0.0` run gave `+1.48 rad/s` once and `+0.00` on a repeat, with the front
wheels reporting no ground contact at all in the second. That looks like a separate issue
(possibly the nose lifting under rear-only braking, or non-determinism in the settle) and is
outside the case above, but may be worth a look while you are in here.

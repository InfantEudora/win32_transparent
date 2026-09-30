# Plant Mechanics Plan

Plants she USES rather than walks past: a tree climbed by its arms, a leaf that bends under her
and flings her, a thin branch she has to keep her balance on - and a handful of smaller ideas that
fall out of systems already built. Talked through 2026-09-26; nothing here is built yet except
where a section says so.

The vines (`vine_plan.md`, `Vine.h`) are the starting point, and most of the MESH work reuses
them. What is new is that these plants are gameplay, and that decides where they live.

---

## The split: gameplay plants are declared in the rules

Today's vines are view-only. `DeclareVines` hands the app a curve, the app builds a mesh, and
`Stage` never hears of them - correct for decoration, wrong for anything she stands on.

A tree she climbs, a leaf she lands on and a branch she walks along have to be declared in
`Stage`, for the same three reasons everything else she touches is:

- **Deterministic and replayable.** A recording replays the rules; a platform the rules do not
  know about is a platform a replay walks through.
- **Testable without the engine.** `make rules` can then PROVE a layout is climbable - every arm
  within a jump of the next - rather than someone finding out by playing it.
- **One source for both sides.** The rules turn the declaration into collision; the app turns the
  SAME declaration into the mesh. The terrain already works this way round (its field comes from
  the blocks), and it is what stops the drawn arm and the arm you stand on drifting apart.

So each plant below is a small struct in `Stage.h`, built into `blocks` (or a new surface type)
by the rules, and read by the app for the mesh. The view side leans on `Spline`, `SplineDeform`
and the vine's station placement; the rules side is where the new work is.

---

## 1. The tree trunk with arms

The easiest of the three, and mostly content. The reference is the stump in the export: a
tapering trunk, a flared root, a cut top, stub branches.

**Rules.** A declaration along the lines of

```
struct StageTree{ float x, base, height; std::vector<StageTreeArm> arms; };
struct StageTreeArm{ float y; float side; float length; };
```

that emits one `BLOCK_PLATFORM` per arm: one-way, jumped up through from below, stood on, dropped
through with Down - all of which the platform at x 23.5 already does. The arm's platform is
exactly as long as the drawn arm reaches, so the arm length is MEASURED off the model at load and
checked against the declaration, the arrangement `KICK_TICKS` and `LEDGE_CLIMB_INSET` use.

**Spacing.** Her jump peaks 3.2 above takeoff (`ARCHER_JUMP_SPEED` against `ARCHER_GRAVITY`), so
arms 2.5-2.8 apart, alternating sides, make a zig-zag climb with a little slack. A rules test
walks each tree from the ground to the top arm the way the existing ones reach the gap's far lip.

**Arm tips as ledges.** Optionally the outer end of an arm advertises a grab, so a jump that
comes up short catches the arm and climbs onto it - the ledge hang and climb, already built.

**Mesh.** The stump splits naturally into a root flare, a repeating trunk section, a stub arm and
a cut top. The trunk section is a tile repeated up a line - what a vine trunk is - with a slight
lean or curve if wanted; the arms are pieces placed at the declared heights, the way the vine
places leaves at stations. Like the vine, it builds from placeholder geometry until the pieces
exist in `archer.glb`, so the blockout plays before the art does.

**Decided default, open to change: the trunk stands BEHIND her in depth**, not solid. She climbs
past it, arm to arm, and the trunk never blocks a jump. A solid trunk (a wall she bumps into) is
the alternative; it reads less like a tree and complicates every jump near it.

**Blockout: BUILT 2026-09-26.** The main level's ground was run out to x 100 for it (later to
141, for the spring plants below). The tree is at x 80: a trunk 11
tall, three arms 2 long - tops 2.5 right, 5.0 left, 7.5 right - between a low slab on the left
(x 70..76, top 5.5, reached from the second arm) and a high slab on the right (x 84..97, top 10.2,
reached from the third). Neither slab can be reached from the ground, even with a grab.

- `StageTree` / `StageTreeArm` in `Stage.h`, declared in `BuildMainLevel`; `Stage::BuildTrees`
  appends each arm as a `BLOCK_PLATFORM` after the level's own blocks (so declared blocks keep
  their indices) and tags it with `StageBlock::tree`.
- The app draws the trunk as a plain box at `STAGE_TREE_Z`, behind her walk line, in its own dim
  bark brown (`ar_trunk`); the arms draw as the one-way blue they are.
- `stage_test` (TestTree) checks every arm's shape and that it covers the play plane, then CLIMBS
  it with plain inputs: straight up through the first arm, across to the second, onto the low
  slab, back up to the third, onto the high slab, and Down back through an arm. The level check
  that measures every platform against the ground skips the arms - they are reached from the arm
  below. The same climb played in the game lands on 2.50, 5.00, 7.50 and 10.20.

Next for the tree: its mesh from the stump's pieces, and the arm length measured off the model.

**The bigtree on the cave roof: BUILT 2026-09-30**, the first tree drawn with the art. The one at
x 80 stays the blockout, and so does its stretch of the level. At x -54 on the roof (top 11), with
the pad beside it drawn as `mushroom_big`:

| piece | where |
|---|---|
| arms | tops 16.5 right, 19.0 left, 21.5 right |
| cut top | 23.5, 0.97 wide: somewhere to stand, the highest point over the bay |
| pad | cap at x -52.1, top 12.15, 2.19 wide, under the first arm's tip |

- **The pieces** are `bigtree_bottom`, `bigtree_segment` (repeated), `bigtree_top`,
  `bigtree_arm_1` (it grows right) / `_2` (left) and `bigtree_arm_decorative_1` / `_2`.
  `BuildBigtree` fits whole segments between the bottom and the top, stretched in y to land the cut
  face on the declared height. It puts each arm's walkable top on its arm's, and each decorative arm
  in the tallest free stretch on its side. The export was clean: no node scale or rotation, and the
  pieces stack as authored (bottom to 0.75, segments 1.0 apart, arms on the trunk's axis).
- **The arm length is measured off the model**, as planned: `Stage.h`'s `BIGTREE_*` and
  `MUSHROOM_BIG_*` are the model's sizes at her scale, and `BuildPlantModels` measures them again at
  load and warns if a re-export moves them. It does not correct them, because the rules own what
  she stands on.
- **The way up**: a running jump left off the island onto the roof, then the pad. The first arm is
  past a plain jump off the roof (feet 14.2, hands 16.0) and off the cap, so only a bounce timed
  to the rebound gets there. Then the arms, and a 2.0 hop onto the cut top.
- **Deviations from section 1:** the first arm is reached from the PAD, not the ground. The tree
  has a standable cut top (`StageTree::top_width`, a one-way platform tagged with the tree like an
  arm), which section 1 did not plan. The mushroom squashes about its foot instead of sliding down
  as the blockout cap does, since its stalk stands on the roof.
- `stage_test` (TestRoofTree) checks the shapes, that the first arm is out of reach without the
  bounce and in reach with it, the whole way up from the island (solved and replayed, every hop
  with a window of 10 ticks or more), and Down back through an arm.

---

## 2. The bouncing leaf (and a bounce pad first)

The richest of the three, and worth building generally, because its parts are reused by every
springy or tilting thing that comes after it.

**The model.** The leaf is a spring in the rules: an angle and an angular velocity, pulled back
toward its rest angle, damped, stepped once per tick. Her weight pushes it down while she stands
on it, and her landing adds her fall speed to that push.

**Sliding off.** The leaf's surface is a tilted segment she stands on. Past a friction threshold
the slope wins and she slides toward the tip and off - slowly, as it sags, which is the point.

**The fling.** A jump while the tip is on its way back UP adds the tip's upward speed to hers.
That is a diving board: land, sink, jump as it rebounds. Timing it is the skill; a mistimed
bounce is an ordinary jump, never a punishment.

**New ground for Stage.** The rules know flat, axis-aligned boxes. This needs a surface under her
feet that has a slope and a velocity of its own. Build that generally - a "surface she is standing
on" with a slope and a speed - and seesaws, bouncy mushrooms and sagging branches follow from it.

**A bounce pad first.** A mushroom that is the same spring with no tilt and no slide. It isolates
the fling - how much of the rebound transfers, how the timing window feels - before the leaf adds
the bending and sliding on top.

**The forecast helps.** `Stage::PredictLanding` already sees the landing coming, so the landing's
anticipation plays onto the leaf as it does onto the ground; the same copy-and-tick forecast can
tell the Puppet when the rebound will peak, so a crouch can meet it.

**Animation.** The leaf: a small skinned mesh with bones down its midrib, like the rope, bent by
the spring's angle, with the loose-leg chain code (`core/DynamicChain`) adding a wobble along it.
Her: a stance for standing on a slope, and a slide.

**Blockout:** the far right of the main level, starting as a tilting box before the leaf exists.

> `core/physics/SpringHinge` (the straw man's, 2026-09-26) is an rp3d hinge with a restoring
> torque, TUNED BY FREQUENCY AND DAMPING RATIO rather than k and c - and that is the idea to take
> for the leaf: ask for how the swing should feel, work the constants out from it. The leaf's
> spring itself belongs in the RULES, not in reactphysics3d, for the reasons in *The split*.

**Blockout: BUILT 2026-09-26**, past the tree's high slab. The ground runs to x 141 and the end
wall moved to 140. The pad is at x 105 (cap 2.4 wide, top 1.2), the shelf at x 110..118 (top
7.5), the leaf grows right off the shelf's end (4.5 long, resting 10 degrees up) and the canopy is
at x 125..133 (top 13.0).

- **One spring, two shapes.** `StageSpringPlant` in `Stage.h` has one coordinate `q`: the cap's
  sink for the pad, the angle for the leaf. The landing, the ride, the throw and the collision
  are written once for both. A leaf differs only in its lever (her distance out from the stem)
  and its slope.
- **Tuned by feel, the SpringHinge idea.** Each plant gives `give` (how far her weight moves it
  at rest), `hz` (its swing with nobody on it) and `damping` (a ratio). Stiffness, inertia and
  damping are worked out from those three. While she stands on it, her weight and her mass (her
  lever squared) join the spring, which is why a leaf with her near the tip swings slowly.
- **Landing, riding, leaving.** A fresh landing hands the plant her fall as shared momentum. She
  then rides at its speed, so when it springs back and slows faster than gravity slows her, she
  leaves it on her own: the rebound hop. A jump keeps whatever she is already rising at and adds
  the jump on top: the fling, capped at `SPRING_MAX_LAUNCH`. The coyote window doubles as grace
  for pressing a moment late. The jump cut shortens the jump part and never the throw
  (`launch_lift`).
- **Slide and step.** Past `SPRING_LEAF_SLIP_DEG` (14) she slides down the leaf, with a little
  control (`SPRING_LEAF_SLIDE_CONTROL`). While standing on a plant she steps up 0.4 onto a block,
  so a leaf bent below its shelf still lets her walk back onto the shelf.
- **Not blocks.** Collision for the plants runs once per move, after the blocks, comparing
  surface-then against surface-now, so nothing tunnels. Crates and arrows pass through both
  plants.
- **The app** draws the cap on a stalk and the leaf as a thin slab turned about its stem
  (`SyncSpringPlants`, every tick). `archer_state.spring_plants` reports which plant she is on,
  the throw, the slide and each spring. An input recording captures and restores the springs.
- **stage_test (TestSpringPlants)** plays every claim:
  - The pad: a landing sinks the cap 0.84 of its 1.10 travel and she rides it down. Left alone
    she hops 1.04 and settles at the give. A jump timed to the rebound peaks at 9.19 and lands on
    the shelf. A jump already waiting as she lands peaks at 4.27. A press 3 ticks after the throw
    still flings. A tapped fling (4.98) comes in between no jump (2.24) and a held one. The
    landing forecast lands her on the pad on the right tick.
  - The leaf: near the stem it holds her. Walked out, it bends to about -21 degrees and slides her
    off the tip, then springs back to rest. A timed bounce peaks at 14.16 and lands on the canopy,
    against 11.03 untimed. Walking back steps her up onto the shelf.
  - The same pad and leaf runs played in the game give the same numbers to the hundredth.

**The timing, measured (2026-09-26).** A recording of the user bouncing on the pad, replayed
through the rules, showed presses 4–8 ticks after landing, about when the cap bottoms out. The
best moment is 12 ticks, when the cap passes back through rest going up. The window that reaches
the shelf is 4–8 ticks wide. The narrow window stays: timing is the skill.

**The timing cue: BUILT.** While a jump would add anything, the plant is tinted green through
yellow to red. The tint shows `SpringBoostNow`, what a press now adds, as a share of
`PredictSpringBoostPeak`, this bounce's best, found by forecasting a copy of the Stage. Red is the
best tick. The cap's own colour moved off red to a lilac, so red only ever means now.
`archer_state.spring_plants.cue` reports the same number. In game, pressing on the first red tick
lands her on the shelf.

**Pumping: BUILT**, the swing's trick, on the aim axis (Up/Down, or the right stick):

- **Stomp.** Down held through the last `SPRING_STOMP_TICKS` (10) of the fall lands her up to 30%
  harder into the plant. Let go early and it doesn't count. A full stomp sinks the cap 1.05
  instead of 0.84, and the timed jump peaks at 10.58 instead of 9.19.
- **Swing.** An Up press in the first ticks after the plant lets go of her adds 25% of the throw,
  full for 3 ticks and fading out by 10. It must be a fresh press; Up held from before the release
  does nothing. It lifts a plain rebound from 2.24 to 2.80.
- Stacked, a stomp, a timed jump and a swing peak at 11.04. `SPRING_MAX_LAUNCH` (28) is what now
  limits stacking, and is the knob to raise if pumping should pay more.

Known and left for tuning: a jump pressed while the cap is still going down can be caught by the
empty cap springing back up (it returns at about 27 u/s against her 15.7), which ends the jump
early.

Next for these: meshes (the leaf is a skinned midrib, like the rope), and her stance and slide
animations; a crouch that meets the rebound, forecast the way the landing is.


### Sliding, and the slide gallery (2026-09-26)

The leaf slides her off, but a leaf is a bad place to TUNE a slide: its angle changes under her
while she slides, so a slide that feels wrong could be the slide rule or the spring. So the rule is
tuned on RAMPS - a leaf held still - at fixed angles, in a gallery of its own.

**One surface list.** The pads, the leaf and the branches were each their own copy of one test (land
from above, stay on while walking the slope, leave). They are now one `StageSurface` per candidate,
gathered by `Stage::GatherSurfaces` and resolved by `Stage::CollideSurfaces`; what differs lives in
the fields - a plant's surface moves (`top_then`, `vel_y`), a branch lets her through after a fall,
a ramp is `f_ground`. The highest surface she lands on wins. Checked by diffing the whole rules
output before and after: identical.

**Ramps** (`StageRamp`, `Stage::ramps`) are ground, not beams: Down does not drop through one,
walking off a block onto one keeps her feet on it (reaching a body-width further, since the block
holds her box up until her centre is half a body past its edge), and walking up one into the block
at its top steps her up. Past `slip_deg` - the leaf's 14 unless the level says otherwise - she
slides, by `Stage::SlideAccel`, the same rule as the leaf.

**The gallery**, left of the rope level's shallow pit (the rope level's left wall moved to x ~-150):
- six hills at 8, 14, 18, 25, 35, 50 degrees, 1.5 high, each an ascent, a top with a sign, and a
  descent; the 25 and 35 meet with no floor between (the V);
- stairs to a block 6 high and a 25 degree LONG RUN of 12.9 down its far side;
- a 25 degree DROP off the floor's end over a 2.5-deep pit.
Every ramp is sealed by blocks at its ends. Drawn as slate slabs (`ar_ramp`); she turns icy blue
while she slides (`ar_archer_slide`), standing in for the slide clip. `archer_state.slide` reports
the ramp, the slope under her feet and the pull.

**What it found, and what changed** - measured before touching the rule:

| slope | before: stood still for 3 s | after |
|---|---|---|
| 18 | crept 0.14 | slides to the foot in 75 ticks, top 3.6 |
| 25 | crept 0.37 | 38 ticks |
| 35 | crept 0.64 (0.21/s) | 24 ticks |
| 50 | slid, 1.7/s | 16 ticks |

- **The slide had friction twice.** The pull already nets out the friction that holds her at the
  slip angle; the air friction (14 u/s^2) was applied on top whenever no key was held, and cancelled
  it on everything short of about 45 degrees. Now no friction on top of a slide.
- **Steering beat every slide.** Input on a slide was 0.35 of the AIR accel (19 u/s^2), more than
  the pull of any slope under 50, so she walked up 35 degrees at nearly full speed. Now pushing UP a
  slide is 0.35 of the PULL - it slows the slide and cannot climb it; pushing down or across is
  still the air share, because a pull just past the angle is near zero and a share of it left her
  unable to walk down a ramp at all.
- **A top speed**, `SLIDE_MAX_SPEED` 14 along the slope (her run is 9), since without friction on
  top a long slide gathers speed for ever. The long run reaches it: 12.69 horizontal at 25.
- **A hop at every ramp's foot**: her feet left a hair above the floor, gravity did not reach it,
  she flew a tick. The surfaces' keep-on reach now also snaps her to a block she runs out onto.
- At exactly 14 the atan put the slope a millionth over the angle - a slide with no pull, and no
  steering; a 0.001 degree tolerance.

All 600 existing rules checks, leaf and pad and branch included, pass unchanged; TestSlideGallery
adds 9 (sealed ramps; held at and below the angle, slides above it, faster when steeper; from rest
climbs what she can stand on and slides back down the rest; never airborne walking down any hill,
Down held or not; the long run's top speed; off the drop into the pit).

**Open, for feel - what to try in the gallery:**
- **She stops dead at the foot of a slide.** Off the long run at 12.7 onto flat ground the run
  friction (120 u/s^2) stops her inside a few ticks. A skid - a slide's speed bleeding off at a lower
  rate on the flat - would carry it on.
- **A run-up carries her over any hill short of 50**: she arrives at the foot at 9 and coasts up.
  Physical, and possibly right; the alternative is that a slope past the angle zeroes her uphill
  speed on contact.
- **Jumping out of a slide** is the ordinary jump, straight up, keeping her speed along x. Off the
  surface's normal instead would kick her away from a steep slope.
- **Walking up a gentle slope** is at full run speed. Slower uphill is one line if wanted.
- **Crates and arrows pass through ramps**, as they do the plants: there are no crates in the rope
  level, and a ramp needs a rotated static rp3d box (and a segment test for arrows) when they are.
- **The terrain** builds its shape from boxes; an oriented box in the SDF would let it melt ramps
  too, so they need not stay blockout.

---

## 3. The thin branch and balance

The tightrope that was parked earlier. The code is not the hard part; the design is.

**The side-view problem.** In a side view, falling off a branch happens toward or away from the
camera - the one axis the view barely shows. Two workable answers:

- **Lean along the branch** (forward/back, in the screen plane). Perfectly readable, arms
  windmilling - but not how anyone falls off a beam.
- **Lean toward or away from the camera**, shown by her pose and a small balance arc above her,
  corrected on the up/down axis. **The pick:** it is the true axis, and the arc makes it legible.

**The model.** An inverted pendulum. The lean grows on its own, and faster the faster she walks
or the more the branch bounces; the player's correction pushes it back. Keep moving steadily and
she recovers; overcorrect and she tips the other way.

**Forgiving failure.** Tipping over is a catch, not a death: she grabs the branch and hangs from
it, like the ledge or the rope, and climbs back up. Kinder, and built from parts that exist.

**The mesh is where the vine system pays off.** A branch is a vine trunk along a curve between
two anchors, sagging under her position, drawn with the rope's skinning - leaves included.

**A tie-in with the bow.** The aim sway already widens standing and narrows kneeling. On a branch
it could grow with her balance: shooting from a branch is possible, and risky.

**Animation.** Balance poses (arms out, a lean each way, a recovery), best as an overlay on the
walk - the whole-body overlay built for the fall pose is the mechanism.

**The clips (2026-09-26), read from `archer.glb` by posing the skeleton:**

- **`Balance_Walking`** (5.7 s) is a slow walk forward, about 0.9 world units a second, with her
  feet placed on one line. Its first frame has the feet together, which serves as the standing
  pose. The arms are held out to her sides at shoulder height, almost static. In our side view
  that points toward and away from the camera, so the arms are foreshortened.
- **`LosingBalance`** (6.0 s) is on the spot with the feet apart, so its legs are unusable on a
  branch. It starts and ends at rest. The wobble runs from 0.8 to 4.5 s: the arms windmill, and
  the lean is mostly forward (0.15 rig units at the head) and only 0.05 sideways.
- **The composite:**
  - Legs from `Balance_Walking`, pinned to the distance walked.
  - Upper body starting on its arms-out pose and blending toward `LosingBalance`'s windmill as
    the danger rises.
  - The sideways lean itself procedural, a roll of the hips and spine by `Stage::lean`.
  - A fall-over clip still to come, blending into `Falling_Idle`.

**Decided: the lean is sideways**, toward or away from the camera, corrected with Up/Down on the
aim axis. The animation will be matched to the gameplay.

**Step 1, rules and blockout: BUILT 2026-09-26.** The ground now runs to x 176 and the wall moved
to 175.

- **Two branches.**
  - The high one runs from the canopy's end (133, 13.0) down to a perch at x 145..152, top 12.4.
  - The low practice one is 2.6 up (raised from 2.0 for the catch, below), between stumps at
    x 155..157 and 168..170.
- **`StageBranch`** is a straight one-way line; Down drops through it. On it she walks at up to
  `BRANCH_WALK_SPEED` (1.8), can't kick or kneel, and Up/Down stop tilting the bow unless she is
  drawing. Aiming from a branch costs balance.
- **The balance** is an inverted pendulum in `Stage::TickBalance`:
  - `lean` is in radians, + away from the camera.
  - The lean's own pull is `BALANCE_TOPPLE` (5).
  - A deterministic drift of `BALANCE_DRIFT` (1.2) plus up to `BALANCE_WALK_DRIFT` (1.6) at a
    full walk, a sum of sines started at a new point on every step-on.
  - Up/Down push back with `BALANCE_CORRECT` (6), against a little damping (0.6/s).
  - A landing adds a wobble in proportion to its speed.
  - Past `BALANCE_FALL_DEG` (35) she goes over, and the event carries which side she fell.
    (Step 1 dropped her through; step 2 turned that into the catch.)
- **`BALANCE_CORRECT` sized so a lean is always recoverable.** A full press beats the worst drift
  plus the lean's pull right up to 35 degrees, so a fall is always a reaction problem, never the
  numbers. At 4.0, a full-speed walk past 14 degrees was lost whatever you did.
- **The blockout view:**
  - The branch is a thin bark-brown slab.
  - While she is on a branch, a gauge beside her head: a bar whose ends are the fall angle, with a
    marker at her lean (up is away from the camera, the way Up pushes). The marker goes green to
    red with the danger.
  - The model rolls about her feet by the lean.
  - `archer_state.balance` reports the branch, lean, rate and danger.
- **stage_test (TestBranch)** checks:
  - Left alone she goes over in about 1.2 s.
  - Walking at full speed, she goes over sooner on average over eight drifts (64 ticks against
    85).
  - Up held throughout tips her over the far side.
  - A late player (reacting 10 ticks, about 1/6 s, behind, with full key presses) crosses both
    branches, and the low one on three other drifts too. Its worst lean is about 30 degrees.
  - The bow's aim holds while balancing.
  - Down drops through.
  - A hard landing wobbles more than stepping on.
- **In the game** the same late player crosses the low branch stump to stump (worst lean 26
  degrees), and left alone she falls after 1.5 s.

**Step 2, the catch: BUILT 2026-09-26.** Going over is not a fall.

- **The catch.** Past the limit she grabs the branch and hangs below it. It's the ledge's
  `MODE_HANG`, with `Stage::hang_branch` remembered in place of a block. Her hands sit
  `BRANCH_HANG_DROP` (the ledge's 0.31) below the line, held `BRANCH_HANG_INSET` (0.4) inside
  either end.
- **From the hang.** Jump climbs back up: the ledge's `MODE_CLIMB`, path and all, since the rise
  is the same, carried `LEDGE_CLIMB_INSET` along the branch the way she faces. She stands balancing
  again from upright, on a fresh drift. Down lets go.
- **Caught in the air too,** like a ledge: falling, her hands within the ledge's band of the line,
  Down not held. A drop through with Down starts the grab cooldown, so she doesn't catch it again
  on the way past.
- **The animation.** A branch hang plays `Hanging_Rope` (`ArcherAnimParams::f_free_hang`): both
  hands over the top, feet loose. There's no wall to brace on, so not `Hanging_Braced`. The climb
  plays `Climb`, which reads well enough as a pull-up over it.
- **Room to hang.** The hang needs her height plus the drop under the branch, 2.11. At 2.0 the low
  branch put her feet 0.11 into the ground, so it went up to 2.6. stage_test now checks every
  branch in every level has that room along its whole length.
- **Tests.** TestBranchCatch covers: pushed over, she hangs and stays still; Jump climbs back to
  standing on it, upright, and balancing resumes; Down lets go to the ground without a re-catch; a
  jump that comes up short of a 4.2 branch catches it, but not with Down held; a tap of Down drops
  through without a catch. In the game, the hang holds her feet 0.49 clear of the ground and the
  climb ends standing on the branch at 2.60.
- **`BALANCE_DROP_TICKS` and `branch_drop_ticks` are gone:** nothing drops her through any more.
- **No jumping off a branch** (added after playing it: she could hop across instead of balancing).
  Jumping ONTO one is fine. On one, a press does nothing and isn't buffered, so a press just
  before touching down doesn't fire on touchdown either. Walking off a branch's end gives no
  coyote grace. From the stump or ledge at its end she jumps as ever. The hang's Jump still
  climbs. stage_test checks all four.

Next: the composite animation above (and a proper branch hang and pull-up, when those clips
exist), shimmying along while hanging, and a branch that sags under her (the spring plants'
spring).

---

## 4. Smaller ideas, from systems that exist

- **Arrows as pegs.** Arrows already stick in wood, and the rules already track where. A stuck
  arrow could be a tiny one-way platform: shoot a ladder into a wall, climb it. Very much an
  archer's move, and nearly free. Limit how many, or let them snap after a few seconds.
- **Rope arrows.** An arrow into a wooden beam leaves a rope to swing on. The rope, and cutting
  it, are built; the arrow is the trigger.
- **Cutting with arrows.** Shoot a vine or a rope to drop a crate onto something, or let a hanging
  log swing down as a bridge. The cut exists (`vine_plan.md`); only the arrow-hit trigger is new.
- **Wind.** One gust value that pushes arrows - the aim arc shows it, since it runs the flight -
  and knocks her balance on branches. The branch and the bow then have something to do together.

---

## Order

1. **The tree**, blocked out at the right of the main level. Mostly content, low risk, and it
   proves the shared rules-and-view declaration everything else here uses.
2. **The bounce pad, then the leaf.** Introduces the moving, sloped surface the later ones need.
3. **The branch.** Needs animation (balance poses) and the input decision above.
4. **Arrow pegs** fit anywhere; they are nearly free.

---

## Status

| # | Mechanic | State |
|---|---|---|
| 1 | Tree with arms | blockout BUILT 2026-09-26 (right of the main level); a second one drawn with the bigtree pieces BUILT 2026-09-30 on the cave roof |
| 2 | Bounce pad, leaf | blockout BUILT 2026-09-26 (past the tree, x 105..133); a `mushroom_big` pad BUILT 2026-09-30 on the cave roof; the leaf's mesh and animation next |
| 3 | Branch and balance | steps 1-2 BUILT 2026-09-26 (rules, gauge, two branches past the canopy, the catch and climb back up); the animation next |
| 4 | Pegs, rope arrows, arrow cuts, wind | ideas |

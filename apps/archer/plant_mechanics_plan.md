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
| 1 | Tree with arms | blockout BUILT 2026-09-26 (right of the main level); mesh next |
| 2 | Bounce pad, leaf | blockout BUILT 2026-09-26 (past the tree, x 105..133); meshes and animation next |
| 3 | Branch and balance | agreed; the lean axis decided above, open to change |
| 4 | Pegs, rope arrows, arrow cuts, wind | ideas |

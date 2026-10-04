# Creature Plan

Creepy-crawlies for the jungle and the cave: spiders in two sizes and snakes, each following a
path through the level. Talked through 2026-10-01. Steps 1-3 BUILT 2026-10-01 (the paths, their
debug view, and the small spiders on the real model); see "As built" below. The big web the
spider lives in has its own plan, `web_plan.md`.

- **Small spiders** crawl up the sides of the level and up and around the big vines. They are
  readable from the side view, and there may be 10-20 at a time.
- **A big spider** lives on the stage, as something she actually deals with.
- **Snakes** follow the same idea. There is no model yet, but a snake is simple to model, and
  even simpler to block out.

As always, **paths and blockout come first**. The path debug view comes before any creature, so
the routes can be seen and judged before anything walks them.

---

## Decided

- **Two kinds of creature, split the way the rest of the game is split:**
  - **Ambient creatures** (the small spiders) are **view-only**, like the leaves, fireflies and
    cave dressing. They are seeded, cost the rules nothing, and sit in a visual-only group the
    state hash skips. They cannot hurt her, and arrows pass through them (see the open questions).
  - **Gameplay creatures** (the big spider, snakes) are **declared in the rules** (`Stage`),
    like everything else she touches. They replay bit-exact, `make rules` can test them, and the
    app draws them from the same numbers.
- **A path is a `core/Spline`.** It already has everything a crawler needs: arc length
  (`GetLength`, `ParamAtDistance`), and position, tangent and a rotation-minimising frame at a
  distance (`PositionAt`, `TangentAt`, `FrameAt`, core/Spline.h:72-80). Its own header says it
  was built "for moving things along a path in time". A creature is a distance `s` along its
  path, a speed and a direction, stepped in ticks.
- **The path debug view reuses the wind view's parts** (section 2).

---

## 1. Paths

**Three ways to make one,** all ending as a Spline:

1. **Hand-declared**, as a point list, like `DeclareVines` (Vine.cpp:947). This is the default
   for gameplay creatures, whose routes are level design.
2. **Along a vine.** The vine curves come from `DeclareVines` / `BuildVineSpline` (Vine.h:118),
   which are engine-free (stage_test already calls them). "Up and around" the vine is the vine's
   own spline with the crawler offset around it, by `FrameAt`'s normal and side, turning as it
   climbs: a helix around the trunk for free.
3. **Generated along a surface.** The creeper growth (`GrowVine` against a `VineField`,
   Vine.h:172-215, 315-324) already walks "up the face, over the lip, across and down" against
   blocks plus terrain, and returns a `VinePath`. Run with a crawler's parameters, it lays a
   path hugging a wall, which suits "up the side of the level". `VineLevelField` (Vine.h:279-304)
   is the surface it walks.

**A path declaration** (rules side for gameplay creatures, view side for ambient ones):

```
struct CreaturePath{
    std::vector<v3> points;   //or a vine index, or a generated surface run
    bool f_loop = false;      //closed loop, or back and forth between its ends
    float offset = 0.0f;      //distance off the surface/curve (round a vine: the orbit radius)
};
```

**Movement along it:** speed, direction, and behaviour at the ends (turn round, loop, or stop
and wait). Small spiders add pauses and twitches, which is most of what makes a spider read as
a spider. All of it is in ticks.

---

## 2. The path debug view

Like the wind view (WindView.h/.cpp), and built mostly from its parts:

- **one line mesh**, a visual-only, unpickable Object rebuilt when the paths change (the Init
  pattern, WindView.cpp:41);
- **`WindView::Line()`** to lift lines over the z=0 plane (WindView.cpp:60), and
  **`WindViewRect`** to draw only what is on screen;
- **the arrow-head maths** (WindView.cpp:96-114), placed every unit or so along each path to
  show the direction of travel;
- the closest template is the floor-edges view (`BuildEdgeView`/`UpdateEdgeView`,
  ApplicationArcher.cpp:2237-2326), which re-uploads only when its data's generation changes.

**What it shows:**
- each path coloured by kind (hand-declared, vine, surface);
- its direction arrows;
- loop or end markers;
- each creature as a small cross at its current `s`.

**Toggle:** `archer_debug_view {paths: true}` over MCP, beside `wind` and `edges`, and a
checkbox in the ImGui panel.

---

## 3. Small spiders (ambient)

**Mesh, not skeleton.** A spider model with **one shape key** for the leg cycle. Morph targets
already work on non-skinned meshes (`Mesh::SetMorphMeshData`, core/Mesh.cpp:251; up to 4 per
mesh, `morph_factors` per object, applied in default.vert:212-216).
- **Phase:** each spider's morph factor is set from the CPU from the **distance it has
  travelled**, so the legs cycle exactly as fast as it moves. Feet don't slide, and a paused
  spider's legs stop.
- **Cost:** the renderer draws one instanced call per unique Mesh, so 20 spiders sharing **one**
  Mesh are one draw. They must actually share it: `GLTFLoader::GetMeshFromNode` makes a new Mesh
  on every call (GLTFLoader.cpp:1213), so load it once and hand every spider the same pointer.
  (Bomber's enemies don't do this, and cost one draw each.)
- **Orientation:** body along the tangent, belly against the surface by the frame's normal.
  Around a vine, the frame turns with the helix.

**Placement:** on vine paths and wall paths, seeded. Denser in the cave, but jungle walls too.

**Blockout:** a small dark box with a nose, wiggling, before the model exists.

**Model:** not in the export yet. A small spider with one leg-cycle shape key. The big spider's
mesh, scaled down, won't do, because it will be skinned.

---

## 4. Snakes (gameplay)

**The body follows the head's path.** The head moves along the path at `s`, and the body occupies
the stretch behind it (`s - length` to `s`), so every part of the body passes exactly where the
head went. On a spline this is just a second distance, with no trail to record.

**Drawing it is the rope and vine technique.** `SplineDeform` already bends a straight mesh along
a spline (core/SplineDeform.h; the rope and the vines use it). A straight snake mesh deformed
along that stretch of path is the snake, with no skeleton. A slither is a small sideways wave
added to the deform, travelling down the body.

**Model:** a straight, segmentless snake body is enough for the deform. There's already a
`snake_skeleton_big` in the export, used as cave dressing. The blockout comes first and needs no
model: a chain of spheres along the stretch.

### Decided 2026-10-04

- **They live in branches and drop on her.** A snake patrols branches above and around her line
  and lets go when she passes underneath.
- **She can kill them, with 1-3 shots from any arrow.** Each snake has 1, 2 or 3 hit points, and
  every arrow kind takes one. The kinds can differ later (ideas below).
- **The branches are the snakes', not hers.** She cannot stand on them or climb them. They collide
  with nothing and are paths for the snakes and looks for the level, so they need no tuning
  against her jump.
- **Their paths run in front of and behind the stage**, as the spiders' do, and cross her line
  only where a branch passes over it. That crossing is where a snake can reach her, and where she
  can reach it.
- **The first home is the blockout tree at x 80, made taller, with a new level on top** (the
  crown) and loose blockout branches spreading from it.

### The tree, taller, with a crown level

Today the tree is 11 tall, with arms at 2.5 R, 5.0 L and 7.5 R. They climb to the low slab
(x 70..76, top 5.5) and the high slab (x 84..97, top 10.2). The climb carries on above the high
slab at the same spacing, 2.5 apart and alternating sides, the spacing stage_test already proves:

| piece | top | how she gets there |
|---|---|---|
| arm 4, right, 2 long | 12.7 | from the high slab: the tip at 82.45 is 1.55 from its edge at 84, the same hop as arm 3 onto it |
| arm 5, left | 15.2 | from arm 4 |
| arm 6, right | 17.7 | from arm 5 |
| **the crown**, the cut top 9 wide (x 75.5..84.5) | 20.2 | from arm 6; the trunk is 20.2 tall now |

- **The crown is the new level**: a one-way floor across the top of the trunk, reached from the arm
  below and dropped through with Down. It is `StageTree::top_width`, which the roof tree already
  uses, so nothing new is needed in the rules.
- **No new blocks.** `BuildTrees` appends arms after every declared block, so the level's own
  block indices, which the dressing is seeded by, do not move. **Check:** the roof tree's arms
  come after this tree's, so their indices do move by three. See whether anything seeds off them.
- From the crown, a long drop back down: Down through it lands on the high slab (x >= 84) or on
  the arms.

### The snake branches

Declared in the rules, since the snakes' paths are gameplay: a list of 3D polylines, each
starting at the trunk (x 80, z -1.3, `STAGE_TREE_Z`), drawn as thin bark-brown boxes from the same
numbers and colliding with nothing. A first set:

| branch | from (at the trunk) | to | why |
|---|---|---|---|
| over the high slab | y 19.0, z -1.3 | x 93, y 15.5, z +0.6 | crosses her line at x 88.9, y 16.6: 6.4 above the slab she walks; the main drop |
| over the low slab | y 16.5, z -1.3 | x 69, y 13.0, z +0.8 | crosses at x 73.2, y 14.3: 8.8 above the low slab, a drop she meets on the way up |
| over the crown | y 21.5, z -1.3 | via x 82, y 22.8, z 0 to x 88, y 22.0, z +2.2 | crosses at x 82, above her head on the crown (feet 20.2, head 22.0) and inside its 75.5..84.5, so it drops onto the new level |
| behind, right | y 21.0, z -1.3 | x 90, y 22.5, z -3.0 | a perch behind: seen, not reachable |
| behind, left | y 18.5, z -1.3 | x 71, y 20.0, z -2.6 | the same, on the left |

Plus **the trunk itself** as one more path: a helix round it from the ground to the crown, the way
a spider climbs a vine (`CREATURE_PATH_VINE`). It ties the branches together and gives a dropped
snake a way back up.

The paths are `CreaturePath`s of the HAND kind, with the up +Y on top of the branch. Creatures.cpp
and core/Spline are already in `make rules`, so the rules can own them.

### The snake in the rules

`StageSnake`: a path, a distance `s` along it, a direction, a speed, a `length`, and `hp` 1-3.
The size follows the hit points, so the tougher ones read as bigger: about 1.2 long for 1 hp,
1.8 for 2 and 2.6 for 3. States are tick counters, in the bomber pattern:

| state | what it does |
|---|---|
| PATROL | glides along its branches in runs and pauses, smoother than a spider (no twitch); at a fork it picks a branch with its own seeded draw |
| WATCH | on a crossing, with her below and within a few units in x: it stops and lowers its head off the branch. The telegraph, a cue (rattle), lasting tens of ticks: the window to shoot it or get clear |
| DROP | lets go: the whole chain falls under the game's gravity, kept in shape, and lands on whatever is under it in her plane |
| LANDED | stunned for a moment where it hit |
| GROUND | slithers along the surface in her plane, heading for the trunk. At a slab's end it falls to the next surface |
| CLIMB | at the trunk's x it joins the trunk path and goes back up to its branches |
| DEAD | a countdown, so a death can play, then gone. It stays gone until the level restarts |

**Arrows:** `FlyArrow` sweeps each arrow, so a snake adds a test against its chain of spheres, as
the apples have. **Only where the body is on her line** (within a `SNAKE_ON_LINE_Z` of z 0, the
apples' rule), because her arrows fly in the plane. That makes the crossings and the ground the
places a snake can be shot, and the trunk and the far branches safe for it. A hit takes a point,
gives an event (`SnakeHit`, `SnakeKilled`) and makes it flinch: it stops and recoils for a few
ticks.

**Contact:** the open question, as for the big spider. First pass, in keeping with section 6: a
touch is a `SnakeBite` event and a fear spike, nothing else, so health can be designed on top of
it when the first creature can touch her.

**The blockout view:** a sphere per body section, the head a different colour, laid along the
path from `s - length` to `s`. The slither is a sideways wave travelling down the body, and it is
looks only: the rules test against the centre line plus a margin, so the wave never has to replay.
The path debug view shows the snake paths in their own colour.

### Ideas, not decided

- **A hit on a branch knocks it off.** A shot brings a snake down where she chooses: knock it off
  before she walks under, then finish it on the ground. It gives the first arrow a use even
  against a 3-hp snake.
- **The heartbeat is the warning.** A snake overhead within a few units raises her fear before it
  drops (section 6's fear input), so the breathing and heartbeat tell the player before anything
  shows.
- **It rears and lunges on the ground.** Within reach it rears (the head leaves the path), lunges
  once, then makes for the trunk. Hit while fleeing, it drops what it is doing and coils.
- **Arrow kinds can matter later.** A bamboo arrow pins a snake to its branch for a while, so it
  cannot drop or move. A vine arrow's vine could become a new path for snakes, so a careless vine
  invites them closer.
- **A snake on an arm.** One coiled on an arm she has to climb past blocks the route: shoot it from
  below, or time the hop while it faces away.
- **A hollow in the crown.** Snakes come out of a hole in the cut top, a nest she can see but not
  reach: the "live in holes" answer to the open question, alongside the branches.

### Order for the snake

1. **The tree, taller, with the crown**, plus the snake branches drawn as blockout. stage_test
   climbs from the high slab to the crown and checks the branches collide with nothing.
   archer_test's state changes here (new arms are new bodies), so its baselines get rewritten.
2. **The snake paths** off the branches and the trunk, in the path debug view.
3. **A snake patrolling**: StageSnake in the rules, spheres in the view, in the state hash.
4. **The drop**: WATCH, DROP, LANDED, and stage_test dropping one onto the high slab with her under it.
5. **Arrows**: hit points, flinch, death. stage_test kills a 1-, 2- and 3-hp snake with that many
   arrows, and checks a snake off her line cannot be hit.
6. **GROUND and CLIMB**: back to the trunk and up.
7. **Contact**: the bite event and fear. Then the model, with SplineDeform.

---

## 5. The big spider (gameplay)

**Skinned**, with its own rig and clips (idle, crawl, lunge, hit, die, and a sit-in-the-web pose).
There is only one of it, so the skinned cost is one instance. The reference image has it sitting
in the web in its frame (`web_plan.md`).

**Rules:** a `StageCreature` with its path, state as tick counters (the bomber pattern,
apps/bomber/Maze.h:272-376: one walker struct, death as a countdown so the death clip has time to
play), and hit points. It reaches her collision the way props do, as a `StageObstacle` refreshed
each tick (Stage.h:1196-1235), or with its own touch test if contact means damage.

**Arrows:** the rules sweep each arrow (`FlyArrow`, Stage.cpp:3917), so a creature adds a body
test there (a circle or capsule), exactly like the apple plan's hit test. Event `CreatureHit`.

**What it does** is the main open question below. A reasonable first pass: it guards its web.
It comes down a thread when she is near, lunges, and climbs back up after taking hits.

---

## 6. Her side: health and fear

**There is no health yet.** `StageVitals` has exertion, fear and heart rate (Stage.h:1663-1733).
`vitals_plan.md` already says health and power "will live here when enemies arrive", and that
enemies are "one more input, not a redesign".

- **Fear:** a creature's distance and approach become a fear input (`Stage::TickVitals`,
  Stage.cpp:2189), so her breathing and heartbeat react before anything happens. That is cheap,
  and it is atmosphere straight away.
- **Health and damage:** to be designed with the first creature that can touch her. Knockback, a
  stagger, a hit point, or a respawn.

---

## 7. Sounds (cues)

Signals into `archer.json`, as everything else:
- small spiders: a faint skitter, only near the camera;
- snake: hiss, rattle;
- big spider: footfalls, a lunge, hit, death;
- a fear sting when a creature first notices her.

---

## Order

1. **Paths and the debug view.** `CreaturePath`, the three path sources, and the view, with a
   dot moving along each path. Nothing alive yet.
2. **Small spiders, blocked out** on vine and wall paths; ambient, seeded, with pauses.
3. **The small spider model** with its shape-key leg cycle, sharing one Mesh, checked for cost
   (20 at once) on the GPU pass timers.
4. **Snake, blocked out** as spheres following its head, then the SplineDeform snake once a
   model exists. Its own steps, starting with the taller tree, are at the end of section 4.
5. **Big spider, blocked out**: rules, path, hit test, guarding behaviour. Then its model and clips.
6. **Fear input,** then health and damage, once a creature can touch her.

---

## As built (2026-10-01): steps 1-3

**The code:**
- `Creatures.h` / `Creatures.cpp`, engine-free and tested by `make rules` (TestCreatures):
  - `CreaturePath` and its three builders;
  - `CrawlerSwarm`, the ambient walk.
- In ApplicationArcher (the CREATURES ON PATHS block):
  - `BuildCreatures` lays the world level out;
  - `StepCreatures` walks and poses the spiders each tick;
  - `UpdatePathView` draws the debug lines;
  - the `archer_creatures` tool.

### The model: `spider`

- **A plain mesh:** 3,484 vertices on `props_texture`, with **three shape keys** - `Walking`
  (target 0), `LegsSwing` (1) and `LegsLift` (2) - and no skin. The node has no rotation or scale.
  Its authoring translation is ignored, as for every prop.
- **Its Mirror modifier is applied** (2026-10-02, `tools/blender_spider_legs.py`, which also builds
  the two gait keys): two sides stepping out of phase cannot come from one mirrored half.
- **Orientation:** it **faces +Z** (the big abdomen is toward -Z) with its **belly down**. The
  origin is near the body's centre and the feet are 0.163 below it.
- **Size:** it is **0.92 across its legs: 1.67 at model_scale**, as wide as she is tall. That is
  the big spider's size, not a small one's, so the small spiders take a factor of their own
  (below).

### Paths

A path is a Spline through `points`, with an `up` beside each point: the way a crawler's back
faces there. Between points the up is blended by distance and squared to the tangent, so the body
lies along the path with its belly on the surface. A path carries the spider's **centre**, held
its foot drop off whatever it walks on.

| source | how | in the world level |
|---|---|---|
| **hand** | a point list and one up (`CreaturePathFromPoints`) | 1: across the cave floor behind her, among the bones |
| **vine** | the vine's own curve, held off the bark by `VineRadiusAt` plus the foot drop, turning once every 2.5 units (`CreaturePathAroundVine`) | 4: the bay's big vines, two in front of her line and two behind, over what the camera sees from the floor (y 0.3 .. 13) |
| **surface** | the creeper's walk (`GrowVine` against `VineLevelField`, the level as drawn) with a crawler's numbers, every point snapped to the surface (`CreaturePathOverSurface`) | 8: the cave's far wall up onto the roof, two runs under the roof (behind her and in front), the mouth's lip; both faces of the bay's hill, under the floating stone, under the island |

**The creeper needed two changes to carry a spider:**
- **It hugs harder (40) and climbs less (1.0)**, with points every 0.12. With a vine's numbers it
  came over the hill's lip 0.4 above the top.
- **Every point is snapped onto the surface at the gap**, along the field's normal. Even with the
  harder hug it rounded a box's corner 0.23 wide, and a spider there would stand on air.

stage_test checks that a surface run sits exactly at its gap, that its up is the surface normal,
that it climbs over the lip, and that it holds its depth to within a tenth.

**Nothing walks through her line:**
- the vines hang at z +2.5 or behind her;
- every wall run is anchored at z -1.3 .. -0.9 or +0.6 .. +0.9;
- the hand path is at z -1.8 ± 0.35.

**The big cave vines are compiled in:** ARCHER_TEST_BAY is 1. The vines the spiders climb are the
bay's four, which DeclareVines declares under that flag.

### The path view

`archer_debug_view {paths: true}`, and a "creature paths" checkbox beside "floor edges" in the
Zones panel. It is the floor-edges view's pattern: one visual-only line object, its lines built
once from the paths (they never move), with the spiders' crosses added from the snapshot each
frame. It shows:
- paths **yellow** by hand, **green** round a vine, **orange** along a wall;
- an arrow every unit toward the path's end;
- a bar across each end of an open path, and a ring at a loop's start;
- a **white cross on every spider drawn**.

Every point is slid along its ray to the eye to z 3.4, WindView::Line's trick taken to 3D. So the
lines sit over what they describe, in front of the vines at z 2.5, and keep their pixel.

### The small spiders

**Ambient.** Nothing in the rules reads them, and every Object is visual-only, so a replay's hash
skips them. They are walked on the physics thread after the leaves, on the world level only.
Seeded: each crawler has its own xorshift, seeded from its index and the path's seed.

**How one moves:** in runs and pauses.
- A **run** is 20-110 ticks at its own speed, between one and two and a half of its spans a second.
- A **pause** is 15-150 ticks. A run turns back a quarter of the time.
- **Twitches:** while paused, about 3% of ticks start one, a few hundredths out and straight back.
- **At an open end** it stops, and goes back the way it came.

stage_test checks that it stays on its path, that it both runs and stops, that no step is faster
than its top speed, and that two swarms seeded alike walk alike.

**The legs: an alternating gait, driven by distance walked.** The phase is `travelled / stride`,
with the stride 0.6 of its span. `travelled` counts only distance really covered, so a stopped
spider's legs stop (stage_test checks this), including one held at a path's end - mid-stride, its
feet planted.

**`leg_mode` 2, the default: `LegsSwing = sin`, `LegsLift = cos` of the phase.** Both keys are
symmetric about the rest pose. Swing +1 puts L1 R2 L3 R4 forward and the other four back; lift +1
raises the same four (knee up, tip tucked) and presses the others down. Lift leads swing by a
quarter cycle, so each foot is up while it swings forward and down while it sweeps back - the
alternating tetrapod a real spider walks. Measured in Blender: tips rise 0.03-0.045 of the 0.92
span, swing 9-14 degrees about the hip (less on the front and back pairs, which point along the
body and otherwise cross the middle ones).
- **The stance presses the feet below rest** by as much as the lift, about 0.035 in the model.
  Two linear keys with a planted rest pose cannot avoid it: the lift key's negative half always
  dips. A third key (a lift for each group of four) would give a flat stance. Not visible at this
  size from the game camera.
- **The back two pairs hinge where they leave the abdomen**, not at the hip: in this mesh they are
  fused to its underside near their roots, and turning them at the hip dragged it along.

**Modes 0 and 1: the old `Walking` key alone**, kept as target 0. It moves **both sides' legs
alike** (+1 gathers them in, -1 stretches them fore and aft), so it cannot alternate the legs
whatever the factor does. A mesh without the two gait keys falls back to 0. Of the two swings
tried, by eye at 3.5 from the camera, a frame every 5 ticks:
- **0..1 (chosen):** the legs gather and splay. It reads as a crawling pump and stays in
  proportion.
- **-1..+1:** this passes through rest to the key's opposite. Past about -0.5 the legs stretch out
  flat and grow longer, because a morph pushed below its rest extrapolates. It reads as a stretch,
  not an opposite stride.

All three are on `archer_creatures {leg_mode, leg_amount}`.

**Size: scaled up to read.**
- At 0.24 x model_scale (0.40 across) they were dark specks on dark vines from the game camera.
- **At 0.38 (0.64 across)** the legs read on the vines, on the hill and under the stones.

That is bigger than a real small spider, chosen for readability (`spider_scale`, or
`archer_creatures {scale}` live).

**Cost.** Every spider shares one Mesh (loaded once in BuildCreatures), so they are one instanced
draw, each with its own morph factor. A mesh with morph targets is never frustum-culled
(Renderer), so StepCreatures hides every spider outside the view; only those on screen are drawn.
Measured in release, paused, vsync off, with the bench (`archer_creatures {bench: n}`: n spiders
in a grid across the view) where no level spider is on screen. Medians of 8-12 reads, alternating
the counts:

| spiders in view | G-buffer | Color | Shadow map | all passes |
|---|---|---|---|---|
| 0 | 1565 us | 3797 | 938 | 6632 |
| 10 | 1587 | 3895 | 945 | 6764 |
| 20 | 1587 | 3921 | 934 | 6769 |
| 40 | 1586 | 3846 | 917 | 6589 |

**The cost is below the timers' noise:** about +20 us in the G-buffer, and the rest moves ±150 us
between reads at any count. Forty spiders are 140k vertices in one draw. They cast no shadow
(`SetCastsShadow(false)`), so the shadow pass is untouched.

### Readability, from the game camera

- **In the bay:** at 0.38 they read as spiders on the vines (on the bark, turning round it as they
  climb), on the hill and under the stones.
- **In the cave they are dim:** upside down under the roof, among the stalactites, readable but
  only about as well as the stalactites, because the cave is dark. A slightly lit copy of their
  material for the cave, or the cave's own light, would help; not done.

### archer_test

It **differs from tick 0, in `anim` and `puppet`, and the footsteps with them, and the cause is
the export, not the spiders.** archer.glb on disk is a new export (22:39, uncommitted), and
**every one of her 43 clips' keyframe data differs** from the committed file. Three clips are new:
`Fleeing`, `Surfing_Idle` and `Kick_Front_2.001`. The spiders are visual-only, and nothing of them
reaches those parts. It was **not re-baselined**: the export should be confirmed first.

### Export notes

- `spider` is clean: one shape key, no node rotation or scale, facing +Z with its belly down.
- `spider_web` is in the export but unused (it is the web's, for later). It is on the startup log's
  list of unused meshes.
- `Kick_Front_2.001` is a duplicate-named clip in the new export.

### Open points for the user

- **The legs:** one symmetric key cannot alternate. A second key, the other half of the gait,
  would give a real stride.
- **Size:** 0.64 across is large for a "small" spider; it is what reads at 26 away.
- **The cave:** they are dim there; a lit material or the cave's light would help.
- **They pass through her and her arrows**, and do not react to her (the open questions below).
- **A restart leaves them where they are.** They are ambient, and the reseed question stays open.

---

## Open questions

- **What does contact do?** Damage, knockback, a fright only, or the end of the run? This
  decides the health design.
- **Can she kill them?** Snakes: yes, 1-3 arrows (decided 2026-10-04, section 4). The big spider
  in N hits? Small spiders, ambient, could still be shootable as a flourish (a hit flicks them off
  the wall), but that makes them gameplay.
- **Do small spiders react to her?** Scatter as she passes, freeze when looked at?
- **Snake behaviour:** they drop from branches (decided). Holes (the crown's hollow, an idea in
  section 4) or swimming the stream could come later.
- **Where:** snakes start in the crown of the tree at x 80. Cave only, or jungle too? Snakes in
  the jungle, spiders in the cave?
- **Respawning** after a restart: rules creatures restore with the level, ambient ones reseed.

---

## Status

| # | Piece | State |
|---|---|---|
| 1 | Paths + debug view | **built** 2026-10-01: hand, vine and surface paths; `archer_debug_view {paths}` and a checkbox |
| 2 | Small spiders (blockout) | **built** straight on the model: 38 on 13 paths in the bay and the cave, runs, pauses and twitches |
| 3 | Small spider model | **built**: `spider` in the export, one Mesh for all, legs off distance walked; an alternating gait on `LegsSwing`/`LegsLift` since 2026-10-02 (`leg_mode` 2); cost below the timers' noise at 40 |
| 4 | Snake | planned; blockout worked out 2026-10-04 (section 4): a taller tree at x 80 with a crown level and snake branches; no model yet |
| 5 | Big spider | planned; model in progress (reference render 2026-10-01) |
| 6 | Fear, health | planned |

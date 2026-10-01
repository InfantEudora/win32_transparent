# Creature Plan

Creepy-crawlies for the jungle and the cave: spiders in two sizes and snakes, each following a
path through the level. Talked through 2026-10-01; nothing here is built yet. The big web the
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

**Blockout:** a chain of spheres along the stretch, before any model.

**Behaviour, first pass:** patrols its path; maybe rears up when she is close (the head leaves
the path for a moment). What contact does is the open question that decides the rest.

**Model:** a straight, segmentless snake body is enough for the deform. There's already a
`snake_skeleton_big` in the export, used as cave dressing.

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
   model exists.
5. **Big spider, blocked out**: rules, path, hit test, guarding behaviour. Then its model and clips.
6. **Fear input,** then health and damage, once a creature can touch her.

---

## Open questions

- **What does contact do?** Damage, knockback, a fright only, or the end of the run? This
  decides the health design.
- **Can she kill them?** The big spider in N hits; snakes in one? Small spiders, ambient, could
  still be shootable as a flourish (a hit flicks them off the wall), but that makes them gameplay.
- **Do small spiders react to her?** Scatter as she passes, freeze when looked at?
- **Snake behaviour:** do they live in holes and come out, drop from branches, or swim the stream?
- **Where:** cave only, or jungle too? Snakes in the jungle, spiders in the cave?
- **Respawning** after a restart: rules creatures restore with the level, ambient ones reseed.

---

## Status

| # | Piece | State |
|---|---|---|
| 1 | Paths + debug view | planned |
| 2 | Small spiders (blockout) | planned |
| 3 | Small spider model | planned; model not in the export yet |
| 4 | Snake | planned; no model yet |
| 5 | Big spider | planned; model in progress (reference render 2026-10-01) |
| 6 | Fear, health | planned |

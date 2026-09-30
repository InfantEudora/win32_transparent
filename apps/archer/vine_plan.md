# Archer Vine and Rope Plan

Vines and the swinging rope, built from one system. Agreed 2026-09-24: vines are placed
**explicitly** for now (generated along terrain features later), and most decorative ones are
**static** - a few may sway. Steps 1 and 2 are being built first; the rest is the plan.

The short argument: a rope and a vine are both *a curve through space with things arranged along
it*. Build the curve once, with a frame that does not flip, and each of them is a recipe of three
operations on it. The swinging rope keeps its physics chain exactly as it is - the chain drives the
curve, the curve drives the mesh.

---

## 1. The three operations

| operation | what it does | rope | vine |
|---|---|---|---|
| **Deform** | tile an authored mesh along the curve, bending it into it - Blender's Array + Curve modifier, in the engine | the twisted middle | the thick trunk |
| **Derive** | a new curve from an old one | - | the thin wraps: a helix around the trunk at trunk radius, two with different phases; the curl where a wrap leaves the trunk and ends in a log spiral |
| **Scatter** | instances at stations along a curve, hashed jitter | the ring at the top, collars, the tassel | leaves (on the *wraps*, eventually), curl tips |

So a rope is `Deform(rope_tile) + Scatter(ring, collars, tassel)` and a vine is
`Deform(trunk_tile) + 2 x Derive(helix) -> Deform(wrap_tile) + Scatter(leaves, curls)`. Decorative
or swingable is a flag on either, not a different asset.

**Deform rather than loft.** Generating a tube from a profile would be less code, but it would
throw away the low-poly faceted look that is being modelled in Blender. Deforming an authored tile
keeps the artist's cross-section and lets the curve decide only the shape.

---

## 2. The core piece: `core/Spline`

`core/Bezier2D.h` exists but is 2D, capped at order 4, with no arc-length table and no frame. It
is left alone; `Spline` is new.

- **Cubic Hermite segments.** Catmull-Rom is Hermite with the tangents chosen for you, and the
  **centripetal** form (alpha 0.5) is the one that cannot overshoot into a cusp or a loop between
  close points. It passes *through* its points, which is what both a hand-typed level coordinate
  and a physics link want.
- **An arc-length table.** "A leaf every 0.3" and "a tile every 0.5" are distances, not parameter
  values. Position is evaluated exactly at the parameter the table maps a distance to, not
  lerped out of the table.
- **Rotation-minimising frames**, by the double-reflection method (Wang, Juttler, Zheng, Liu
  2008). NOT Frenet: a Frenet normal flips at every inflection and is undefined on a straight
  run, so a helix wrapped around an S-bend would jump round the trunk at the middle. This is the
  one thing in the file that is easy to get wrong.
- **By time as well as by distance**, for the animation uses - camera rails, moving platforms,
  bird paths, the aim arc drawn as a ribbon. Also worth noting: `GLTFLoader.cpp` reads glTF
  CUBICSPLINE samplers and keeps only the middle value, dropping the tangents. The static
  `Spline::Hermite` is what would let it honour them.

Tested without an engine - `tools/spline_test.cpp`, built from `Spline.cpp` and the type helpers
alone - and through `make rules`, which links it into the vine checks.

---

## 3. Static vines (steps 1-2)

### The declaration

`apps/archer/Vine.h` - engine-free apart from core's maths types and `Spline`, and tested by
`make rules` like Foliage. The main level's vines are a hand-typed table of control points in
`Vine.cpp`, beside a per-vine seed. Later a generator will produce the same `VinePath` from the
blocks (along a lip, down a face, across the underside of an overhang), so nothing downstream will
care where the points came from.

### The tile convention

A tile is one repeat of the trunk (or rope), authored:

- running along **glTF +Z** (Blender -Y), the same forward as every other prop in this file;
- with its **cross-section centred on the Z axis**, +Y the side that faces up when the vine lies
  on the ground;
- with **matching end rings** - the vertices on the z-min face must sit exactly where the z-max
  face's do, or the seams show. The length is measured off the vertices, never typed in.

Tiles are stretched slightly so a whole number fits the curve exactly. The ends are tapered (a
radius scale along the curve) so a vine does not end in a sawn-off stump.

Until the Blender pieces exist, `Vine.cpp` builds a placeholder tile - an irregular octagon, a
little flattened - so the whole path works today. The app looks for the node names first
(`vine_trunk`, `vine_leaf_1`, `vine_leaf_2`) and falls back per piece.

### Leaves

Placed at stations along the trunk, jittered by the same hash-of-where Foliage uses (moved out
into `PlaceHash.h` so both share it), each rotated so its stem sits on the trunk surface and it
points outward and along. For now, leaves sit on the trunk itself. When the wraps exist (step 3)
they move to the wraps, which is where they grow in the reference model.

They are Objects sharing a mesh, like the ferns, so a vine's leaves are one draw per leaf kind.
**Baking** trunk and leaves into one mesh per vine - or every static vine in the level into one
mesh - is the cost optimisation to reach for when they become numerous, leaving only the few that
sway as separate objects. It is not needed at a handful.

---

## 4. The swinging rope (steps 4-5)

The chain in `BuildRope` stays. What changes is only how it is drawn.

- **Skinned, not remeshed.** Build the rope or vine once, in its bind pose (hanging straight), with
  one bone per link, each vertex weighted to the two links it lies between. Each tick, write the
  bones from the rp3d bodies. The GPU bends it; there is no per-frame `SetMeshData` and no upload
  from the physics thread (remember `NewGame` is the physics thread and must not remesh).
- **The frames only matter once.** Parallel transport is needed to generate the bind pose; after
  that the bones carry the orientation. The hard maths runs once per build.
- **Leaves ride along** because they are baked into the same skinned mesh, weighted to their
  nearest link - one object, one draw.
- **The unknown:** skeletons today come only from glTF. A `Skeleton` built in code, whose bones
  the app writes each tick, is the new core work and the thing to prove first. `Bone` is an
  `Object` and the renderer only asks it for `GetWorldTransformScaleMatrix()`, so it looks
  feasible. Where the bone writes happen relative to the render thread's read needs checking,
  not assuming.
- **Stiffer vines**: a vine should swing heavier than a rope - more angular damping, or the ball
  joint's cone limit.

---

## 5. Ideas beyond the brief

- **Grip at any point, not at a link.** With a curve the hand snaps to the nearest point on the
  rope rather than a box centre, and climbing up or down is moving that distance. Hand IK can
  target it (`Bone::IKExtend`). Probably fixes more of how the swing *reads* than any model will.
- **Rope arrow.** An arrow sticks in a wooden beam and a vine grows down from it, then becomes
  swingable. Growth is cheap because everything is parameterised by distance: sweep to `g * L`
  and scale the leaves in as the front passes them. Ties the bow to the rope.
- **Cut it with an arrow.** A hit breaks the joint at that link - a hanging crate drops, a bridge
  falls. A one-off event, so rebuilding the mesh as two pieces at that moment is fine.
- **Decorative drapes.** A catenary between two ledge lips, from the same generator. Stage already
  knows where the lips are, so vines could be scattered the way ferns are - hanging ones under
  overhangs, where the occlusion score is high.
- **Rustle.** A small impulse to the nearest links when the archer brushes past or an arrow flies
  through; decorative vines get the same through their fixed bones.

---

## 6. Order

1. **`core/Spline`** with its tests. *(BUILT 2026-09-24 - `core/Spline.{h,cpp}`,
   `core/SplineDeform.{h,cpp}`, 35 checks in `tools/spline_test.cpp`: through the points, arc
   length on a circle within 0.03%, the RMF normal stays out of plane through an S-bend's
   inflection, and turns by the known torsion on a helix, 2.810 against 2.810 rad per turn; the
   deform's seams close on a bend and its faces point outward.)*
2. **Static deform + scatter**: decorative vines from hand-typed points, placeholder tile until
   the Blender pieces land, compared against the reference model in a screenshot. *(BUILT
   2026-09-24 - `apps/archer/Vine.{h,cpp}`, `ApplicationArcher::BuildVines`, 22 checks in
   `make rules`. Three vines on the main level: along the ground and up the step by the start,
   draped under the one-way platform, over the high ledge's lip with a free end. Seen in-app from
   the placeholders. One thing added on the way: the scatter takes the blocks and turns a leaf
   whose tip would be buried to the other side of the trunk. Measured: without it 11 of 25 leaves
   on the step vine and 8 of 18 on the ledge vine were in the block, the drape none; with it, all
   of them found the open side and none was dropped. The rules test proves the blocks are what
   keep them out.)*
3. **The wraps.** *(BUILT 2026-09-24, and NOT as derived curves.)* The user modelled the wrap
   instead: `vine_curl` in archer.glb, the strands and a curl wound round one trunk tile. So it is
   a second tile, an OVERLAY, laid with `BuildVineOverlay` at the trunk tile's period, twist and
   taper - copy for copy on the copy of the trunk it was modelled round. Its own extent cannot be
   used: its strands cross the tile boundary on a slant, so its bounds run -0.007..1.019 against
   the trunk's 0..1.001, and laid by those the copies drift 2.6% and open a gap at every join
   (`SplineDeformParams::tile_start/tile_length`). The overhang on the first and last copies runs
   past the curve's ends, where the deform now carries on straight along the end tangent rather
   than squashing it flat onto the end ring. No placeholder: a wrap only means something on its
   own trunk tile. Leaves still sit on the trunk; moving them onto the strands would need anchor
   points out of the wrap mesh, and it looks right without. Derived curves (a helix, a catenary)
   stay useful for the rope and for generated drapes, just not needed here.
   The step vine moved to the FRONT of the lane (z +1.1): behind, the crates hid it.
4. **Procedural skeleton** driven by the existing chain; replaces the rope boxes. The risky step.
   *(BUILT 2026-09-24 - `apps/archer/RopeMesh.{h,cpp}` builds the bind-pose vertices and
   weights, engine-free, 11 checks in `make rules`; `ApplicationArcher::BuildRopeSkin` makes a
   plain core `Skeleton` with one `Bone` per link and `UpdateRopeSkin` copies the link poses on
   every tick. No core change beyond `GLTFLoader::GetNodeScale`. Verified in the Rope scene
   swinging under her weight: one smooth curve over 12 links.)*
   - **Weights are a quadratic B-spline over the link centres** (0.125 / 0.75 / 0.125 at a
     centre), which is what hides the joints; clamped at the ends so the top stays on the anchor
     and the tassel rides the last link rigidly. Nothing is weight-painted in Blender.
   - **Built once per level on the render thread**, never from BuildRope: NewGame rebuilds the
     chain on the physics thread, where there is no GL, and the skin carries on over the new
     links because the bind pose is always the same straight hang.
   - **One tick behind the solver, deliberately** - read before the step, with her, so her hands
     and the rope are drawn from the same instant (see UpdateRopeSkin).
   - The link boxes are hidden under the skin; the panel's Rope section puts them back.
   - **A cut splits the skin into pieces** (2026-09-24). `RopeMeshInput::cuts` clamps each
     section's weights inside its own links, puts every triangle wholly on one side (by its
     middle), and caps both sides of the cut with the tassel - hanging from the upper piece, turned
     up over the lower. The bind and the bones do not change, so the tick only notices the chain's
     cut joints differ from the skin's (`CheckRopeSkinCuts`) and PreRender re-uploads the mesh; a
     restart re-weights it whole the same way. 6 more checks, pulled apart: worst edge under 1.6x
     its bind length cut, over 3x uncut.
5. **Rope preset** (ends, collars, twisted tile) plus grip-at-a-point with hand IK.
   *(The preset is BUILT with step 4: `rope_segment`, `rope_ring` (its unapplied node scale of 2
   is honoured), three `rope_collar`s stacked by their measured height, `rope_tassel`, all baked
   into the one skinned mesh. Open: grip-at-a-point and hand IK, which go with the rope
   animation work - she currently holds a link's centre, which is the lowest link from the
   ground, down among the collar and tassel.)*

   **Found 2026-09-24 while looking at the swing, and FIXED: the model was tilted about its
   feet while the collider tilts about its centre** (SyncArcherAnimation placed the feet
   straight below the centre, then applied the roll). The error grew with the swing - about
   0.39 at 24 degrees, 1.3 at 90, the model lying beside its own collider. Nothing to do with
   gripping. With a debug view for it (`archer_debug_view rope_attach`, or the panel's Rope
   section; distances in archer_state) the joint measures 0.000 open and the drawn hands sit a
   CONSTANT 0.18-0.20 from the attachment across -24..+19 degrees of swing - what is left is
   Hanging_Rope's grip being higher above her feet than the top of the 1.8 box, a fixed offset
   to take out one of two ways: hang the MODEL by its hands (place it so the clip's measured
   grip meets the joint), or move the joint's body anchor up to the clip's grip height.
6. **Gameplay**: rope arrow, cutting, growth - if wanted. *(Growth is now planned in full: see
   "Growing vines", sections 7-14 below.)*

## The cave vines (2026-09-26)

Four big long vines hung into the terrain bay's cave (in front of its back wall, Backdrop.h),
added to DeclareVines under `#if ARCHER_TEST_BAY`: two BEHIND her (z -1.1 .. -6.6 - off the wall's
crest onto the island's step; over the island's left end onto the small stone at x -30) and two IN
FRONT (z 2+ where they hang - from above the frame over the island's front lip onto the stone at
x -19; onto the stone at x -14). Thickness 1.5 .. 1.8, which also lengthens the tile - 3 .. 6
tiles for 12 .. 21 units. Resting points sit 0.2 over the top they lie on. A vine that starts
hanging straight down needs `up = +Z`, or the frame starts parallel to the tangent. The front two
start at y 33+: begun at 20, their tapered tops showed in mid-air at the camera's widest.

## The trunk's shading (FIXED 2026-09-29)

The trunk rendered faceted, one flat shade per triangle, although `vine_trunk` is smooth-shaded in
Blender. The cause: `TrunkDeform` set `SplineDeformParams::f_flat_normals`, written in step 2 for
the faceted placeholder, which replaces every normal with its face's. The file was never the
problem - measured on archer.glb, `vine_trunk` and `vine_curl` have not one split normal between
them, and their vertex normals sit 23-36 degrees off their faces, which is what smooth looks like.

Turning the flag off exposed two more things, both fixed in `core/SplineDeform`:

- **A rotated normal is not a deformed normal.** The stretch-to-fit is a non-uniform scale, the
  taper makes the trunk a cone and the twist shears it. The carried normal now goes through the
  deform's inverse transpose (`CarriedNormal`, derivation at the function): on a lumpy tapered
  twisted test tube, 0.43 degrees off the true surface against 15.5 for rotating alone. It is
  exactly the old rotation on a straight, untapered, untwisted run, so the rope is unchanged.
- **The tile's end rings disagree.** A tile smoothed on its own leans its end-ring normals toward
  its middle: `vine_trunk`'s two rings sit on the same positions exactly but their normals differ
  by 11 degrees median, 16 at worst, so every join creased. Blender hides this only when an Array
  modifier merges the copies first. `f_weld_seams` averages each pair across a join, leaving a pair
  more than 60 degrees apart alone as an authored hard edge. The vine turns it on.

7 new checks in `tools/spline_test.cpp` (46 in all), and `make rules` still passes at 751.

**For the next tiles:** a smooth tile needs nothing special now. Flat-shaded art should turn
`f_flat_normals` back on for its own species. Authored hard edges survive both the deform and the
weld.

---

# Growing vines

Agreed direction 2026-09-29, not yet built. Arrow types are chosen with the number keys and shown
bottom-right. An arrow of the vine type that sticks in the underside of the terrain grows a vine
down from where it hit. A normal arrow in the same place grows roots out of the underside first,
and then a tuft on the top above (section 11). A vine arrow in a wall grows a creeper. It is a
visual effect first, to see whether it reads, and a mechanic later. Five kinds of growth are
planned, and they are one system. The walker and the later animations also need platform edges
the stage can look up, which is section 15.

The argument for that is the same as section 1's: everything here is **already parameterised by
distance along a curve**, so growth is revealing a curve up to a length `g` that rises over ticks.
Leaves already know their `s`, so they unfold as the tip passes them. None of the five kinds is a
new mechanism. Each is a different *path generator* and a different *recipe* of the three
operations.

## 7. What is already in place

| piece | where | what growth takes from it |
|---|---|---|
| curve, frame, arc length | `core/Spline` | the grown shape, and `s` for everything along it |
| deform, now smooth | `core/SplineDeform` | the trunk, root, cane or strand; needs a *reveal* (section 9) |
| scatter | `ScatterVineLeaves` | leaves with their `s`, kept out of the blocks |
| the hit | `Stage::TickArrows` -> `StageEvents::ArrowHit{arrow, point, normal, speed, block}` (Stage.h:1427) | where, which face, which block. `normal.y == -1` **is** "an underside" |
| the stuck arrow | `Arrow::f_stuck`, `ARROW_STUCK_TICKS` 600 | the anchor. It expires after 10 s, and a vine must outlive it |
| HUD | `UIOverlay` via `ApplicationArcher::DrawOverlay`, e.g. `DrawVitalsHud` | the arrow card. Not ImGui, so it survives `make ship` |
| input | `INPUT_ARCHER_*` (next free is `INPUT_LAST+20`), `AddKeyMap`, `NameAction`, `ArcherInput` | the number keys. Nothing maps a digit today |
| wind leaves | `leaf_small`, `LeafSwarm` | bamboo's leaves, and what a withering vine drops |
| leaf flutter | `material_t::wind_mode` 1 (LEAF) | every new leaf kind, as the vine leaves do now |
| rope | `BuildRope` chain + `RopeMesh` skin, which already **takes a tile** | the swingable vine, later: RopeMesh with `vine_trunk` |
| determinism | `Stage::HashState`; the `SetVisualOnly` subtrees are skipped by the hash | visual growth costs the trace nothing; rules state must be hashed |
| async | `core/BackgroundWork.h` | not needed. One walk is microseconds |

## 8. Arrow types

**The selection belongs to the rules**, because what an arrow does on landing is gameplay the
moment any kind has a mechanic, and a replay has to reproduce it.

- `Stage` gets `int arrow_kind` (the selected kind) and `Arrow` gets `int kind`, copied from the
  selection in `Loose`. A change while nocked applies to the arrow already on the string, so what
  the HUD shows is what flies. Both go into `HashState` in member order. The archer test's trace
  will then differ from its first tick with every value still 0; that is expected, rewrite it.
- `ArcherInput` gets `int arrow_select = -1` (an edge: the kind asked for this tick) and
  `f_arrow_next`, for a pad.
- Keys `1`..`5` map to `INPUT_ARCHER_ARROW_1..5`, and one `ARROW_NEXT` goes on a pad shoulder or
  the d-pad. **They are recorded** (not `SetRecorded(false)`), because they change rules state.
  `NameAction` names them `arrow_1`...; add them to `archer_hold`'s table so an agent can pick one.
- `StageEvents::ArrowHit` gains `kind`. `SignalArrowHit`'s cue can then tell a vine arrow's thud
  from a normal one's.
- **HUD, `DrawArrowHud`**: a card in the bottom-right corner in the vitals card's style
  (`TITLE_*`). It holds the selected kind's name, large, and under it a row of the kinds with
  their keys (`1 Arrow  2 Vine`) with the current one lit. It flashes briefly on a change. The
  kind reaches it through `ArcherSnapshot` like the vitals do. Later it gets an icon per kind
  (`AddSprite`) and, if special arrows become limited, a count.
- The arrow looks different: an `arrow_vine` node if the file has one, else the normal arrow with
  a green-tinted head, so a vine arrow can be told apart in flight.

Kinds to start with: `ARROW_NORMAL`, `ARROW_VINE`. The rest (`ARROW_BAMBOO`, `ARROW_THORN`,
`ARROW_GRAPE`) land with their species. Roots have no arrow of their own; they are what a normal
arrow leaves behind.

## 9. Growth, seen

### Revealing the curve without popping

`DeformAlongSpline` **stretches** its copies to fit the range. Grown by moving `end`, the tile count
would step and every copy would slide at each step. So the deform gets a reveal instead of a range:

- `SplineDeformParams::grown` (< 0 = all of it). The copies are laid out **for the full length**, so
  every copy stays where it will finally be. A copy wholly past `grown` is skipped. A vertex of a
  partly grown copy that lies past `grown` is pulled back onto the tip.
- The end taper runs against `grown`, not the curve's end, down to a **growing tip** of scale about
  0, so the front is a closed point rather than a sawn ring. `tip_scale` still applies once fully
  grown.
- Checks for `spline_test`: no vertex past `grown`; every copy's vertices wholly inside `grown`
  identical to the full build; the tip closes to a point; `grown == length` identical to the
  plain call.

### The leaves unfold

Each scattered leaf already carries `s`. At a growth front `g`, a leaf appears once `g > s + delay`
and over the next `unfold` distance scales from 0 and swings from lying along the stem up to its
lift. That is a young leaf opening, and it reads as growth far more than the length does. Fruit and
thorns do the same with their own delays. Grapes come last, a cluster swelling some time after
the leaves near it.

### The clock

In **ticks**, as always. `g(t) = L * ease(t / grow_ticks)`, fast out of the arrow and slowing to
full length. Slowing matters: a constant rate looks like a progress bar. The view records the hit's
tick and computes `g` from the snapshot's tick, so a replay, `sim_step` and a paused screenshot all
show the same growth at the same tick.

### Where the work happens

- The hit is a rules event on the physics thread, which has no GL. The view **queues** a growth
  request, and `PreRender` builds and uploads it, the way `RegenerateTerrain` does.
- While a vine grows, PreRender re-deforms it every frame: a 12-tile vine is about 1.8k vertices,
  well under a tenth of a millisecond, and `SetMeshData` on the render thread. Once full grown it is
  frozen and never touched again.
- Leaves come from a **pool per kind**, pre-created at Init under `vine_group` (visual only) and
  handed out on growth, like the wind leaves and the hit popups. No object is created mid-tick.
- Everything goes under `vine_group`, so none of it enters the state hash. A **cue** (`vine_grow`,
  rustle and creak) is the one thing that does, through `cues`. Add it knowing the `.cues`
  baseline moves.
- **Live growths are capped at 32** (agreed 2026-09-29). Past that, the oldest withers, see
  section 12. `NewGame` sets a request flag, and PreRender clears them. At 32 the leaves are the
  cost to watch: 32 vines of about 50 leaves is 1,600 leaf Objects. They share a mesh per kind, so
  the draw count stays low, but each one still has a transform to update. Measure it with
  `renderer_timings` before reaching for anything else. The fix, if needed, is not baking: LEAF
  wind mode bends each leaf about its own origin, which a baked mesh no longer has. It would be a
  per-vertex stem position, or instancing.

### Where it starts

The hit point is on the **block's** face, but the drawn terrain is rounded outward and hangs a
belly and drips below it (up to 0.35 on an underside, Terrain.h). A vine started at the box face
would begin inside the drawn rock, or in mid-air under a drip. So:

- `Terrain.cpp` exposes its field as an engine-free query, `TerrainDistance(blocks, params, p)`
  (and a gradient). The start is then marched out along the hit normal to the drawn surface and
  set a radius back in, so it beds in. The same query later serves the walker's collisions, and
  any other prop that wants to sit *on* the drawn terrain and not on the box.
- Until that exists, offsetting by the block's `round_r` plus a bed-in gets it roughly right.

## 10. The path generator: a walker

The hand-typed vines stay hand-typed. Grown ones come from a **walker**, engine-free in
`Vine.cpp` (or a new `VineGrowth.cpp`), tested in `make rules`:

```
VineGrowth GrowVine(const VineSpecies& species, vec3 anchor, vec3 surface_normal,
                    uint32_t seed, const std::vector<StageBlock>& blocks);
```

It returns one `VinePath` per strand plus its branches, each with where it leaves its parent
(`parent`, `s_on_parent`). It steps about 0.08 at a time:

- **heading** = keep going (`stiffness`) + gravity or anti-gravity (`habit`) + wander (a smooth
  hashed noise of `s` with its own amplitude and wavelength, never white noise, or it kinks) +
  surface-hugging for a creeper;
- **collision**: a step that would bring the centre within a radius of a live block (and later of
  the drawn surface, via `TerrainDistance`) is slid along the face instead. A hanging vine that
  reaches the ground lies along it for a while and stops, which is what a real one does;
- **stop** at the length drawn for it, or when it has crept a set distance flat.

The whole shape is walked **once, at the hit**, and growth only reveals it. A hanging vine
therefore does not droop more as it lengthens. That is right for anything that holds its shape
(roots, bamboo, thorns). For a long hanging vine it may want a *sag while growing* later, the tip
end relaxed by a few verlet iterations per frame, visual only. Try without first.

**Deterministic by construction.** The seed is the hash of the quantised hit point and the arrow's
slot (PlaceHash, never `RRandom`, see the rrand note), so the same shot always grows the same vine.
Checks: a grown vine never enters a block; an underside vine ends lower than it started; bamboo
stays within 8 degrees of vertical; roots stay short; the same input gives bit-identical points;
branches start on their parent's surface.

**Branches** are strands of their own, started at `s_on_parent` and turned off the parent's
heading. Each one's clock starts when the parent's front passes that point, and its base sits a
little inside the parent's radius under a tapered start. All the strands of one growth share one
mesh.

## 11. The species

One table, `VineSpecies`, holds per kind: the pieces (node names, each falling back to a
placeholder as the current vine does); the walker's `habit`, lengths, stiffness, wander, gravity;
branching; the tile layout (stretched to fit, or at a **fixed period**); the growth clock; and the
scatter (`VineParams` as today, plus fruit and thorns). In code first. It moves to a JSON file
beside the cues once the numbers want tuning live, since the asset watcher already hot-reloads.

**Every plant has roots** (agreed 2026-09-29). A growth is a *plant*: an optional **roots** part
and a **shoot** part, each a set of strands from the walker. The roots grow first, the shoot once
they are nearly done. Roots are only worth growing where they can be seen, which means out of an
underside. Into a top or a wall they would be inside the rock, so there the species simply shows
none, which is what every vine does today. The roots are a species of their own (the first column
below) that any plant can name as its roots, so all plants share one root look.

**What each arrow grows, by the face it hits:**

| hit | normal arrow | vine | bamboo | thorny | grape |
|---|---|---|---|---|---|
| **underside** | roots down, then a **tuft on the top above** | roots, then a hanging vine | roots, then a cane that curves down and turns up | roots, then hanging, coiled | roots, then hanging, fruiting |
| **wall** | roots, drooping (a tuft until 2026-09-30) | a **creeper** along the face and over the lip | a cane out of the wall, bending up | a creeper, dense | a creeper, fruit hanging off it |
| **top** | a small tuft | a creeper along the top and over an edge, to hang | canes straight up | a low thicket | a creeper |

**The tuft on the top above.** A normal arrow in an underside plants a seed *through* the
platform. Its roots come out of the underside where the arrow is, and the plant they belong to
comes up on the top directly above: a small foliage clump, with Foliage's own meshes scaling in.
It needs the top above to be exposed (an edge-list span, section 15) and the platform thin enough
to be believable, about 2 units at most. Otherwise it is roots only. (Confirmed 2026-09-29: the tuft comes up on the top above, not at
the roots.) Into a wall or a top a normal arrow grows a small tuft where it sticks, with no roots.

| | roots | bamboo | vine | thorny | grape |
|---|---|---|---|---|---|
| from | undersides, under any plant | tops, walls, undersides | undersides, walls, tops | undersides, walls | undersides, walls |
| habit | hang, heavy wander | climb, straight up | hang, then creep on contact | hang, coiled | hang, like the vine |
| length | 0.3-0.9, 2-4 roots | 2-4, 1-3 canes | 3-8 | 2-5 | 3-7 |
| grows in | ~0.4 s | ~0.6 s, fast | ~2 s | ~2.5 s | ~2 s, fruit after |
| branches | 1-2 per root, near the tip | none | a few side runners | many, short | a few |
| tile | `root_tile`, dark brown, thin | `bamboo_tile` = one internode, **fixed period** | `vine_trunk` | `thorn_tile` | `vine_trunk` or `grape_tile` |
| overlay | - | - | `vine_curl`, **optional** (with or without the wrap) | 2-3 strands coiled round one path | - |
| at the end | fine point (tip scale 0.05) | `bamboo_tip`, the sharp point | tapered | tapered | tendril curl |
| along it | nothing | `leaf_small` at the upper nodes | `vine_leaf_1/2` | `thorn_1..n`, dense | `grape_leaf_1/2`, `grape_cluster` |

Notes per kind:

- **Roots.** They must stay small and quick. Every normal arrow into an underside leaves them,
  and they sit under every other plant, so they are seen dozens of times. A little earth falling from the hit (a few debris
  specks, or the Leaves swarm with a soil tint) sells them more than their length does.
- **Bamboo** is the one that does not stretch. A culm is a whole number of internodes, laid at the
  tile's own period, so every node ring sits where the artist put it. The growing tip is the
  `bamboo_tip` piece riding the front (placed at `g` in the frame there), not a taper. Real
  internodes shorten toward the top; a per-copy scale along the cane would give that cheaply.
  Leaves come in sprays on short twigs at the upper nodes, from `leaf_small` in LEAF wind mode.
- **Vine** is today's vine grown. The wrap overlay is a per-species flag, so "vine" and "bare
  vine" are two rows sharing everything else.
- **A creeper** is the walker's *hug* habit. It keeps a set distance off the face it grew from, and
  wanders in the face's plane with a bias up and along. When it reaches the lip above (the edge
  list gives the height of the face, section 15), it turns over onto the top. At a side edge it
  turns the corner or hangs off. The frame's `up` starts as the face normal, so the leaves stand
  out of the wall, and the scatter's block test already turns buried leaves to the open side.
- **Thorny** is where section 1's **Derive** operation finally earns its place: two or three thin
  strands wound as a helix *around* the walked centre line (radius 0.1-0.2, a pitch that shortens
  as it goes, different phases), which is the dense, tightly curved look, rather than one wandering
  strand. Thorns scatter along the strands, tipped **back toward the base** as real ones hook.
- **Grape** grows like the vine, with its own leaves, plus clusters every ~1.2 along it. A cluster
  hangs by **gravity** (its own down, not the trunk's frame) from a short stem, and swells in after
  the leaves around it. The tendrils are small log-spiral curls, the curl shape section 3 had.

**Asset convention for the new pieces**, the same as the existing ones: a tile runs along
Blender -Y with matching end rings (smooth shading is fine now, the joins are welded); leaves,
thorns, fruit and tips have their origin at the attach point, blade or point along +Z (glTF), and
upper face +Y. A cluster is authored hanging, with its stem at the origin. Names as in the table.
Each kind falls back to a coloured placeholder until its nodes exist, so the system can be built
and tested ahead of the art.

## 12. Withering, and the level changing under a vine

- **Withering** (agreed 2026-09-29) is growth in reverse. The leaves let go first, handed to the
  wind `LeafSwarm` so they drift off rather than shrink away, and are tinted toward brown over a
  moment beforehand. Then `g` falls from the tip back to the arrow, the roots last. It is how the
  oldest growth leaves when the cap of 32 is reached, and it gives a vine arrow a lifetime if that
  is ever wanted. The swarm needs a way to take a leaf mid-flight from a given pose and kind; today
  it spawns its own.
- **The block it grows from crumbles** (bridge_crumble_plan): its vines wither fast, or better,
  fall with the rubble. The ArrowHit event's `block` is kept with the growth for exactly this.

## 13. Mechanics, later

Each kind has an obvious job, which is why the arrow type is rules state from the start:

- **Vine: a rope.** A grown hanging vine becomes swingable: `BuildRope` anchored at the arrow, its
  length the grown length, drawn by `RopeMesh` with `vine_trunk` as the tile, which RopeMesh
  already takes. The swap from the grown mesh to the chain is invisible if the swingable kind's
  walker hangs almost straight. This is section 5's rope arrow, and it wants grip-at-a-point.
- **Bamboo: a pole and a lift.** It climbs like a ladder. Grown under a crate or under her, it
  *lifts*, and the fixed-period growth makes the lift speed a clean number. The sharp tips hurt
  anything standing where it sprouts.
- **Thorny: a barrier.** It blocks a passage or a chaser (the crumble chase), hurts to touch, and
  cuts a rope that swings through it.
- **Grape: a pickup** that calms her: it lowers fear and exertion (vitals_plan), so breathing and
  heartbeat settle. Shot, a cluster drops.
- **Roots: handholds.** Hanging from an overhang's underside by a root is a small traversal verb,
  if normal arrows should ever matter for climbing. Otherwise they stay cosmetic.
- **Limited special arrows**, found in the level, would give the HUD its count and the kinds their
  value.

For any of this the growth itself moves into `Stage`: the seed, anchor, kind and start tick are
rules state (hashed), and the walker, already engine-free, runs there. The view keeps only the
meshes. That move is cheap exactly because sections 9 and 10 keep the walker out of the view now.

## 14. Order

0. **Smooth trunk normals.** *(DONE 2026-09-29, above.)*
1. **Arrow kinds**: the Stage fields and hash, the keys, the HUD card, the tinted arrow. `make
   rules` checks that selection edges set the kind, `Loose` copies it, and the hash covers both.
   Then rewrite the archer test's baselines. *(DONE 2026-09-29.)*
   - `ArrowKind` in Stage.h (`ARROW_NORMAL`, `ARROW_VINE`), `Stage::arrow_kind`, `Arrow::kind`,
     `ArrowHit::kind`, `StageEvents::shot_kind` and `f_arrow_kind_changed`.
   - `ArcherInput::arrow_select` / `arrow_step`, read by `Stage::SelectArrow` from the *raw* input,
     so a pick during the get-up still counts.
   - Keys 1..5 and d-pad left/right, recorded, named `arrow_1..5` / `arrow_next` / `arrow_prev`,
     and in `archer_hold`. A key for a kind that is not built yet does nothing.
   - The selection **survives a restart** (Reset leaves it). A recording's state line carries it
     (`arrow_kind`), and a file without one replays with normal arrows.
   - `DrawArrowHud`: the bottom-right card, with a rim flash for 30 stage ticks on a change and
     the panel's "arrow card" box.
   - A vine arrow wears greened copies of the arrow's materials (`BuildArrowDress`), swapped by
     index in `SyncArrowViews`. `archer_state` reports `arrow_kind` and each live arrow's `kind`.
   - 16 checks in `make rules` (767 in all). The archer test's trace parted from tick 0 in `her`
     and `world` only, the two new hashed fields. Every other part and all 84 cue lines were the
     same, so the trace was rewritten and `.cues` left alone. Checked in-app: the card, and a vine
     arrow stuck beside a normal one.
   - Not done here: the cue does not yet tell a vine arrow's thud from a normal one's. That is
     for step 5, together with the `vine_grow` cue, so the `.cues` baseline moves once.
2. **The reveal** in `SplineDeform` (`grown`, the tip), with its `spline_test` checks.
   *(DONE 2026-09-29.)*
   - `SplineDeformParams::grown`, `grow_tip_length` (0.35) and `grow_tip_scale` (0).
     `SplineDeformTaper` includes the tip, so a leaf seated by `VineRadiusAt` will follow it.
     `DeformAlongSpline` returns the copies laid so far.
   - **The tip is a cone, not a needle.** It closes by an ease-out, 1 - (1 - k)^2, whose slope is
     finite at the point and zero where it meets the trunk. The plain smoothstep closed with zero
     slope and drew the last of it hair-thin. The closing fades out over the last tip length, onto
     the ordinary end taper.
   - 11 checks, 57 in `spline_test`:
     - grown to the end is the plain sweep, bit for bit, and grown 0 lays nothing;
     - nothing lies past the front, and the front closes to a point;
     - all 1,440 vertices more than a tip behind the front are the finished sweep's own, and the
       copies are whole ones of the finished layout (3 of 4);
     - the point is a cone (radius ratio 1.94 at double the distance);
     - grown along a bend in steps of 0.01, no vertex moves more than 0.0102, so nothing pops;
     - every new copy is born at the front as a point, and no NaNs appear;
     - the last step lands exactly on the finished sweep.
   - Not yet passed through `Vine.cpp`: `BuildVineTrunk` / `BuildVineOverlay` get a `grown`
     argument with step 5, where something first grows.
3. **The edge list** (section 15) with its `make rules` checks. It comes before the walker, which
   needs it to turn a creeper over a lip, and it can land while the art is still coming.
   *(DONE 2026-09-29.)*
   - The types are `StageSpan` / `StageEdge` / `StageCorner` in Stage.h. The builder is
     `apps/archer/StageEdges.cpp`, added to the app and to all five `make rules` exes.
   - Queries: `Stage::NearestEdge`, `SpanAt`. A platform is a floor but never a wall. A gap
     narrower than `STAGE_EDGE_JOIN` (0.02) is no gap.
   - **Rebuilt by** Reset, by `KeepBlockLayout` (the editor's moves), and by the tick after the
     count of live blocks moves, which is all play can do to blocks: a wall kicked in, a stone
     crumbled. The full fingerprint over every block (`RefreshEdges`) is for code that edits
     blocks by hand. It is deliberately not run per tick, because `PredictLanding` ticks a copy of
     the Stage up to 30 times a tick.
   - The main level has 52 spans, 80 edges and 24 wall feet.
   - 28 checks in `make rules` (809 in all): each rule on a layout built for it, then the main
     level's landmarks (the step's feet, the high ledge's grabbable 4.2 lips, the first gap, no
     edge where the bay meets the start), then the refresh and the editor path. The archer test
     replay is unchanged, since nothing reads the edges yet.
   - The view is `archer_debug_view edges` or the panel's "floor edges" box: white floors, drop
     lines (red past `VITALS_DROP_FROM`, amber past 0.5, grey a step), a blue bar on a grabbable
     lip, green feet. `archer_edges` (with `x0`/`x1`) gives the numbers. Both come out of the
     snapshot. World level only, like the wind view. Checked in a screenshot at the start.
   - Not done here: fear reading the edges instead of its column scan, and Foliage sharing the
     spans. Each changes what exists today (the trace, the plants), so each is its own change.
4. **The walker** and a `VineSpecies` row for the vine, checked in `make rules`.
   *(DONE 2026-09-29.)*
   - In Vine.h/.cpp: `VineSpecies` + `VineSpeciesFor`, `VineStrand` / `VineGrowth`, `GrowVine`,
     `VineGrowthSeed` (from the hit point and the arrow slot) and `VineBlockDistance`, the blocks'
     2D signed distance the walk keeps clear of.
   - The walk takes 0.08 steps and keeps a point every 0.3. The heading turns toward down (or up)
     by `gravity`, sideways by a smoothed hashed noise of distance (`wander` over
     `wander_wavelength`, some of it into the screen), and back toward its start's depth.
   - A step that would come nearer a block than the trunk's radius plus `clearance` slides along
     the face instead. Only a step coming *nearer* does, so it can leave the face it grew from. A
     step straight into a face creeps to one side, fixed per strand. On a floor it counts toward
     `rest_length` and then stops. A strand that creeps off the end falls on and hangs.
   - The path starts 0.1 inside the rock, so the trunk comes out of it. Branches (0-2 for the
     vine) leave the main strand's built curve, turned 35 degrees to a hashed side, from inside
     it.
   - 16 checks (825 in all):
     - under slab one it grows, ends lower and builds;
     - under the high slab it hangs 2.7 degrees off straight down, for a length in range;
     - under a low ceiling it lands, lies at its keep and crept 1.3 along the floor, stopping
       short of its full length;
     - off the step's wall it grows clear;
     - the same shot is bit-identical, and another arrow gets another seed;
     - 200 seeds under slab one and under the ceiling: none enters a block, all build, all hang
       lower, all 200 land, and all 198 branches start on their parent's curve.
   - **Known, and left:** the built curve rounds the walk's corners, so where a hanging strand
     lands it can dip up to 0.09 inside its own keep (the walked points never do). On a floor
     that sinks the trunk's belly a little into the grass, which the terrain hides. If it ever
     shows, walk with a keep a little larger than the drawn radius.
5. **The vine arrow grows a vine on an underside**, visual only: the queue, PreRender re-deforms,
   the leaf pool, unfolding, the cue, the cap of 32. Judged in screenshots at fixed ticks under
   `sim_step`, which the tick clock is there for. The start is offset by `round_r` until:
   *(DONE 2026-09-29, and not offset: the vine starts inside the box, and so inside the drawn
   rock, which hides its first few tenths. Step 6 still wants the query, for the walk's
   collisions against the drawn surface.)*
   - **The trigger:** `HandleEvents` catches an `ArrowHit` of `ARROW_VINE` with `normal.y < -0.5`,
     on the world level only.
   - **Physics thread** (`StartGrownVine`, `StepGrownVines`):
     - it walks the vine with `GrowVine` off `stage.blocks`, scatters its leaves and borrows them
       from a pool;
     - each tick it computes every strand's front from the ticks since the strike
       (`VineGrowthFront`, eased out over `grow_ticks` = 150);
     - branches start when the main front passes their root, and run to finish together with it;
     - leaves open by `VineLeafOpen` (0.45 behind the front, over 0.6), scaled in and folded up
       from lying along the stem, and any still closed at the end open over 0.4 s;
     - a vine grown and open is left alone.
   - **Render thread** (`DrawGrownVines` in PreRender): re-deforms a slot's trunk and wrap with
     `grown` only when its fronts moved. Fully grown is the plain sweep, identical to a static
     vine.
   - **Shared:** `grown_shared` under `grown_mutex` (paths on refill, fronts every tick).
   - **Pools:** 32 slots (a ring; past it the oldest goes, withering is step 9) and 512 leaves per
     kind, all under the visual-only `vine_group`. Nothing is created mid-tick. A restart clears
     them.
   - **Sound:** `vine_grow` in archer.json, a PLACEHOLDER (rock_crumble_2 at pitch 0.75, quiet,
     by distance and panned). It wants a rustle and creak of its own.
   - **Read-out:** `archer_state` has `grown_vines` (slot, where, strands, leaves, length, front,
     ticks, done).
   - **Checked in the game:** a vine arrow under slab one grew 3 strands, 4.7 long, 37 leaves, from
     the underside to the floor, lying along it at the foot. A second, stepped with `sim_step` and
     shot at ticks 6/20/40/70/120, grows down with a pointed tip and its leaves opening behind the
     front. The restart cleared both. The archer test replay is unchanged.
   - **Cost, and the fix it needed.** PreRender went from about 0.5 to 5.6 ms while a vine grew
     (debug), because every vertex worked out its own curve frame and twist though a tile's
     vertices share a few heights. `DeformAlongSpline` now does those once per distinct height:
     the same values, three times faster on a bench. In the game a growing vine now adds at most
     about 2.2 ms. Once grown it costs nothing.
     - If several growing at once ever shows: every copy more than a tip behind the front is
       already final, bit for bit, so those can be cached and only the last one or two re-laid.
       The weld needs the copy before the first re-laid one included, then dropped.
     - **Or on the GPU** (the user's suggestion, 2026-09-29). The deform is a pure function of
       the tile vertex, the curve and `grown`, so a vertex or compute shader could do it. It would
       upload the tile once and the curve's sampled frames as a buffer, and each frame pass only
       the front. The CPU version stays as the reference the tests check it against. 2.2 ms is
       accepted for now, for a few events at a time.
   - 5 more checks in `make rules` (829): the clock's shape and the leaf opening.
   - Not done: `arrow_hit` does not yet tell a vine arrow's thud from a normal one's.
6. **`TerrainDistance`**, for the start and for the walker's collisions.
   *(DONE 2026-09-29, as `TerrainField` rather than one function.)*
   - **`apps/archer/TerrainField.{h,cpp}`, engine-free:** the terrain's field and its shape
     helpers, moved unchanged out of Terrain.cpp (which kept the mesher and calls
     `TerrainFieldAt` / `TerrainFindFloating`). `TerrainParams` and `TerrainRegion` moved with
     them. Proven unchanged by the build's own stats, identical before and after:
     - bay 0, 20,178 tris (dip 0, rise 4.80);
     - bay 1, 10,071 tris (dip 0, rise 5.45);
     - the back wall, 66,672 tris.
   - **`TerrainSurface`:** one region's drawn surface to sample (Build / Distance / Normal), made
     from the same region and params as its mesh.
   - **`VineField` in Vine.h:** what a vine grows against.
     - `VineBlockField` is the boxes, the old behaviour, bit for bit.
     - `VineLevelField` is every box except the melted blocks, which are their surfaces instead.
     - `GrowVine` and `ScatterVineLeaves` take a field. The blocks versions wrap
       `VineBlockField`, so the static vines and every earlier check are unchanged.
   - **The start is marched out** along the normal (0.05 at a time, 3 units at most) to where the
     field is open. So a strike on a box the terrain has drawn a belly under grows from the belly.
     The collisions and the leaves' burial test then use the drawn surface too.
   - **`VinePath::f_rooted`:** a grown strand's buried start has no taper. Seen in the bay: the
     trunk came out of the stone pinched like a stalk. It is now full thickness out of the rock,
     and so is a branch out of its parent.
   - `StartGrownVine` builds each bay's surface per strike (the same regions and default params
     `RemeshTerrainBay` meshes with) and grows against a `VineLevelField`.
   - 5 checks (834):
     - with no surfaces the level field is exactly the boxes;
     - a floating stone's drawn belly hangs below its box (-0.53 just under it);
     - against the boxes the vine started 0.61 inside the drawn rock, against the drawing on the
       belly, 0.65 lower;
     - neither the walk nor any of 24 leaf tips goes into the drawn rock;
     - rooted is full radius at the root.
   - **Checked in the game:** a vine arrow into stone two's underside (x -14, 6.4) in the terrain
     bay grows out of the bottom of its drawn belly at full thickness, down to the hill under it
     and along it. The archer test replay is unchanged.
7. **Plants have roots**: the roots species; the normal arrow's roots, then its tuft on the top
   above.
   *(DONE 2026-09-29.)*
   - **`VINE_SPECIES_ROOTS`:** 0.3-0.9 long, 0.04 steps, hard short wander, 1-2 forks near the
     tip, thickness 0.35 (0.035 on the placeholder octagon), 24 ticks to grow.
   - **`GrowRoots`:** 2 to 4 roots from one strike, spread a hand's width along the surface and
     leaning out, every fork's parent kept pointing into the plant's list.
   - **`ScatterTuft`** in Foliage: 3-5 of the garden's own plants, mostly grass, now and then one
     low fern or one flower, on a disc square to `up`, staggered by up to 10 ticks.
   - **The app's growth is now a PLANT** (`GrownVine`): strands each with a look (vine or root), a
     species and a start tick, plus tufts. `StartGrowth` decides per strike:
     - vine arrow into an underside: roots, then the vine `GROWN_ROOTS_LEAD` (16) ticks later;
     - normal arrow into an underside: roots, then a tuft on the top above, if that top is open
       (`SpanAt`) and the platform at most 2 thick;
     - normal arrow into a wall or a top: a small tuft where it stuck, marched out onto the drawn
       surface (`VineMarchOut`);
     - vine arrow into a wall or a top: nothing yet (step 8).
   - Not on BREAKABLE or CRUMBLE blocks, until section 12 makes growths fall with them.
   - **Two rings of 32:** vine-arrow plants, and a normal arrow's roots and tufts. Normal shots
     never push a vine out.
   - The root look is `root_tile` from archer.glb if it is added, otherwise the placeholder
     octagon in `ar_root`, a dark earth brown. Roots taper to a 0.06 point and twist harder.
   - **Tuft plants:** a pool of 64 per foliage kind, the garden's meshes and materials, so they
     sway the same. They scale in over 30 ticks.
   - `archer_state` `grown_vines` gains `roots` and `tufts`.
   - 8 checks (842), over 100 strikes: 2..4 roots a strike, 451 forks, 0.32-0.92 long, at most
     0.046 thick, none into the rock, all lower. The tuft is 3-5 plants, mostly grass, on the
     surface it was asked for, a wall's out of the wall, and the same spot gives the same tuft.
   - **Checked in the game:**
     - a normal arrow under slab two grew 2 forked roots and a 5-plant tuft on its top;
     - a vine arrow under slab one grew 3 roots and then its vine;
     - a normal arrow into the ground grew a tuft round it.
     The archer test replay is unchanged, though its shots now grow roots and tufts: all
     visual-only, no cue.
   - **Wants art:** a `root_tile` would soften the placeholder's blockiness up close. The faceted
     octagon reads as roots at play distance, as short dark spikes.
8. **Creepers** from wall hits.
   *(DONE 2026-09-29.)* `StartGrowth` grows a `VINE_SPECIES_CREEPER` in the vine's look for a
   vine arrow into a wall or a top. Checked in the game: a vine arrow into the step's right face
   (x 9, y 1.49) grew a 6.4-long creeper that comes out of the face, curls up over the lip and
   runs along the top. The doubled tuft is seen round a normal arrow in the ground. The archer
   test replay is unchanged.
   - **The hug habit** (`VineSpecies::hug`, `hug_reach`, `hug_gap`, `climb`) is in `WalkStrand`,
     and the hanging plants are untouched by it. Within `hug_reach` of a surface:
     - the heading is drawn to lie at the keep;
     - the bias is taken in the surface's own plane: up at `climb` while climbing, gravity once
       over a lip, so on a top it is nothing and persistence carries it across;
     - the wander is in the surface's plane, its into-the-screen part damped to 0.3 on a top,
       where it only zigzagged against the hold back to the walk line.

     Further from any surface it hangs. It never rests.
   - **It needed no edge list:** the field's normal turning round a corner is what bends it over
     the lip.
   - **The one trap, found by printing a path:** just past a corner the normal already points up
     while the strand is still rising in the air beside it. Counted as "over the lip" there,
     gravity pulled it straight back down the face it had climbed (36 of 100 seeds did). It
     counts only once the normal is up AND the heading is mostly level, and the pull is 10.
   - **It starts** lying against the face at its keep, heading up the face (or across a top to a
     hashed side). Its leaves' frame `up` is the face normal, so they stand off the wall.
   - **`VINE_SPECIES_CREEPER`:** the vine's look, 3.5-7 long, climb 2.5, hug 10 within 0.6,
     clearance -0.03, 180 ticks.
   - **Roots and tufts doubled** at the user's word the same day: roots 0.6-1.8 long, 0.07 thick,
     the step, spacing, wavelength and spread doubled and the turning rates halved to keep the
     shape; tuft plants 1.1-1.8 of the garden's size on a 0.56 disc.
   - 7 checks (870):
     - a creeper struck into a wall 3 tall climbs it, comes over onto the top, and lies against
       a surface at every point (23 of 23);
     - it goes nowhere near the rock, 0.083 at worst;
     - its frame stands out of the wall;
     - struck into a top, it creeps 5.1 along it, low, without resting;
     - 100 seeds: 100 climb, 100 come over, none goes in.

   **8b. Roots on walls, and bamboo** *(DONE 2026-09-30, before step 9 at the user's ask.)*
   - **A normal arrow into a wall grows roots now**, out of the face and drooping down it: a tuft
     of grass standing sideways out of a wall looked wrong (the user). A top still grows a tuft.
     Seen in the game: 11 root strands out of the step's right face.
   - **The bamboo arrow**, `ARROW_BAMBOO`, key 3, a cane-yellow tint and HUD card. It grows:
     - off a top, a clump of canes straight up;
     - out of a wall, canes that come out and bend up, each at its own rate (`gravity` scaled
       0.45-1.35 per cane), so they fan out rather than bending alike into one column;
     - into an underside, roots, then the clump on the top above, by the normal arrow's tuft test.
   - **`VINE_SPECIES_BAMBOO`:** gravity -3 (up), wander 0.25 on a 2.5 wavelength, no branches,
     3-6 long, 0.8 thick, 75 ticks - 16.7 ticks a unit against the vine's 27.
   - **`GrowBamboo`:** 3-5 canes spread across (0.45 on a top, 0.25 up a wall) and 0.35 into the
     surface, the middle ones tallest (down to 0.6 of the length at the ends).
   - **The pieces, all from archer.glb:**
     - `bamboo_stalk` is the tile. It carries 3 node rings: its middle and its ends. It is laid
       like every tile, whole copies stretched to fit. That is not the fixed period this section
       first planned, but every ring still sits where the artist put it in its copy.
     - `bamboo_tip` is a pooled Object (`GROWN_TIP_POOL` 192) riding each cane's front. The cane
       keeps full thickness to the front: `VineParams` gained `grow_tip_length` / `grow_tip_scale`,
       0 and 1 here, so there is no closing point under the tip.
     - `bamboo_leaf` sprays sit at the nodes, from 40% of the length up. They alternate sides, with
       two per node near the top, and are drawn at 1.5x. They are rolled a quarter turn about the
       blade to face the camera; unrolled they stood edge-on and read as wisps.
       `VINE_LEAF_BAMBOO` is a leaf kind past `VINE_LEAF_KIND_COUNT`, so the static vines never
       deal or load it.
     - `bamboo_end`, a flat cut end, is not used yet.
   - **Seen in the game:**
     - off the ground, 5 canes over her head with tips, nodes and sprays;
     - mid-growth, the pointed shoots pushing up;
     - out of the step's face, fanned;
     - under the platform at (15, 6.4), roots below and canes on top.
   - 9 checks (890):
     - a clump of 3-5 canes, each almost straight up;
     - none into the ground;
     - out of a wall, the canes bend up and never go back in;
     - 100 seeds each way all grow, none go in, and no more than 3 are crooked;
     - leaf sprays sit on the upper nodes only;
     - it grows faster than a vine.
   - The archer test replay is unchanged.
   - **Sounds (2026-09-30), the user's own:** `vine_growing`, `bamboo_growing`, `roots_growing`, as
     the cues `vine_grow`, `bamboo_grow` and `roots_grow`. That replaces the rock-crumble
     placeholder.
     - One sound per strike, chosen by what actually grew: bamboo, then vine, then roots.
     - A plant's own roots are covered by its sound; a tuft alone is silent.
     - `roots_grow` is at gain 0.3, since every normal arrow into a wall or an underside plays it.
     - The test recording's three shots now each play `roots_grow`. The baselines were rewritten:
       only the `cues` part of the state moved, from tick 607 on.
   - **The cap:** `bamboo_end` is the cut cap. When it's used is to be decided later (the user).
9. **Withering**, and growths on crumbling blocks.
10. **The other species** as their assets arrive: bamboo (done, step 8b), thorny (the coil
    derive, thorns), grape (fruit, gravity-hung).
11. **Mechanics**: growth into `Stage`, then the vine rope first.

Separately, from step 3 on: fear from the edge list, then the teeter and the catch (section 15).
Those belong to the animation plan when they start, not here.

### Decided 2026-09-29

- Every plant has roots, shown only where they can be seen: out of undersides.
- A normal arrow into an underside grows roots, then a tuft on the top above. Into a top, a small
  tuft where it sticks. Into a wall, roots (changed 2026-09-30: grass sideways out of a wall
  looked wrong).
- A wall grows a creeper, its kind set by the arrow.
- The cap is 32 live growths, the oldest withering with its leaves blown off.
- Platform edges become something the stage lists and looks up (section 15).
- The fall-and-catch works on any edge with a real drop, but only the one she has just gone over
  and only inside the window after leaving it (section 15).

Nothing is open.

## 15. Edges, as something the Stage knows

For the creepers, for the fear of heights, and for the teeter and fall-and-catch animations to
come. Asked 2026-09-29: how are platform edges treated today?

### Today: nowhere, and in three places

Nothing in `Stage` is an edge. Three pieces of code each find them their own way:

- **Fear** (`Stage.cpp`, the vitals on a floor): every tick it steps out 0.1 at a time to
  `VITALS_EDGE_REACH` (0.9) either side of her, calling `DropBelow`, which is a column scan over
  every block. The first column that drops more than `VITALS_DROP_FROM` is the edge.
- **Hanging** (`FindGrabbableLedge`): only the side faces of `BLOCK_LEDGE` blocks, at hand
  height. That is deliberate: a level states where she can hang, and a `SOLID` block of the same
  shape is not catchable.
- **Foliage** (Foliage.h, step 1): the "exposed tops", each block's top minus whatever sits on it.
  Its occlusion score gives a convex corner 0, so plants do not crowd a drop.

Also relevant: the **drawn** lip is not the collider's. Terrain rounds outward by `round_r` (0.2)
and the cap overhangs by `cap_lip_x` (0.10), so turf reaches about 0.3 past the corner she can
actually fall from.

### Proposed: `StageEdge`, derived from the blocks

```
struct StageEdge{
    float x, y;             //the collider's corner: the end of a floor
    int   side;             //+1: floor to the left, drop to the right; -1 the mirror
    int   block;            //whose corner it is
    float drop;             //down to the next floor past it, VITALS_NO_FLOOR if none
    float wall;             //the face below the lip, down to where another block meets it
    float z_front, z_back;
    bool  f_grabbable;      //a BLOCK_LEDGE corner: she may hang here
};
```

- **Built from the exposed-top spans.** Foliage's step-1 rule moves into Stage so both share it.
  Spans of neighbouring blocks at the same height merge, because a floor made of several boxes
  (the main ground run, the bay against it) has no edge where the boxes meet. A span end is an
  **edge** where the floor drops away, and an **inner corner** where a block rises instead. Inner
  corners go in a list of their own: they are where a creeper starts up a wall, and where foliage
  crowds.
- **Rebuilt, not ticked.** It is rebuilt at `BuildLevel`/`Reset`, and wherever a block's `f_alive`
  changes: a broken breakable, a crumbled stone (the two `f_alive = false` sites). It is derived
  from hashed state, so it is never hashed itself. A `make rules` check proves a rebuild after a
  crumble equals a fresh build of the same blocks.
- **Queries**, const and cheap (a level has tens of edges): `NearestEdge(x, floor_y, max_d,
  side)`, `EdgesBetween(x0, x1)`, and `SpanAt(x, y)` for "is this top exposed here" (the tuft).
- Branches, bridges, ramps and pads are not blocks and keep their own ends. A branch already has
  its balance.

### Who uses it

- **Fear** reads the nearest edge on her floor within `VITALS_EDGE_REACH`, with the same formula
  as now, at the exact distance rather than the 0.1 step. The values move slightly, so this is its
  own change, with the trace rewritten. A `make rules` check shows the old and new agree to within
  one step first.
- **The teeter** ("almost falls off"). She is on a floor within about 0.3 of an edge whose drop
  is past `VITALS_DROP_FROM`, slow or stopped, with her centre at or past the lip. Then the teeter
  clip plays (arms wheeling, hips back), fear gets a burst, and pushing on takes her over. Her box
  stays supported until its far side leaves the corner, so "centre past the lip" needs no change
  to the collider. The turf drawn past the corner may want the trigger moved out by about 0.2, or
  she teeters with grass under her toes. Tune that by eye.
- **Fall and catch.** She walks or slides off an edge: `coyote_ticks` already opens a window
  after leaving a floor. Within it, turned back toward the edge she just left, she can catch it
  and go straight into the existing hang (`EnterHang`). This is a *save* rather than traversal, so
  it applies to **every edge with a real drop**, not only `BLOCK_LEDGE` (agreed 2026-09-29). But
  it applies only to **the edge she has just left, inside the window**. That keeps today's "the
  level says where she can hang" rule for everything else, and keeps every wall from becoming a
  ladder. `FindGrabbableLedge` stays as it is, and the catch is a separate test that remembers
  the edge she left.
- **Vines:**
  - A creeper climbs a wall face as far as `wall` says, then turns over the lip.
  - A vine grown from an underside knows where its platform ends.
  - Drapes (section 5) run lip to lip.
  - The normal arrow's tuft checks `SpanAt` above it.
- **Foliage** reads the shared spans instead of its own copy.
- **An edge hint**: a debug view drawing every edge with its drop, as `archer_debug_view` does for
  the rope, and an `archer_edges` MCP read-out. This is how an agent checks the list against a
  screenshot.

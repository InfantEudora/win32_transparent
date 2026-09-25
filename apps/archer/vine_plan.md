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
6. **Gameplay**: rope arrow, cutting, growth - if wanted.

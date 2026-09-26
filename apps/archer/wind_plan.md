# Archer Wind Plan

Wind you can see: leaves and grass first, subtle streaks second, fireflies in the still places -
all driven by ONE wind field that is built from the level, so it always matches it. Talked through
with the user 2026-09-26; the field and its debug view are the first step.

---

## What was decided

| question | answer |
|---|---|
| what carries the look | **leaves and grass**, most of it. Streaks subtle but visible. |
| does wind push her | **no.** It may become an input to the balance mechanic later - not now. |
| leaves | **one model** (the user's), varied in colour and size; **caught in the eddies** is wanted |
| fireflies | emissive flies, plus **a few real point lights as one group** |
| where to start | the field, with a debug view |

---

## 1. Why the side view is the right view for this

The wake behind an obstacle in a wind tunnel happens in the plane of the flow: air comes over the
top, separates at the downwind top edge, and a slow eddy rolls in the lee, turning back toward the
wall along the ground. In a side-view game that plane IS the screen. The swirl is the one thing the
camera shows best, so nothing needs faking in depth - the field is 2D, in the same x/y the rules
already work in.

---

## 2. The field

`apps/archer/Wind.{h,cpp}`, engine-free like `Foliage` and `Terrain`'s field - blocks in, a
velocity at any point out. It is built from `Stage::blocks`, the same derive-from-the-blockout move
the terrain makes, so a moved block moves the wind with it.

### A stream function, not a fluid solver

The velocity is the curl of a scalar ψ (u = ∂ψ/∂y, v = -∂ψ/∂x). Whatever ψ is, the flow has no
sources and no sinks, so leaves never bunch up or thin out for no reason. And a wall is simply a
line ψ is constant along: flow cannot cross a line of constant ψ.

The alternative - a real grid fluid (Stam's stable fluids) - would also run at this size, and gives
genuine emergent swirl. It is not the pick: it smears, it is hard to direct ("an eddy HERE, this
size"), and its state is a history, where the stream function is a pure function of the level and
the tick. That last property is what lets the field move into `Stage` later, when the balance
mechanic wants it, without a replay drifting.

### Four layers

1. **The mean flow** - potential flow over and around the blocks, solved ONCE per level: Laplace's
   equation on a grid, ψ held at 0 on the ground and everything standing on it, at the
   undisturbed value on a floating block, and at the wind's flux on the lid far above. This is the
   part that speeds up over a hilltop and goes still in an inside corner. Solved for a wind of 1
   and scaled, so changing the wind speed costs nothing.
2. **The eddies** - a vortex blob in the lee of every downwind top corner that drops at least
   `eddy_min_drop`. Two per corner, half a cycle apart, each born near the corner, drifting away
   downstream while it swells and fades. Sized by the drop H (a real recirculation bubble runs
   about 2-3 H long and H deep). A pure function of the tick - no state - so the shedding is
   deterministic and restarting a level starts it the same way.
3. **Turbulence** - a few slow travelling sine waves in ψ, drifting with the wind. Analytic, so its
   gradient is exact and cheap.
4. **Gusts** - fronts that travel along x at the wind's speed, multiplying the whole field as they
   pass. Scheduled from the tick by a hash (`PlaceHash`'s idea: a function of WHEN, not a stream).

Layers 2 and 3 are multiplied by a ramp of the distance to the nearest block (Bridson's trick for
curl noise near walls): zero at a surface, 1 beyond `wall_ramp`. That keeps ψ constant on every
surface whatever the eddies do, so nothing they add can push flow into a wall. Gusts multiply the
VELOCITY rather than ψ - a velocity scaled by a scalar is still tangent to a wall, and a gust in
ψ would throw big vertical winds high above the ground.

### What gets stored, and what is evaluated

- At build: the solid mask, the distance to the nearest block (exact box SDF, not a chamfer), its
  gradient, the mean flow's velocity, the separation corners. All on one grid, `cell` apart.
- At any point, any tick: `WindField::Velocity(x, y, tick)` - bilinear lookups for the stored
  parts, the eddies and waves analytically. `const`, so the render thread can call it while the
  physics thread ticks, as long as nobody rebuilds under it.
- For bulk consumers (the foliage texture, later): bake `Velocity` onto a coarse grid once a frame.

### What is an obstacle

`BLOCK_SOLID`, `BLOCK_LEDGE`, and `BLOCK_BREAKABLE` while alive - `f_invisible` ones too, since
they stand for real scenery. **Not** one-way platforms or tree arms: they are thin, and wind
through them reads fine. **Not** the trunk, the spring plants, the props or her. Broken walls
change the field; rebuilding on `broken_blocks` changing is a later refinement.

---

## 3. The debug view

A line mesh, drawn toward the camera over the level:

- **arrows** on a grid, coloured by speed, so the mean flow and the lee eddies are readable in one
  screenshot;
- **streamlines** traced from a column upwind, which is what shows an eddy as a closed loop;
- the **separation corners**, marked.

On a panel checkbox and on `archer_debug_view` (`wind`), plus an `archer_wind` MCP tool for the
parameters and to sample the field at a point. Rebuilt on "Regenerate terrain", since that is when
blocks move.

---

## 4. The visuals that read it

In order of how much they carry, which is the order to build them:

1. **Grass and foliage sway.** Plants already have their origin at the base (the foliage
   convention), so the bend is "sample the wind at the plant, bend by height above the origin". A
   gust front then rolls through the grass like wind through wheat - the most readable wind there
   is, and later the warning a balance mechanic needs. Needs a small core hook: a `wind_flex` in
   the material (it has two padding ints) and one field texture sampled in `default.vert`, in the
   G-buffer pass too so SSAO agrees.
2. **Leaves.** The user's leaf, a few hundred, tinted and sized by a hash. Moved by the CPU through
   the field (with a tumble and a flutter of their own), drawn as ONE mesh holding N copies through
   the custom-shader pass - one object, one draw, no renderer change. Not `ParticleEmitter`: it makes
   every particle a scene `Object` and draws from the shared `RRandom` stream. Lit in their own
   shader, with back-lit translucency. Leaves inside an eddy stay in it for a while, which is the
   wanted "caught in the eddies"; a small chance each cycle lets one escape.
3. **Streaks.** Subtle. A few dozen tracers through the field, each a short fading ribbon of its
   own recent path, additive and thin. In the lee they curl.
4. **Fireflies.** Emissive billboards with a halo (the engine has no bloom) and a real flash
   pattern, wandering slowly, nudged a little by the wind, kept out of solids by the distance
   field, and homed where the foliage is thickest (Foliage's occlusion score). A few real point
   lights as ONE group - the lights follow the centroid of whichever flies are near the camera -
   rather than a light per fly.
5. **Clouds**, the user's models, drifting in the backdrop at the mean wind speed.

---

## 5. Later: wind as a gameplay input

Not now. When the branch and balance arrive (plant_mechanics_plan.md §3), the field moves into
`Stage` as it is - it was built to be able to - and a gust becomes a torque on her lean. Then
gusts would be aimed: launched upstream so that a front arrives while she is on the branch, with
the grass wave showing it coming. The same value can push arrows, and the aim arc would show it for
free since it runs the arrow's own integrator.

---

## Step 1, as built (2026-09-26)

`Wind.{h,cpp}` and `wind_test.cpp` (31 checks, run by `make rules` after stage_test as its own
exe). What the building changed from the design above:

- **Each corner has a BOUND eddy as well as the two shed ones.** Shed blobs alone never reversed
  the flow along the ground in the lee: potential flow runs at about the full wind there (0.99 of
  it behind the test step), because it is symmetric fore and aft and knows nothing of separation.
  A vortex held about a drop downwind and half a drop up does it - reversed on 100% of ticks.
  That held eddy is also what leaves will circle in.
- **The level's end walls are left out.** A tall, thin block at the outermost x is the boundary
  that stops her, not terrain; extruded outward, the main level's 20-high end wall made a cliff
  the whole wind had to climb (a jet of 4.6x the wind over its top) and the range's 48-high pair
  sealed it into a box of still air.
- **An eddy is sized by the exposed downwind FACE when that is shorter than the drop.** Found in
  the debug view: a floating island's "drop" is everything down to the ground, and its eddies hung
  in open air far below it as big loops. Its wake is the height of its own side. Both ends are
  placed off the distance field, not by counting nodes, which read a 1.5 slab as 2.0.
- **An eddy fades when it has no room.** Placed by rule, one can land against or inside another
  block, and the wall ramp then squashes it into a sheet of fast flow along that wall (21 units/s
  in a 2.5 wind). Each is now scaled by the distance at its centre against its radius.

Measured, main level: 24 obstacles, 387x88 nodes at 0.5, 571 SOR iterations, **~145 ms to
build** (the solve is nearly all of it; the range is 5 ms, the rope level 16). `Build` returns at
once when the blocks have not changed, so a restart costs nothing, but a level switch or "Regenerate
terrain" stalls whoever calls it for that long - worth a nested coarse-to-fine solve, or a worker
thread, if it shows. `Velocity()` is ~1.1 us a sample. Fastest wind anywhere: 3.1x the mean, in a
slot under a floating block, where potential flow is genuinely fast. Divergence is 2% of the
velocity gradient; flow into a wall 0.05 off it is 6% of the wind, away from corners.

---

## Step 3, as built (2026-09-26): the plants bend

**Core** (every app, zero cost where unused):

- `material_t::wind_flex` (was the first padding int - no offset moved). EVERY GLSL mirror of the
  material struct had to be renamed with it, even those that never read it: a storage block must
  be declared identically in every stage of a program, and the first run failed the link.
- `Renderer::SetWindField` / `ClearWindField`: the grid travels in an **SSBO at binding 7**, not a
  texture - the texture-unit map is full on the device (11 + 5 = 16). Its header carries the
  grid's mapping and the time, so no shader needs a uniform for it. Created and bound in InitSSBO
  with a zero header (= off).
- `default.vert` bends any vertex whose material has `wind_flex > 0`: push = wind * flex * h^2
  (h above the object's origin), plus a flutter phased by where the plant stands and a smaller
  sway through the slab, the tip lowered to keep the blade its length. Before anything reads the
  position, so the G-buffer and the shadow bend too.
- `UploadMaterials` now binds each TEXTURE once, however many materials use it - which is what
  makes a plant's private copy of an atlas material free.

**Archer:** `UpdateWind` runs every frame, bakes the field over the camera's view (padded 25%,
origin snapped to whole steps so panning does not shimmer) at 0.5 and hands it over.
`WindField::Bake` makes the same numbers as `Velocity()` (checked to 2e-6) for 0.40 ms a frame
in-app (73x47): it works out the eddies and gusts once per frame and takes the waves' sines per
row and column (their phase is separable). Each foliage kind draws with its own material copy
(`<name>@<node>`), because grass_1 shares `terrain_texture` with the tiles and grass_2 shares
`prop_texture_2` with props. Flex per kind on the panel: fern 0.035, low fern 0.20, flower 0.06,
grass 0.30 - the bend goes with height squared, so tall plants need far less.

`grass_2` joined the grass pass (each clump picks one by its own hash channel, `grass_2_share`),
so adding it moved no other plant. `leaf_small` is in archer.glb for step 4 - note it carries an
UNAPPLIED node transform (scale 3, +90 deg about X).

---

## Status

| # | Step | State |
|---|---|---|
| 1 | Field (mean flow, eddies, waves, gusts) + standalone test | **BUILT** 2026-09-26 |
| 2 | Debug view + `archer_wind` tool | **BUILT** 2026-09-26 - panel "Wind", `archer_debug_view` `wind`, `archer_wind` (tuning + `sample`); 0.8 ms a frame in debug while shown, nothing while hidden |
| 3 | Foliage sway (core material hook) | **BUILT** 2026-09-26 - grass_1/2, ferns, flowers |
| 4 | Leaves | waiting on the leaf model; builds on a placeholder |
| 5 | Streaks | |
| 6 | Fireflies + light group | |
| 7 | Clouds | |

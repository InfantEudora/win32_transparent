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

**Vine leaves** sway too, in a second mode: `material_t::wind_mode` (the last padding int, mirrored
as `int wind_mode` in every shader) - 0 STALK bends by height above the origin, 1 LEAF by
DISTANCE from it with more flutter and a flap, because a vine leaf hangs every which way and by
height would hardly move. Each vine leaf kind gets its own `@vine_leaf_N` material copy in LEAF
mode; flex 0.8, on the panel.

**Both modes SWING, they do not shift** (`WindSwing` in default.vert, 2026-09-26, from the user
seeing vine leaves look "off"). The bend is applied AFTER the object's transform, in world space,
and the push comes from the wind - so it pointed the same way whatever the leaf did: a leaf
pointing downwind was pushed along its own length and stretched, one pointing upwind was
squashed, only one hanging across the wind swung. Now the part of the push along the
origin-to-vertex line is dropped and the vertex put back at its old distance - a rotation about
the attachment, whatever the direction. The same fixed fern fronds lying along the wind, and
replaced the stalk mode's "tip comes down" approximation with the exact arc.

---

## Step 4, as built (2026-09-26): leaves on the wind

`Leaves.{h,cpp}` (engine-free, `leaves_test.cpp` 11 checks in `make rules`). A swarm of 140 kept in
the view: velocity pulled toward the wind over `drag_time` 0.30 s, a sink of 0.55 u/s, a falling
leaf's zig-zag, a tumble that grows with how hard the air pushes. Lands on tops where the air is
calm, lifts above 3.4 u/s, skitters in between; recycled after 6 s lying. A leaf that leaves the
view comes back on the OPPOSITE side - whichever carried it out, that is where fresh air enters,
and spawning upwind instead emptied the view whenever she ran downwind of the leaves. Simulated on
the physics thread in RunSimulationTick (so pausing holds them) under wind_mutex; drawn as a pool
of 400 Objects on ONE re-baked mesh, so one instanced draw. 0.12 ms a tick.

What building it changed:

- **The bound eddy is an ellipse, twice as long as tall, 1.2 drops out** (was a circle at 0.9).
  As a circle it ended 1.4 drops downstream, and leaves carried over it came down beyond the
  reverse flow. Reverse flow in the lee doubled (-1.16 against -0.57). `WindEddy::stretch`.
- **A leaf only settles where the air is calm** (< 0.6 x lift). Leaves that settled on touching
  down in the eddy's reverse flow along the ground dropped out of the eddy.
- **A resting leaf feels the wind half a unit up** - the grid's first cell over a surface is
  blended with the zero inside it, and half the leaves lay still in a 6.0 wind.
- **"Caught" is measured as carried back against the wind**: 40 of 60 leaves dropped in the lee
  with eddies, 0 without. Most then settle at the foot of the lee wall, where real leaves pile up
  too; gusts stir them. "Still airborne in a box" was tried first and is no measure - in a calm lee
  without eddies leaves just drift slowly and stay in the box.
- **`leaf_small` has no texture and no colour in the export** (plain white). The four tints are
  therefore material COLOURS (fresh, dark, yellowing, turning), with brightness and a faint
  emissive warmth written too, which is what would carry the variation if it came back textured
  (the lit shader ignores the colour when there is a texture). Metallic capped at 0.1, as for the
  character. The node's unapplied scale 3 / +90 X is applied and the mesh re-centred on its bounds,
  so it tumbles about its middle, not its stem.

**The swarm lives in the view padded 150%** (user, 2026-09-26: zooming out showed the leaves in
a box, and aiming and obstacles will zoom). By DENSITY now (0.2 per square unit, ~1600 leaves at
the default zoom, a sixth on screen; pool 2000), so the padding does not thin them out. A zoom
resizes the swarm without moving anyone, new leaves fade in, and leaves outside the view itself
step every third tick (three ticks at a time) - 1200 leaves cost 0.35 ms a tick. Checked at the
maximum zoom-out (60): leaves across the whole view at once. The PLANT grid stays at 25%: it is
rebuilt from the camera every frame, so zooming never outruns it, and 150% would cost six times
the bake for grass nobody sees.

---

## Step 5, as built (2026-09-26): streaks

`Streaks.{h,cpp}` (engine-free, `streaks_test.cpp` 10 checks in `make rules`). 30 tracers, each
the recent path of a speck of air (midpoint rule - plain Euler spirals out of an eddy), drawn as a
ribbon: thin at both ends, faded at the tail and tip, widened across its path AND the line of
sight so it faces the camera, soft-edged across. A dead streak tries to come back at 2% a tick,
weighted toward gusts (`gust_bias` 0.7), so a gust front arrives as a flurry: 31% of new streaks
under gusts that cover 17% of the air. Alpha follows the local speed against 1.6x the mean, so a
gust draws them brighter as well as more often. A tracer dropped in the lee turns 513 degrees in
4 s; without eddies, 8.

Simulated on the RENDER thread in UpdateWind (catching up the ticks since the last frame - pure
decoration, and the mesh is rebuilt there anyway), drawn through the custom-shader pass with
`assets/shaders/wind_streak.*`: unlit, alpha-blended, no depth write, both faces. The alpha rides
in normal.x and (along, across) in uv. Colour, count, alpha, width, trail length and gust bias
on the panel; `streaks`, `streak_count`, `streak_alpha`, `streak_width` on archer_wind.

---

## Step 6, as built (2026-09-26): fireflies

`Fireflies.{h,cpp}` (engine-free, `fireflies_test.cpp` 11 checks in `make rules`). 40 flies, each
living at a HOME - the foliage plants, handed over by ScatterFoliageObjects, weighted 0.2 + 2 x
the plant's occlusion score, so they gather in the shaded corners (seen in-app: a cluster at the
foot of the cliff). Each steers for a slow Lissajous loop around its home (1-2 units above it),
drifts with 15% of the wind, is pushed out of blocks, and fades away and re-homes when its home
leaves the view (padded 50%).

The flash is a Photinus-like burst: 1-3 quick flashes (70 ms rise, 220 ms glow-down, 0.4 s
apart) every ~5 s, and an ember between. The brightest flash at a moment, not their sum - summed
and capped, a burst of three was one plateau. **Pulse coupling**: a fly starting a burst pulls
the clocks of flies within 3.5 units forward, if they are in the second half of their period, by
a share of what they have left. A cluster's order parameter goes 0.06 -> 0.87 in a minute coupled
(0.20 uncoupled); periods jittered +-0.35 s keep it from ever being perfect.

Drawn like the streaks (render thread, catch-up ticks, custom pass): a camera-facing quad per fly
with a hot core and a DRAWN halo (`assets/shaders/firefly.*`, exp(-3.2 r^2) - the engine has no
bloom, and at exp(-5 r^2) a flash read as a dot). **Three point lights as one group**
(`Fireflies::LightGroup`): one per third of the view, at the glow-weighted centre of its flies,
brightness = gain x sqrt(summed glow) - the sqrt because a point light's brightness is applied
twice in default.frag - smoothed faster up than down so a flash swells. No shadows. Count, sync,
period, glow size, halo and light gain on the panel; `fireflies`, `firefly_count`, `firefly_sync`,
`firefly_light_gain`, `firefly_glow_size` on archer_wind.

**Also fixed that day, in the field:** the forced floor under a bottomless pit was a row forced
solid, not a plane in the distance field, so the distance jumped from ~1.5 to 0 in its last
half-unit and the wall ramp made 10 u/s along the main level's gap floor (6.5x the wind; found
when the default speed went to 0.5 and "no jets" tripped). Now `d = min(d, y - y0)`. And an eddy's
ellipse is shortened toward a circle until the points one long radius either side are clear. The
checks now pin their own wind (`TestWind()`, 2.5) - the default is a tuning choice.

---

## Status

| # | Step | State |
|---|---|---|
| 1 | Field (mean flow, eddies, waves, gusts) + standalone test | **BUILT** 2026-09-26 |
| 2 | Debug view + `archer_wind` tool | **BUILT** 2026-09-26 - panel "Wind", `archer_debug_view` `wind`, `archer_wind` (tuning + `sample`); 0.8 ms a frame in debug while shown, nothing while hidden |
| 3 | Foliage sway (core material hook) | **BUILT** 2026-09-26 - grass_1/2, ferns, flowers |
| 4 | Leaves | **BUILT** 2026-09-26 - plus vine leaves swaying (LEAF mode) |
| 5 | Streaks | **BUILT** 2026-09-26 |
| 6 | Fireflies + light group | **BUILT** 2026-09-26 |
| 7 | Clouds | |

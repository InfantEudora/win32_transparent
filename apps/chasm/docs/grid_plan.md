# Grid Plan

The ground everything in Chasm is built on: a Townscaper-style irregular quad grid, terrain levels
and the cliffs between them, and painting zones onto it. Talked through 2026-10-04; nothing is
built yet (see the Status table at the end). The cross-cutting rules - one level of detail, one
palette, seed plus edits, replay from a saved state - are in `README.md` and are not repeated here.

---

## Decided

- **The Townscaper grid**, not squares or hexes: triangles merged into quads, subdivided, relaxed.
- **Two subdivision levels**, coarse and fine, kept linked: coarse for land use, fine for buildings.
- **Buildings are PAINTED as zones**, never placed as fixed footprints. Capacity comes from area,
  because plots differ in size.
- **Terrain is a few coarse levels**, and a building storey is a separate, much smaller step. A
  cliff wall is generated geometry, not stacked modules.
- **The map is finite, so the grid is generated whole**, once, at world creation. Only drawing is
  chunked.
- **The chasm rim, the shard outlines, rivers and the coast are pinned into the grid** while it
  relaxes, so their edges come out smooth instead of following a grid laid over them.

---

## 1. How the grid is made

The method is Oskar Stålberg's, from Townscaper and Bad North. Every step draws from the world-gen
`RRandom` instance in a fixed order, so the same seed gives the same grid.

1. **A triangle lattice covering the map.** Townscaper starts from a hexagon; a map is closer to a
   rectangle, and the steps that follow do not care what shape the lattice is.
2. **Merge random neighbouring triangles into quads.** Visit triangles in a seeded order; each one
   still unmerged merges with a random unmerged neighbour if it has one. Some triangles are left
   over. That is intended, and it is what makes the grid irregular.
3. **Subdivide everything into quads.** A quad splits into four, a triangle into three, through its
   centroid and edge midpoints (shared between neighbours). The result is all quads, with some
   vertices joined to 3, 5 or 6 edges instead of 4. **This is the coarse level.**
4. **Relax the coarse level.** For each quad, find the square that best fits its four corners about
   its centroid and pull the corners a little toward it; sum the pulls per vertex and apply them
   together. Repeat until quads stop changing much. Pinned vertices (section 3) do not move freely.
5. **Subdivide once more and relax again.** Every coarse quad becomes four fine quads. **This is the
   fine level.**

**The two levels stay linked.** Each fine quad knows its coarse parent, and every coarse vertex is
also a fine vertex. Fine relaxation moves the vertices on coarse edges too, so a coarse quad's shape
is *defined* as the union of its four children, not as its four original corners. No constraint is
needed to hold the coarse edges straight.

**Relaxation runs over the whole map at once.** A map is about 100,000 fine cells (section 6), and
fifty passes over that is well under a second. Townscaper's trick of generating an
endless plane in stitched hex chunks is not needed for a finite map.

---

## 2. Plots and cells: what the player touches, what gets drawn

The grid is used two ways, exactly as Townscaper uses it:

- **A plot is a vertex.** What the player clicks and paints. On screen it is the polygon around the
  vertex, whose edges run through the centres and edge midpoints of the 3 to 6 quads that share it.
  Plots differ in size, which is why capacity is by area.
- **A cell is a quad.** What gets drawn. A cell looks at its four corners - and, for buildings,
  each corner's storeys - and picks its geometry from that pattern, then stretches it to fit its
  actual shape. With four corners below and four above, a storey of a cell has 2^8 patterns, fewer
  after symmetry, which is the marching-cubes idea.

**Stretching is cheap with flat shading.** Geometry is built for a unit square, mapped into the
cell by bilinear interpolation of its corners across the ground and linearly in height, and the
face normals are recomputed afterwards. There are no smooth normals to break and no textures to
stretch.

---

## 3. Pinning features into the grid

The chasm rim, shard outlines, rivers and the coast are drawn as polylines, by hand for now.

**On the triangle lattice, before the merge**, each feature is turned into a chain of lattice
edges: a cheapest path that follows the line, chosen where every vertex is alike and while the
merge along it can still be chosen (see "Step 3, grid side, as built"). **During relaxation** the
chain's vertices slide along the line instead of moving freely, so the chain spaces itself out
evenly and the quads around it relax square against a smooth curve. They stay pinned through fine
relaxation.

**Where the edge actually shows.** Terrain levels belong to vertices (section 4), so a level change
runs through the middle of the cells between a high corner and a low one - half a cell outside the
pinned chain and parallel to it. It is as smooth as the chain is. The cliff generator decides where
in those cells its top edge goes; it is generated geometry, not a module.

**The map outline is pinned too, and is the first user of this**: its four edges are `GridLine`s
its vertices slide along, with the corners fixed (built 2026-10-04, see the end of this file). So
the mechanism exists - `Grid::lines` and `GridLevel::pin` - and the rim adds lines to it.

**What pinning taught, from the outline and then the rim and shard:**

- **Sliding does not fix topology.** A chain vertex wants as many quads on each side as that
  side's angle has right angles - two a side on a straight stretch, one inside and three outside
  a sharp tip. Too many is a 60-degree wedge, too few a quad bent toward flat, and no relaxation
  squares either. So the merge along a chain is chosen, not random: the outline pairs its outer
  ring, a feature pairs its triangles along the chain by dynamic programming.
- **Only the lattice vertices need that care.** A lattice chain edge becomes two coarse chain edges
  meeting at its midpoint, which has two quads a side whatever the merge does, and the same again
  at the fine level. This is why chains are chosen on the lattice.
- **A chain must hug its line, or the cells beside it pay.** Moving the chain onto the line
  squashes the cells on one side. A chain that saves turns by cutting across curves enclosed 17%
  too little of the shard, and the ring outside it was squashed to 0.6 of square. Straying has to
  cost far more than turning, and the move onto the line is spread smoothly into the lattice
  around it.
- **A sharp tip needs a 120-degree turn.** The rim's tip is narrower than two lattice sides; a
  chain allowed only 60-degree turns cut across it and folded the cells it left outside.
- **The lattice limits how small a feature can be.** A chain is up to half a side (4 units) off
  its line. A shard four sides across comes out a hexagon before it is pulled round, and the cells
  just outside it stay sheared (see the as-built notes).
- **Projecting onto a line keeps a segment hint per vertex** - searching every segment is fine for
  four two-point lines, not for a rim of four hundred.

---

## 4. Terrain levels and cliffs

**Each fine vertex has a terrain level**: the plateau, the rim terraces, shard tops and the chasm
floor are a handful of values, not a height field. Buildable land is flat within a level. Gentle
variation inside a level is looks only, and never changes what can be built.

**A level step is not a storey.** A storey is one building floor; a chasm wall is many tens of
them. So:

- **Small steps** (terraces near the rim, a raised plateau) can use cliff pieces chosen per cell by
  their corner pattern, as Bad North does.
- **The deep walls** are made by dropping the cells' level boundary straight down to the lower
  level, then cutting the drop into horizontal rock bands, adding noise to each vertex and taking
  colours from palette bands. A sheer wall is then a few rings of polygons, however deep it is.
  Shards are fine areas at a lower level with their own drop on every side.

Which of the two a step uses is decided by its height, not by its place. Where exactly the line
falls is for step 3 to find out.

**Waterfalls, mist and the chasm floor** come later. Archer's waterfall (`apps/archer/docs/
water_plan.md`) and the raymarched volumes are the starting points for them.

---

## 5. Painting zones

**Two kinds of zone, each on its natural unit:**

- **Land zones** - farmland, pasture, forestry - are painted on **coarse cells**. They cover ground
  and never stack, and a coarse cell makes a good field: big, irregular, and in a patchwork with
  its neighbours, with crop rows running along each field's own axis. A coarse cell takes a land
  zone only if all nine of its fine vertices are on one level.
- **Built zones** - housing, workshops, storage, walls - are painted on **fine plots**, and grow in
  storeys, Townscaper's way. A quarter's wall follows the outline of what was painted.

**Painting a zone does not put down a building.** It marks the plots; what stands there is generated
from the zone's type, its shape, its storeys and its neighbours. A town gaining a storey as it grows
is the natural way for tiers to show, though that is a gameplay decision for later.

**Is one unit enough?** Painting fields on coarse cells and houses on fine plots is the proposal;
step 5 is where it is tested. If the two units feel wrong side by side, everything moves to fine
plots and fields are grouped by their coarse parent for the look only.

---

## 6. Size and drawing

**The size: about 100,000 fine cells**, a map of roughly 3x3 screens at maximum zoom-out (decided
2026-10-04; it changes only if playtime asks for it). Where that comes from: in A Little Age at
maximum zoom-out a house is roughly 15 pixels across, and if a fine cell is about one house, a
1920x1080 screen holds roughly 128x72, about 9,000-11,000 fine cells. That is small for generation;
the number that matters for drawing is the per-screen one, because of the single level of detail.

**Drawing is chunked.** The fine grid is cut into chunks of a few hundred cells. Each chunk's
ground, cliffs and buildings are merged into one mesh and rebuilt on `BackgroundWork` when
something in it changes. A painted plot rebuilds only the chunks its cells touch.

**Forests use the Renderer's instancing.** Every object sharing a mesh is drawn in one instanced
call (`Renderer.cpp`, the `instancedata` SSBO), so a tree is one Object per tree on a few shared
tree meshes, and felling one removes an Object rather than rebuilding a chunk. What stays to be
measured is the per-Object cost of tens of thousands of them on the CPU side.

---

## 7. The order

Every step's checks are commands in the app, debug build only, next to a debug view that draws what
was generated (`README.md`, "Checks live in the app"). There is no separate engine-free build.

| Step | What | Checked by |
|---|---|---|
| 1 | The app and the grid generator together: lattice, merge, subdivide, relax, two levels; a top-down camera; the grid drawn as lines, coarse and fine, with valence and squareness views | seeing it; a `check` command: a pinned hash per seed, every vertex 3-6 edges, no folded quads, a squareness figure, failures highlighted in place; a regenerate command with a seed |
| 2 | Picking plots and cells | the picked plot and its cells highlighted; an MCP tool reporting the pick |
| 3 | Terrain levels and cliffs, with one hand-drawn chasm outline and a shard pinned in | screenshot from above and at an angle; the rim reads as a smooth curve; a terrain view of levels and pinned chains; `check` extended to levels agreeing with the features |
| 4 | Palette texture and lighting | a screenshot that sits next to A Little Age's without looking out of place |
| 5 | Painting zones: placeholder geometry per corner pattern, storeys, fields on coarse cells | painting by hand and over MCP; a zone view; chunk rebuild time |
| 6 | Save and restore, per-tick state hash, replay from a save | the same recording replayed from the same save gives the same trace |

**After step 6:** procedural buildings in place of placeholders, roads and a walker (A* over plots),
forests, biomes and their palette rows, the full chasm map. Each gets its own plan when its turn
comes.

---

## Status

| Step | State |
|---|---|
| 1 App, grid generator, grid view | BUILT 2026-10-04 - see below |
| 2 Picking | BUILT 2026-10-04 - see below |
| 3 Terrain levels and cliffs | BUILT 2026-10-04 - grid side and terrain side, see below |
| 4 Palette and lighting | planned |
| 5 Painting zones | planned |
| 6 Save, restore, replay | planned |

### Step 1, as built

`Grid.h` / `Grid.cpp` generate; `ApplicationChasm` draws, checks and drives. **These figures are
from before the outline fix below**, which changed the grid; the current ones are at its end.
Measured at the default settings (seed 1): **100,384 fine quads** (25,096 coarse) from 11,904 lattice triangles,
1,288 of them left unmerged; 772 x 430 world units. Generation takes 277 ms in debug and 140 ms
in release. Interior valence 3: 2,090, 4: 95,847, 5: 1,630, 6: 186. Squareness mean 0.908,
worst 0.585.

- **Checks:** the panel's *Run checks* or `chasm_check` - deterministic, pinned, edges, valence,
  folded, squareness. Seeds 1-3 are pinned in `Grid.cpp`, and **the hashes are the same in the
  debug and release builds** (checked by running both).
- **Views:** fine and coarse edges, valence stars (blue 3, orange 5, red 6), squareness (each quad
  inset, green to red), the check's issues (ring plus post; click one in the panel, or
  `chasm_view focus_issue`, to fly there). Rebuilt only when something changes: 435k line
  vertices in 47 ms.
- **Tools:** `chasm_grid`, `chasm_generate`, `chasm_view` (camera and layers), `chasm_check`.
  Run on its own port: `--minimized --mcp-port 8769`.
- **Keys:** middle-drag orbits, right-drag pans, wheel zooms, arrows/WASD pan, Q/E turn,
  F frames the map, N regenerates with the next seed.

**Done 2026-10-04: the map outline is straight, and slides.** The lattice's odd rows sat half a
triangle over, so the left and right edges zig-zagged, and with the outline held fixed the cells
against it could not square up - all 20 worst quads were there (seed 1: mean 0.908, worst 0.585).
Two changes, both in `Grid.cpp`:

- **Outline vertices slide along straight edges.** `Grid::lines` holds the map's four edges as
  `GridLine`s, and each vertex's `GridLevel::pin` is free, fixed (the four corners), or the line
  it slides along. Relaxation moves a pinned vertex and puts it back on its line, at both levels;
  subdivision pins a midpoint when its edge runs along a line. This is section 3's mechanism, and
  the chasm rim can use it as it is: polylines, not special-cased edges.
- **The lattice's outer ring is paired, not random.** On a straight edge every outline vertex
  wants exactly two coarse quads. Random merges gave some three or four, wedges of 60 or 45
  degrees that sliding cannot fix, so each outer-ring cell merges its own two triangles. Along
  the top and bottom that is the only pairing that does it.

Seed 1 now: mean 0.911, worst **0.704**, and the worst quads are spread across the interior
instead of lining the edges (seeds 2 and 3: worst 0.705 and 0.692). The map is exactly
768 x 429.5, 100,032 fine quads. A new `outline` check confirms every outline vertex is pinned
and on its line.

The current figures (seed 1, debug): interior valence 3: 1,987, 4: 95,624, 5: 1,593, 6: 197;
generation 257 ms debug, 142 ms release. Pinned hashes for seeds 1-3 re-pinned, the same in both
builds.

### Step 2, as built

`GridPick.h` / `GridPick.cpp`: a `GridPicker` is built with every grid (11 ms) and swapped with
it, immutable like the grid. Fine quads go in a bucket grid about a coarse cell across, so a pick
tests a handful; the plot is found by which quarter of the fine quad holds the point - the quarter
cut by the quad's centre and the two edge midpoints at that corner.

- **Hover:** yellow plot outline, white fine cell, light-blue coarse cell (its bends included).
  Blank whenever the cursor is not over the scene - off the window, over a panel, or unfocused.
- **Select:** left click; drawn as the plot's outline nested toward its vertex, so it reads as
  filled. A click off the map clears it. The click is taken on press and only over the scene, so a
  click on a panel or beside the window does not select what is under it.
- **Panel and tool:** the panel's Pick section shows the hovered and selected plot (sides, area),
  fine cell (area, squareness) and coarse cell (area). `chasm_pick` takes a world x/z or a window
  pixel px/py - the pixel goes through the camera exactly as the mouse does - and `select`.
- **Both are view state, not simulation**: painting (step 5) will be a command naming a plot.
- **Checked:** world and pixel picks agree; four- and six-sided plots and an outline half-plot
  outline correctly; hover confirmed by hand with the mouse.

**Plots differ a lot in size**: a six-sided plot measured 13.2 units against about 4 for a
four-sided one. Counting plots would be unfair; capacity has to come from area (section 5).

### Step 3, grid side, as built

`GridSettings::features` holds the feature polylines in normalised map coordinates (u across, v
from the -z edge to the +z edge); the defaults, `ChasmDefaultFeatures()`, are the rim (open, both
ends on the v = 1 edge) and one shard (closed). Each is smoothed (centripetal Catmull-Rom,
resampled every quarter side), mapped onto the map's rectangle and appended to `Grid::lines` from
`Grid::feature_line_base` - the line the terrain reads, so grid and terrain agree on it.

**How a feature is pinned** (`Grid.cpp`, `PinFeature` and what it calls), on the triangle lattice
after the outer ring is paired and before the random merge:

1. **The chain.** Points every four sides along the line are snapped to lattice vertices, and the
   chain between each two is a Dijkstra path over (vertex, direction, turn state): a step costs
   its length plus 12 x (distance from the line / side)^2, a 60-degree turn 0.25, a 120-degree
   turn 0.5 and only where the line itself bends more than 60 degrees within a side, and a turn
   the same way as the last 0.5 more. A step may not land beside another chain vertex, so two
   chains or two parts of one never share a triangle. An open end on the map's edge takes the
   nearest outline vertex, moved exactly onto the end and fixed.
2. **The merge along it.** Each side of the chain is a strip of triangles; which neighbouring
   pairs merge decides how many quads each chain vertex gets on that side. Dynamic programming
   picks the pairs that come closest to each vertex's ideal - the line's angle on that side over 90
   degrees, from the turn between the midpoints of its two chain edges. The random merge then may
   not cross any edge at a chain vertex.
3. **Onto the line.** Chain vertices are moved onto the line, and that move is spread into the
   lattice around them by 40 passes of neighbour averaging, so the cells beside a chain move with it.
4. **Down the levels.** Chain edges are named edge by edge, so subdivision pins exactly their
   midpoints. A fine edge is on a feature's chain when both its ends are pinned to that line (or
   are its fixed end). Relaxation keeps a segment hint per pinned vertex.

**Measured, seed 1, debug** (seeds 2 and 3 in brackets):

| | quads | mean | worst |
|---|---|---|---|
| Rim: fine quads touching its chain (453 vertices) | 906 | 0.877 (0.880, 0.876) | 0.477 (0.471, 0.487) |
| Shard: the same (60 vertices) | 122 | 0.811 (0.796, 0.794) | 0.498 (0.557, 0.526) |
| Interior: quads with no pinned corner | | 0.910 | 0.583 |
| Whole map | 99,808 | 0.910 | 0.477 |

Topology along the chains is all but exact: of about 110 rim lattice vertices a side, one has a
quad more than its ideal; the shard's 15 have none on one side and two on the other. No quad
folds. Generation 280 ms in debug and 162 ms in release (was 257 and 142). Seeds 1-3 are
re-pinned, the same in both builds. `chasm_check` gained a **features** check (each chain one
piece, unbranched, ends where it should, on its line; ends on the map's edge fixed) and reports
squareness per chain and for the interior.

**Open: small closed features.** The ring of cells just outside the shard is sheared - they meet
the chain at a slant, median height 0.72 of their width against 0.95 everywhere else along a chain.
The cells outside follow a grid turned about 45 degrees to the chain there, and the first row has
to bridge the two. The shard is four lattice sides across, so its chain is a rough hexagon up to
half a side off the line, and the lattice cannot do better. Doubling the relaxation passes and
fixing one chain vertex (no rigid rotation) both left it unchanged. The likely fix is choosing a
small feature's chain on the coarse grid, a quarter the size, rather than the lattice; it was not
needed for this step. The rim, about 90 units across, does not show it; how small a feature can be
before it does has not been measured.

### Step 3, terrain side, as built

`Terrain.h` / `Terrain.cpp` give every fine vertex a level; `TerrainMesh.h` / `TerrainMesh.cpp`
turn the levels into triangles; `ApplicationChasm` holds grid, picker, terrain and mesh data as one
immutable `ChasmWorld`, swapped whole on regeneration, so nothing can pair one grid with another's
terrain.

- **Levels:** plateau 0, shard -24, floor -70. The chasm is the smoothed rim line closed a little
  *past* the south edge (closed along the edge, the vertices on it sat exactly on the polygon and
  half of them came out plateau - a cliff across the chasm's mouth). A vertex pinned to the rim is
  plateau, one pinned to a shard is shard, a fixed vertex is plateau; the rest by inside test.
  Seed 1: plateau 91,569, shard 331, floor 8,541 vertices.
- **Mesh:** per fine cell, marching squares on high corners against low, cut at edge midpoints -
  one high polygon (saddles join their high corners), one ground polygon per run of low corners, and
  a wall down each cut. Walls are cut into 6-unit strata, each row offset by smooth value noise of
  world position, so neighbouring cells' walls meet exactly and each stratum reads as one ledge (an
  independent offset per vertex looked like crumpled foil). A skirt drops the map's outline to -90
  so the map reads as a block. 144 chunks of 48 units, culled by bounds. Seed 1: 215,034
  triangles, 11,856 in walls, 116 ms in debug.
- **Four material slots** (ground, floor, two rocks) - an Object has `NUM_MATERIAL_SLOTS` 4. Stand-ins
  until the palette (step 4).
- **Shadows:** the sun casts them, from the south-west and fairly low so the walls shadow the floor.
  It follows the view as archer's does: the ortho scales with camera distance and the light moves
  in whole texels, so edges do not crawl while panning. Its far plane is set to reach the floor -
  `DirectionalLight`'s default 100 would stop short.
- **Picking on levels:** the ray is tried against each level's plane from the top down; the first
  whose point lands on a plot of that level wins. Pick results and the panel name the level.
- **Views:** grid layers lie on the terrain (an edge between levels drops at its midpoint, where the
  cliff is cut). New layers: *pins* (the smoothed feature lines, a tick on every pinned vertex,
  outline / rim / shard in grey / magenta / cyan, fixed vertices as tall red ticks), *terrain* (the
  mesh on or off, all builds), *flat* (every layer at y = 0, for judging cell shapes near cliffs -
  on the terrain a cliff cell reads as a long spoke from above).
- **Checks added:** `levels` (every level the features call for is present), `steps` (no cell spans
  three levels), `pin levels` (every feature-pinned vertex is on its high side; 511 on seed 1).

**Open:** level assignment tests every vertex against the whole smoothed rim (about 400 points) -
43 ms in debug, up from 3 ms against the hand-drawn corners. A bounding-box test first, or the
pinned chain itself, would bring it back down. Not urgent at one generation per map.

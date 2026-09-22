# Archer Terrain Plan

Replacing the archer's blocked-out cubes with a marching-cubes surface, while the **blockout stays
the collision**. The level keeps being authored as `StageBlock` AABBs in `Stage::BuildLevel`; what
changes is only what the app builds to *look* at them.

The short argument for why this fits: `Stage.h` names no engine type and owns collision,
`ApplicationArcher::BuildBlocks` turns `stage.blocks` into scaled unit boxes, and those are already
two separate halves. Marching cubes replaces the second half and does not touch the first. Nothing
about "adding colliders to a complicated mesh" ever comes up, because no collider is ever generated
from the mesh — it goes the other way round.

Everything measured below was read out of the tree on 2026-09-22 rather than assumed.

---

## 1. The one rule that makes this safe: the field IS the blockout

The density field is **an SDF union of the same `StageBlock` boxes**, not a separately authored
voxel grid. Rounded box per block, smooth-min to union them, a little 3D noise on the result.

This is the whole design, and it is worth being explicit about why it is not just "one way of doing
it":

- **The visual cannot drift from the collision**, because one is a pure function of the other. The
  classic marching-cubes failure — the mesh says you can stand there, the collider disagrees — is
  reduced from an open-ended authoring hazard to a *bounded* mismatch equal to the smoothing radius.
- **There is no second thing to keep in sync.** Move a block in `BuildLevel` and the terrain follows
  on the next rebuild. A hand-authored voxel field would be a second source of truth for level
  layout, and `Stage.h` exists precisely so there is only one.
- **`make rules` keeps testing the real level.** The rules test links no engine and knows only about
  blocks; if blocks stay authoritative it stays meaningful.

### The top must be pinned

> **Corrected 2026-09-22, while building it.** The first version of this section said a smooth-min
> union pulls the surface inside at convex corners and is what sinks a top face. That is wrong, and
> wrong in a way that would send a reader after the wrong term. The two displacements go in
> *opposite* directions and only one of them can ever cause a dip:
>
> | term | direction | where | consequence |
> |---|---|---|---|
> | corner **rounding** (`sdBox(h - r) - r`) | shrinks the solid | within `r` of every edge | the last `r` of a ledge sags **below** the collider — the archer floats |
> | **smooth union** (`smin <= min`) | grows the solid | inside corners, between blocks | ground rises **above** the collider — the archer's boots sink into grass |
>
> Noise is the third term and is the only one that can go either way, which is why it is the one
> that has to be attenuated rather than arranged.

A block whose collider top is `y = 0` rendering its grass at `y = -0.2` is not a cosmetic problem:
judging a jump by eye is the one thing the camera notes in `SetupCamera` say must never be
compromised.

**Requirement:** the iso-surface meets `block.Top()` exactly over the whole collider footprint, and
the surface is never *below* a top face anywhere the archer can stand.

**The mechanism that works**, and it is one line rather than the two-field `max` this section used
to propose — build each block's SDF with **asymmetric half extents**:

```
d_block(p) = sdBox(p - centre, (hw, hh - r, depth)) - r        NOT (hw - r, hh - r, depth - r)
```

Adding `r` back then puts the flat top at `(hh - r) + r == hh` exactly and the bottom at `-hh`
exactly, while x and z grow by `r`. So **the rounding rolls over the edge outside the collider's
footprint** and every point the archer can stand on is at the height the sweep thinks it is. The
cost is `r` of terrain overhanging the lip, which reads as turf hanging over a cliff — the error
you want to have.

Smooth union then cannot pull a top face down (`smin <= min` only ever adds material), and noise is
attenuated to zero at the tops, so nothing left in the field can dip.

**Measured, all four variants, `smooth_k` 0 to 0.6:** `worst_dip = 0.0000`. The fillet `rise` on
open floor peaks at 0.26 on the most aggressive variant and 0.03 on the plan's defaults.

**Acceptance check, in the builder and logged once at build time** (`TerrainStats`, and it is worth
knowing what building it changed about this):

- **Two numbers, not one.** `worst_dip` and `worst_rise` are different phenomena with different
  causes and only one is a bug. A single absolute error would have hidden a dip behind a fillet.
- **Nine samples across each face, not one at the centre.** A single centre probe would have missed
  the edge entirely, and the edge is the whole question.
- **Occluded faces are skipped.** A probe at the floor's centre finds the wall standing on it.
  Without that test the number is meaningless on any level with something stacked on something.
- **The probe starts above the whole bay**, not a fixed distance above the face. Starting at
  `Top() + 0.6` put the probe *inside* a fillet at large `smooth_k`, found no sign change, and
  reported the "no crossing" sentinel as the deepest possible dip — three of four variants failed
  that way while the field was in fact correct.

It cannot go in `stage_test.cpp` — that binary links no engine and must stay that way.

### Only `BLOCK_SOLID` melts

`BuildMaterials` states the app's design rule outright: **colour means a rule**. Ground you stand on,
a ledge you can grab, a platform you drop through and a wall that breaks are four different verbs,
and a prototype's job is to make them readable before anyone touches them.

So `BLOCK_LEDGE`, `BLOCK_PLATFORM` and `BLOCK_BREAKABLE` keep their own distinct, colour-coded
boxes. Terrain is what `BLOCK_SOLID` becomes and nothing else.

That also removes a real problem for free: `ApplicationArcher::BreakBlocks` destroys a block at
runtime. A breakable buried inside a single terrain mesh would force a remesh mid-game. It stays a
box, so it does not.

---

## 2. What the engine already gives us, measured

| thing | where | what it means here |
|---|---|---|
| `NUM_MATERIAL_SLOTS` is **4** | `core/Object.h:37` | grass / soil / rock / spare, per object, no shader work |
| `vertex.matid` read from an SSBO by `gl_VertexID` | `core/Mesh.h`, `default.vert:136` | per-vertex material selection is native |
| `flat out int vmatindex` | `default.vert:83`, `deferred.frag:17` | material is effectively **per-triangle** — see below |
| `vuv` interpolates (location 2) | `default.vert:80` | a free `vec2` per vertex; MC has no natural UV anyway |
| `tangent` only read when a normal map is bound | `default.vert:141` | a second free channel, 3 floats, while nothing is normal-mapped |
| meshes are **non-indexed triangle soups** | `core/Mesh.h` | MC output already has this shape; no index buffer to build |
| `Renderer::CullObjects` does no frustum culling | `core/Renderer.cpp:178` | chunking buys nothing today except rebuild granularity |
| shadow ortho follows the camera, `zoom` 22 | `ApplicationArcher.cpp:966` | one big terrain mesh is a fine shadow caster |
| `Object::Hide()` / `Show()` | `core/Object.h:129` | the debug view in §4 |
| `Application::PreRender` virtual, unused by archer | `core/Application.h:263` | the render-thread hook for a live remesh |

### Materials, concretely

Classify at generation time and write `matid` per vertex:

| slot | material | test |
|---|---|---|
| 0 | grass | `normal.y > 0.70` **and** within `grass_depth` of a block top |
| 1 | soil | everything else |
| 2 | rock | `normal.y < 0.25` (steep faces) |
| 3 | spare | accents; keep free |

Because `vmatindex` is `flat`, the grass/soil boundary is a **hard, jagged, per-triangle line**, not
a gradient. At 0.25-unit cells that reads as a faceted low-poly style and sits well beside the flat
palette this app already uses — it is worth looking at before deciding it is a problem.

If a soft blend is wanted later, it needs no new vertex format: put a grassiness weight in `uv.x`
and (say) vertex AO in `uv.y`, tag the mesh `MESH_MODE_SHADER`, and mix two colours in a ~20-line
fragment shader registered with `Renderer::AddCustomShader`. That is phase two; do not start there.

---

## 3. Geometry: true 3D marching cubes over a thin slab

The play volume is `z ∈ [-1.5, +1.5]` (`BLOCK_DEPTH` is 3.0) and the camera looks straight down -Z
from 26 units at a 38° vertical fov.

**Use true 3D MC, not marching squares plus an extrusion.** What the player mostly sees is the front
face and the top lip; MC rounds that lip *toward the camera*, where an extrusion leaves it a flat
wall. The slab is only a handful of cells deep, so this costs essentially nothing.

**Use a rounded-box SDF that includes the z extent.** The surface then closes itself at the slab
boundary — no capping logic, no open holes at `z = ±1.5`, and the near face gets a gentle bulge that
catches the sun at `(-9, 20, 10)`.

**Use anisotropic cells.** The field barely varies in z, so `cell_z` can be twice `cell_xy` and the
flat front and back faces stop being finely tessellated for no benefit.

Sizing, for the full 84 × 12 × 3 level at `cell_xy` 0.25 / `cell_z` 0.5: roughly 340 × 48 × 6 ≈ 100k
cells, 60–100k vertices, ~5 MB of vertex data. Nothing. The test bay of §4 is a fraction of that.

### Threading

`Mesh::SetMeshData` calls `glNamedBufferData` immediately, which is why `core/Primitives.h` says
render thread only. The same applies here:

- field sampling and the MC itself are pure CPU and may run anywhere;
- **the upload must happen in `Init` or `PreRender`**, never in `RunSimulationTick`, which is the
  physics thread and may not touch GL.

A live parameter tweak therefore sets a dirty flag; `PreRender` sees it and rebuilds.

---

## 4. The test bay

A dedicated area to the **left of the start**, where nothing exists today, holding several visual
variants side by side so they can be compared in one screenshot.

### Why left, and why it is free

- The existing ground run is `x -12 .. 14` and the archer starts at `(-6, 2)`, so everything left of
  `x = -12` is empty world.
- The camera has no clamp — it simply follows the archer — so walking left into the bay just works.
- `Stage.cpp:435` restarts the game at `pos.y < -40`, so the bay must be **contiguous with the
  existing ground** rather than a floating island, or walking into it drops the player out of the
  world.
- **`make rules` costs nothing, provided the bay is built from `BLOCK_SOLID` only.** The reach
  assertion at `stage_test.cpp:115` explicitly `continue`s past `BLOCK_SOLID` and `BLOCK_BREAKABLE`;
  `HighLedge` at `stage_test.cpp:531` takes the *first* `BLOCK_LEDGE` above feet reach, so a stray
  test ledge would hijack it. `blocks.size() >= 8` stays satisfied.

### Layout

Four bays of 7 units each, `x -40 .. -12`, abutting the existing run at `x = -12`. At the default
camera distance the view is about 31.8 units wide, so **all four fit in one `screenshot`** — which is
the entire point of doing it this way.

Appended at the **end** of `BuildLevel`, never prepended: `block_objects` is indexed in step with
`stage.blocks` (see the note at `ApplicationArcher.cpp:1614`), and `stage_test`'s `blocks[0]`
fallback expects the main ground run.

```cpp
//--- The terrain test bay -------------------------------------------------------------------
//Guarded so the whole thing comes out in one edit once the look is settled. SOLID ONLY - a
//LEDGE in here would be picked up by stage_test's HighLedge() instead of the real one.
#if ARCHER_TEST_BAY
    blocks.push_back({ -26.00f, -2.00f, 14.00f, 2.00f, BLOCK_SOLID, true });  //x -40..-12, top 0
    blocks.push_back({ -40.50f,  4.00f,  0.50f, 4.00f, BLOCK_SOLID, true });  //left wall, mirrors x=71

    //The same three shapes in every bay, so the variants differ ONLY in meshing parameters.
    const float bay_centre[4] = { -36.5f, -29.5f, -22.5f, -15.5f };
    for (int i = 0; i < 4; i++){
        float cx = bay_centre[i];
        blocks.push_back({ cx - 2.0f, 0.60f, 1.00f, 0.60f, BLOCK_SOLID, true }); //step, top 1.2
        blocks.push_back({ cx - 0.2f, 1.40f, 0.80f, 1.40f, BLOCK_SOLID, true }); //wall, top 2.8
        blocks.push_back({ cx + 2.2f, 1.00f, 0.25f, 1.00f, BLOCK_SOLID, true }); //pillar, 0.5 wide
    }
#endif
```

### What the three shapes are each for

They are not decoration — each one is the cheapest thing that exposes a specific failure:

| shape | what it tests |
|---|---|
| the **step**, top at 1.2 | a convex lip — does the top-pinning of §1 hold where you land? |
| the **wall** beside it | the concave inside corner — does smooth-min bulge into walkable space? |
| the **pillar**, 0.5 wide | a feature thinner than 2× the smoothing radius — does it survive, or dissolve? |

The seam at `x = -12`, where the terrain stops and the ordinary blockout begins, is *deliberate*: it
puts the old look and the new one edge to edge in the same frame.

### The consequence for the API

"Compare variants simultaneously" means the field function must be **parameterised and pure** — the
same input blocks meshed four different ways in one frame. So it takes a params struct, not
constants:

```cpp
struct TerrainParams{
    float cell_xy     = 0.25f;  //MC grid spacing in the play plane
    float cell_z      = 0.50f;  //coarser; the field barely varies in z
    float round_r     = 0.20f;  //corner rounding on each block's own SDF
    float smooth_k    = 0.35f;  //smooth-min radius of the union
    float noise_amp   = 0.15f;  //world units of displacement
    float noise_freq  = 0.60f;
    float grass_ny    = 0.70f;  //normal.y above this is grass...
    float grass_depth = 0.40f;  //...if within this of a block top
    float rock_ny     = 0.25f;  //normal.y below this is rock
};

Object* BuildTerrainBay(const std::vector<StageBlock>& blocks,
                        float x_min, float x_max, const TerrainParams& p);
```

Bays are selected **by x range**, not by a new field on `StageBlock` — `Stage.h` stays untouched.
One `Object` per bay, named `terrain_bay_0..3`, so `object_list` over MCP names them and each can be
hidden on its own.

### The debug view that falls out of this for free

The melted `BLOCK_SOLID` blocks keep their collider objects; `BuildBlocks` just calls `Hide()` on
them instead of skipping them. That has three benefits and no cost:

1. `block_objects` indices stay in step with `stage.blocks`, which `BreakBlocks` and `NewGame`
   depend on.
2. Collision is completely unchanged — the bodies are still there, still static, still swept by
   `Stage::MoveAndCollide`.
3. `Show()` them again and you are looking at **the blockout underneath the terrain** — which is
   exactly the view needed to judge whether the surface is sitting where the collider says it is.
   Worth a key, an ImGui checkbox, or an MCP tool from day one.

---

## 5. Where the code goes

Split along reusable / not reusable, the way the tree already splits everything:

- **`core/MarchingCubes.{h,cpp}`** — field → mesh, and nothing else. Takes a sampler callback (or a
  pre-sampled grid), grid dimensions and cell size; returns a `Mesh*` the caller owns. Sits beside
  `core/Primitives.h` and inherits its documented conventions: centred, wound CCW seen from outside,
  smooth normals where the surface is smooth, non-zero tangents, caller owns the result, returns
  NULL and logs rather than building a broken mesh. Generic — `bomber` or `tank` can use it later.
- **`apps/archer/Terrain.{h,cpp}`** — `stage.blocks` → density field, and normal → `matid`. Knows
  about `StageBlockKind` and `TerrainParams`. App-specific, never enters core.

`apps/tank/Heightmap.cpp` is the closest existing precedent for the second half and is worth reading
first — same shape of problem, same ownership rules, and the same warning about baking world size
into the vertices rather than fixing it with `SetScale` afterwards (normals do not survive a
post-hoc non-uniform scale).

Add `Terrain.cpp` to `APP_SRCS` in `apps/archer/makefile`. Nothing needs adding to `ASSET_ROOTS` —
the terrain is generated, not loaded.

---

## 6. Build order

Smallest step that answers a real question, at each stage. **Steps 1–5 are built as of
2026-09-22**; step 6 is the open one.

1. ~~**`core/MarchingCubes.cpp`**~~ **DONE.** The mesher, plus `MarchingCubesSelfTest` — which
   turned out to be worth more than the sphere-in-the-app this step originally called for. It
   meshes an analytic sphere and checks four things, the load-bearing one being that **every
   directed edge has exactly one matching reverse**, which is what "closed and consistently wound"
   means and which no wrong row in the 256-entry table can survive. It also caught the sign
   convention: Lorensen's table is written for high-is-inside, so with an SDF the winding has to be
   reversed or every triangle faces into the ground. Result: 3692 triangles, closed, outward,
   worst radius error 0.0015 of a 0.125 cell.
2. ~~**`apps/archer/Terrain.cpp`**, top-pinning~~ **DONE**, and it is what corrected §1. See the
   asymmetric half extents there. `worst_dip = 0.0000` on every variant.
3. ~~**Four bays, four `TerrainParams`**~~ **DONE.** F2 brings the blockout back. One structural
   change fell out of building it: **each bay needs its own floor segment.** A single slab spanning
   all four has its centre in one bay, and selection is by centre, so it would be meshed into every
   bay as four overlapping surfaces — z-fighting, not a comparison.
4. ~~**`matid` classification**~~ **DONE.** Grass / soil / rock reads well; the hard per-triangle
   boundary looks like style rather than an artifact at 0.25 cells.
5. ~~**Noise**~~ **DONE**, and it carries the sharpest lesson in the whole exercise:
   **`NoiseAttenuation` must be continuous in *every* axis, not just the one it is attenuating.**
   The first version skipped a block entirely when outside its footprint, which made attenuation
   jump 0→1 across `x == hw + r`, which made the *field* jump by a whole `noise_amp` there, which
   made the gradient enormous and sideways — and `MarchingCubes` reads its normals off that
   gradient. A field sampled for its gradient has to be continuous everywhere, not just where it
   is convenient.
6. **OPEN.** Whether to apply it to the main level's `BLOCK_SOLID` ground runs, and whether a
   custom shader for a blended grass line is worth it.

### What did not need doing, and why

`NewGame` does **not** remesh, and must not: it runs on the physics thread, and `BuildTerrainMesh`
ends in `glNamedBufferData`. It does not need to either — the terrain is a pure function of
`Stage::blocks` and `Stage::Reset` rebuilds those identically. What a restart *does* need is
re-hiding, because `BuildBlocks` has just made a fresh set of block objects that all start visible.
That is `ApplyBlockoutVisibility`, which touches no GL for exactly this reason.

---

## 7. Verification

- `mingw32-make.exe rules` — must stay green at every step. If it goes red, a non-`SOLID` block got
  into the test bay.
- `mingw32-make.exe -j8` then `./build/archer.exe 2>stderr.log &`, and the `screenshot` MCP tool with
  `include_ui: false` for the clean comparison shot. **One app at a time** — they all bind 8765.
- `sim_pause` before measuring anything; the terrain is static, but the archer standing on it is not.
- The ±0.02 top-pinning assert is logged at build time to stderr — check it rather than trusting the
  picture, because a 0.2-unit float is invisible in a screenshot and obvious under the feet.

---

## 8. Known downsides, accepted

Recorded because they are real, not because they are blocking:

- **Legibility.** The ground stops being a flat colour that says "this is ground". Mitigated by
  melting only `BLOCK_SOLID` — every surface with a verb attached keeps its colour code.
- **Judging a jump.** An organic top edge is slightly harder to read than a hard box corner. The
  top-pinning rule of §1 is what keeps this honest; treat it as a requirement, not polish.
- **Flat slab faces are over-tessellated.** MC emits two triangles per cell across the flat front and
  back of the slab regardless. Anisotropic `cell_z` is the cheap fix; at this scale it does not
  matter enough to do anything cleverer.
- **No frustum culling today** (`Renderer::CullObjects`), so a full-level terrain mesh is always
  submitted. Irrelevant at 100k vertices; worth remembering if the level grows a lot.

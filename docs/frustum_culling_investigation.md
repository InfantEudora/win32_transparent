# Frustum culling: does it save frame time in archer? (2026-09-30)

An investigation with a prototype, not a feature. The prototype is `Renderer::f_frustum_cull`, off by
default, toggled with `renderer_timings {"frustum_cull": true}`. Its counts (`cull`) are reported
every frame, whether it is on or not.

## Verdict

Culling would save about **1-2 ms a frame** in archer, split between GPU and CPU, and change nothing
on screen. It is worth building properly, but it is not the big lever. The frame is GPU-bound at
12-21 ms, and ~70% of that is the colour pass's **per-pixel** work, which culling cannot touch.
The first thing to try there was the 16x MSAA the renderer is set to. **Measured since** (see
*The per-pixel experiments*): 4x halves the frame and looks nearly the same.

## What is in view

Measured from the normal game camera, paused, at four spots. "Objects" is every normal-mesh object
in the colour pass; skinned, line and custom-shader meshes are not counted.

| spot | objects | in view | meshes (= draw calls per pass) | meshes with any instance in view | vertices in view |
|---|---|---|---|---|---|
| start (-6, 0.9) | 4,385 | 462 (10.5%) | 61 | 29 | 0.83 M of 3.11 M (27%) |
| bridges (20.25, 8) | 4,383 | 356 (8.1%) | 61 | 19 | 0.21 M (7%) |
| cave (-50, 0.9) | 4,385 | 256 (5.8%) | 61 | 38 | 0.84 M (27%) |
| roof tree (-49.5, 12) | 4,383 | 254 (5.8%) | 61 | 32 | 0.75 M (24%) |

About 6-10% of the objects are in view, but a quarter of the vertices, because the terrain and
backdrop meshes are few, huge and partly on screen. The renderer is already instanced: one draw
call per unique mesh per pass. So culling does not cut draw calls much. It cuts the instance
data each pass builds and uploads, and the vertex shading of the instances out of view.

## Where the time goes

Release build, `--minimized` (paced to 60 fps), vsync off, paused, medians of 3-4 alternating
off/on runs. All figures are in microseconds.

**GPU**, with the timers made to wait for their results. See *The timers go stale* below for why
that was needed.

| spot | GPU total off -> on | G-buffer off -> on | colour off -> on | shadow |
|---|---|---|---|---|
| start | 14,471 -> 13,555 (-916) | 1,192 -> 602 | 12,062 -> 11,704 | 545 (unchanged) |
| bridges | 21,400 -> 19,668 (-1,733) | 1,168 -> 418 | 19,043 -> 18,066 | 504 |
| cave | 12,275 -> 11,122 (-1,153) | 1,272 -> 735 | 8,768 -> 8,189 | 602 |
| roof tree | 11,954 -> 10,727 (-1,227) | 1,261 -> 663 | 9,370 -> 8,755 | 604 |

The frame rate went from 46-80 fps with culling off to 50-89 with it on.

**CPU**, with the normal non-waiting timers. `renderer_us` is the whole of `DrawFrame` on the CPU.

| spot | renderer_us off -> on | G-buffer CPU | colour CPU | shadow CPU |
|---|---|---|---|---|
| start | 3,823 -> 2,913 (-911) | 906 -> 439 | 668 -> 260 | 602 (unchanged) |
| roof tree | 3,896 -> 3,299 (-598) | 898 -> 561 | 658 -> 377 | 608 |

The CPU side falls by 0.6-0.9 ms: the per-object instance data (a world matrix walked up the parent
chain, the slots, the morph factors) is no longer built for objects out of view in the two camera
passes. The shadow pass keeps building it for all of them, which is right (see below).
`ComputeViewCull` itself costs 220-330 us for ~4,400 objects.

**The colour pass is fragment-bound.** At the bridges, rendering at a quarter of the pixels
(`renderer_scale 2`) took it from 18.6 to 7.9 ms. Fitted as fixed + per-pixel, that is about
14 ms per-pixel and 4.3 ms fixed. Culling saved the same ~0.8 ms at both scales, so it comes out
of the fixed part, as it should. The 16x MSAA, the lights per fragment and the grass's small
triangles are the suspects for the per-pixel part; none was measured here.

**The G-buffer pass is vertex-bound** and halves with culling, which is the cleanest single win.

**Checked against the presented frame rate**, which cannot go stale: re-measured later on a
quiet GPU (nothing else on it, archer alone at 96% load). `fps` off -> on was 68 -> 73 at the
start, 48 -> 51 at the bridges, 80 -> 88 in the cave and 82 -> 90 at the roof tree. That is the
same 1-2 ms the waiting timers gave, so those GPU numbers hold. (The user had an image generator
on the GPU at some point during the session; the agreement says it did not skew these.)

**Why the start costs ~14 ms of GPU now against ~6.1 ms on 09-27** (the `measuring_gpu_cost`
memory note): the growth is in the colour pass, and it is per-pixel. It is not the day's new
content. The cave dressing and the roof tree are 40-60 units off-screen from the start, so they add
only off-screen vertex work, which culling removes, about 1 ms. It is not the lights either: the
fill costs nothing measurable, and the sun costs 1.6 ms (its shadow map 0.5 ms plus the lookup per
fragment), both far short of the growth. The shader commits since 09-27 are the SSAO work (its
passes did not run here), the waterfall's split of `default.frag` into `lighting.glsl`, and a
field-shadow change that is a speed-up. Untested suspects left: the **16x MSAA**, and foliage
overdraw with `alpha_clip` (a `discard` turns off early-z for the whole program - see the
`archer_water` memory note).

**Correctness:** screenshots with culling on and off differ by no more than 8/255 in any pixel
(the view-driven wind still moves while paused), at all four spots. Nothing in view was culled.

## The timers go stale

`BeginGPUPass` keeps two queries per pass and SKIPS a sample when the one it is about to reuse is
not ready. When the GPU runs more than two frames behind, which it does here at 12-21 ms of GPU
against a 16.7 ms paced frame, the late passes (colour, skinned, resolve) stop getting samples, and
their 60-sample averages freeze at old values. The first measurement read a 0.6 ms colour pass
that was really 12 ms; it had frozen at the title screen's frames. `gpu_total_us`, and
`uncapped_fps` built from it, inherit the error. For these numbers the read was made to wait,
temporarily. The lasting fix is a deeper ring (4-6 queries per pass), or to report a pass as
stale rather than repeat its last average. **Fixed the same evening**: a ring of 6 per pass with a
`stale` flag (`Renderer::GPU_QUERY_RING`, `GPU_STALE_FRAMES`); the numbers below use it.

## The per-pixel experiments (2026-09-30, later)

**DECIDED (the user, 2026-10-01):** 4x MSAA is the startup default (`Renderer::Init`), and blending
is off while the colour pass draws the opaque meshes. Both stay switchable live:
`renderer_experiment {"msaa_samples": 16 | 4 | 1, "opaque_blend": true | false}`, or the Renderer
panel's MSAA slider. Everything that genuinely blends keeps it: lines, skinned, the custom pass
(fireflies, waterfall, streaks), the overlay (HUD cards, vignette, fades) and ImGui. The discard
experiment is gone: both discards are wanted, and they measured free.

Checked in one run, paused on the same frame at the four spots and at the waterfall:
- The new defaults against 4x with blending on: every scene pixel identical. The UI shots differ
  only in live readouts (the fps line, the Music panel's timers and its scrolling graphs).
- The new defaults against the old 16x default: edges only, as below.
- `make rules` and `tools/cue_replay.py` (archer_test: `state same`, `ALL SAME`) pass.

What was measured, as three switches at the time:

- **MSAA samples**: `renderer_experiment {"msaa_samples": 16 | 4 | 1}`, live. The buffers are
  rebuilt as the panel's slider does.
- **Blending while the colour pass draws opaque meshes**: `renderer_experiment {"opaque_blend":
  false}`, live. It was on because `SetOpenGLState` enables it for everything.
- **Discards**: a compile-time define, since removed. A discard behind a uniform would still have
  cost the early-z.

**The discard inventory.** Archer's frame has TWO live discards, and neither is alpha clipping:

| shader | program, pass | what for |
|---|---|---|
| `firefly.frag` | the fireflies, custom pass | trims the square sprite to a round glow |
| `archer_water.glsl` (as `archer_water_sheet.frag`) | the waterfall sheets, custom pass | their ragged, moving edges |

The main programs (`default.frag` + `lighting.glsl`, colour and G-buffer passes) contain none.
Their alpha clip is `step()` into the output alpha, and the `discard` in `lighting.glsl` is
commented out. So early-z was already intact where the cost is.

**Results.** Release, `--minimized` (presented fps capped at 60), vsync off, paused, archer alone
on the GPU (nvidia-smi), no pass stale. GPU per frame in ms:

| spot | 16x (today) | 4x | off (1) | blend off | blend off + 4x | discards off |
|---|---|---|---|---|---|---|
| start | 13.7 (colour 11.2) | 8.1 (5.7) | 7.3 (4.8) | 13.7 | 8.2 | 13.7 |
| bridges | 19.7 (17.5) | 9.9 (7.6) | 9.2 (7.0) | 20.0 | 10.0 | 20.0 |
| cave | 12.0 (8.4) | 7.0 (4.2) | 6.7 (3.9) | 11.9 | 7.0 | 12.2 |
| roof tree | 11.7 (8.9) | 7.2 (4.7) | 6.7 (4.3) | 11.6 | 7.1 | 11.6 |

- **MSAA is the cost.** 16x to 4x saves 4.5-9.8 ms, 40-50% of the whole frame. At the bridges the
  frame rate goes from 50 fps to the 60 cap. Turning MSAA off entirely gains only 0.3-0.9 ms more.
- **Blending the opaque meshes costs nothing measurable**, and turning it off changes no pixel.
- **The discards cost nothing measurable.** The custom pass is 27-845 us either way, the 845 being
  the waterfall in the cave's view.

**What it looks like** (screenshots against 16x at each spot, and zoomed crops of the region that
changes most):

- **4x** is hard to tell from 16x at game view. The differences are a slightly harder edge on grass
  blades and crate corners (mean difference 0.4-1.2 of 255, and they sit on edges only).
- **Off** shows plain stair-stepped edges on the grass blades, the crates, the block silhouettes
  and the rock outlines. Visible at game view on thin grass, and it will crawl when the camera moves.

## Design, if it is built for real

- **Bounds.** A local AABB on `Mesh`, set in `SetMeshData`: the prototype's `HasBounds` /
  `GetBoundsMin/Max`. That is the one path every normal mesh takes, rebuilt vines and rope
  included, so it never goes stale. Per object, a world bounding sphere, cached and recomputed
  when the object or a parent moved. Today `GetWorldTransformScaleMatrix` walks the parent chain
  on every call, and each pass calls it for every object, so caching the world matrix itself
  would also save CPU time with no culling at all.
  - **Skinned meshes** have no bounds yet. Use the bind-pose AABB padded by the rig's reach, or
    the skeleton's bone positions each frame. There is one character, so it can stay never-culled.
  - **SplineDeform** (vines, rope, grown plants) rebuild through `SetMeshData`, so they are
    covered. **Morph targets** are never culled in the prototype.
  - **Vertex-shader motion** (wind flex on foliage and leaves) needs padding: the prototype uses
    x1.1 + 0.25.
  - **Terrain** is one mesh per bay, 33-70k triangles, and never out of view in the bay. It would
    only gain from being split into chunks with bounds of their own. That is worth it only if the
    G-buffer's vertex cost matters after the rest.
  - **Instanced or merged meshes** (foliage kinds) are objects each, so they cull per object
    already.
- **Per pass.**
  - Colour and G-buffer: cull against the camera (what the prototype does).
  - Shadow map: cull against the LIGHT's ortho volume, never the camera's, or casters off-screen
    lose their shadows on-screen. The prototype leaves it unculled. It is ~0.55 ms GPU and
    ~0.6 ms CPU, and a light-volume cull would take a share of that too.
  - The occluder field, and any reflection or water pass: their own volumes.
  - Picking reads the G-buffer, so culling it is free.
  - The custom-shader pass (clouds, water): small, left alone.
  - The ImGui Scene panel lists objects, not draws, so it is unaffected.
- **Hierarchy.** A group's union sphere could skip whole subtrees: foliage (2,498), boulders and
  the vine group. With ~4,400 objects a flat linear test is 0.2-0.3 ms, cheap enough. A grid or BVH
  is not needed at this count. Culling whole groups first would take most of that 0.3 ms away,
  if it matters.
- **Cost.** Six plane tests per object per camera: the measured 220-330 us per frame, of which
  the matrix walk is most. With cached world spheres it would be well under 100 us.

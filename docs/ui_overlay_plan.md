# The 2D overlay: rounded rects and centred text, one pass, two platforms

Plan for backlog item 81, written 2026-09-14 after Dick fixed the scope: **filled rounded rects,
outlined rounded rects, and centred text. Nothing else.** This is not a UI framework and must not
grow into one — there are plenty of good ones and none of them is worth writing here.

The second constraint, and the one that shaped most of what follows: **this stage runs on Android
too, and the goal is one pipeline stage that looks identical on both.** Not one engine that looks
identical — the renderer proper will never be that — one *stage*.

Everything below was checked against this tree and against the Android port at `C:/code/android`,
which has a working GLES 3.1 renderer on a real device and therefore has already paid for most of
the portability lessons in here.

---

## 1. Why this stage can be identical on both platforms when the renderer cannot

The port's own shaders are forked — `shared_assets/cube.vert` and `cube_desktop.vert` are two
files, and the desktop one's header says so plainly: "there's no single shared source for both
today." That fork is not laziness. It is the accumulated weight of a list of real, measured device
limits, every one of which is recorded in `C:/code/android/docs/gles31-*.md`.

**The point of this section is that a 2D overlay is hit by none of them.** Going through the list
in the order the port hit them:

| What broke the port's main shaders (Mali-T720) | Why the overlay does not care |
|---|---|
| `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS = 0` — SSBOs link-fail outside compute | §3: vertex attributes only, no SSBO and no UBO |
| std140 mishandles a trailing scalar in a UBO struct | no UBO |
| Dynamic indexing of a sampler array needs `GL_EXT_gpu_shader5` | one atlas texture, indexed by nothing |
| A third integer MRT output is silently dropped | one colour output |
| No `EXT_color_buffer_float`, so float attachments are not renderable | draws to the default framebuffer, after resolve |
| `highp` needed for shadow coords or the scene goes black | no shadow map, no world-space coordinates at all |
| MSAA config differs wildly (the port disabled its colour MSAA pass) | §4: the SDF antialiases itself, so MSAA underneath is irrelevant |

That table is the whole argument for doing this now and doing it shared. A lit, shadowed, deferred
scene has a large surface area against driver variation. A textured quad with alpha blending has
almost none — it is close to the most portable thing this engine could possibly draw.

**What genuinely does differ, and it is two things:**

1. **The `#version` line.** `#version 310 es` and `#version 430 core` cannot both be the first
   line of one file. This is the only truly irreconcilable token, and §6 says what to do about it.
2. **Direct State Access.** `glNamedBufferData`, `glCreateBuffers`, `glVertexArrayAttribFormat`
   and friends are GL 4.5 and **do not exist at any GLES version, including 3.2**. The port shims
   this in `android_core/Mesh.cpp` with an `#if defined(__ANDROID__)` split — a `UploadBufferData`
   helper that binds then uploads, and a second `InitVBOVAO` using `glVertexAttribPointer`. That
   shim is the pattern to copy; it is about twenty lines.

A note on precision qualifiers, because the port concluded otherwise and it is worth settling.
`cube_desktop.vert`'s header says `precision`/`highp` are "ES-only syntax". Desktop GLSL has
accepted precision qualifiers and `precision` statements since GLSL 1.30, parsed and ignored, for
exactly this compatibility reason — so a shared source *should* be able to carry
`precision highp float;` and compile on both. No shader in this tree uses one today, so there is no
local precedent either way. **Verify it in step 2 rather than trusting this paragraph**: add the
line to an existing shader, see if it still links, delete it again. Five minutes, and if it fails
the fallback is to inject the precision block in the same place as the `#version` line.

---

## 2. The asset: bake offline, ship a field

**Offline, not at runtime.** `3rdparty/imgui/imstb_truetype.h` is complete stb_truetype 1.26 with
`stbtt_GetGlyphSDF` at line 4583, so a runtime bake is possible with no new dependency — but what
it would cost is `consola.ttf` (459 KB) plus ~5,000 lines of rasteriser in every shipped binary, to
recompute a result that is byte-identical on every run of every device. With item 82 counting
kilobytes, that settles it. Offline also means Android does no startup work for this at all.

This mirrors the house pattern exactly: `tools/blender_glyph_meshes.py` bakes glyph geometry to a
`.glb` plus a metrics sidecar, and `tools/fontbake.cpp` bakes glyph coverage to an atlas plus
metrics. Same shape, same reasoning.

**Copy `imstb_truetype.h` to `3rdparty/stb_truetype/` first.** Items 24 and 81 both say so and both
are right: a font system that includes a header out of ImGui's folder has not achieved an
ImGui-less build. Here it is cheaper than usual — the tool is offline, so the copy only ever
compiles into `fontbake.exe` and never into an app.

### Grid, not metrics

Monospace collapses the entire glyph table to arithmetic. Bake printable ASCII `0x20..0x7E`
(95 glyphs) into a fixed grid, draw each glyph as its **whole cell** rather than a tight quad, and
there is no per-glyph record at all:

```
index = codepoint - 0x20
cell  = ivec2(index % GRID_W, index / GRID_W)
uv0   = vec2(cell) / vec2(GRID_W, GRID_H)
```

Full-cell quads cost a little overdraw on transparent margins and buy the removal of every bearing,
width and offset field. For a HUD font that is the right side of the trade. A proportional font
would reintroduce the table and change this one helper, which is why nothing else should read it.

A reasonable starting point — not a measurement, adjust once it is on screen: 16x6 cells, 64 px
per cell (1024x384), em around 48 px, leaving ~8 px of padding as distance range. Single channel
R8. `GL_R8`/`GL_RED` are core in GLES 3.0, so the format needs no extension check.

### One binary asset, not PNG + JSON

The sidecar for the mesh glyphs is `fonts_glyphs.json`, and copying that here would be the obvious
move. Recommend against it: a JSON sidecar pulls nlohmann into the *runtime* load path, and item 74
is trying to make that dependency optional. What the runtime needs is about five numbers — grid
dimensions, cell size, em size, advance, line height, distance range — so:

**`shared_assets/fonts/mono_sdf.fnt`: a fixed-layout header struct followed by raw R8 pixels.**
One `LoadFile`, one struct cast, one `UploadTexture`. No JSON parser, no PNG decode, no stb_image.
The tool should *also* write a `.png` next to it purely so the atlas can be eyeballed — that keeps
the inspectability that made the PNG tempting without putting a decoder in the shipped path.

### The solid texel

Reserve one texel in the atlas holding the maximum "inside" value. Untextured geometry points its
UVs at it. This is ImGui's white-pixel trick, and §4 shows it is what removes the last branch from
the shader.

---

## 3. The batch: vertex attributes, built fresh every frame

**Correction to what I suggested when we started: not an SSBO.** The instanced-quad-with-SSBO shape
is idiomatic for this renderer's scene passes and is wrong here, because
`GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS` and `GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS` both read **0** on
the port's test device. Declaring one outside compute fails at link:

```
Program link error: The number of vertex shader storage blocks (1) is
greater than the maximum number allowed (0).
```

This is legal — GLES 3.1 permits those per-stage limits to be zero — so it cannot be assumed away
on any GLES 3.1 device. The UBO fallback the port used works, but `GL_MAX_UNIFORM_BLOCK_SIZE` is
16384 bytes on that device (the spec floor, no headroom), which at 64 bytes per quad caps a batch
at 256 quads and reintroduces the std140 trailing-scalar bug.

So: **ImGui's model.** A dynamic vertex buffer, plain attributes, rebuilt each frame. Universally
available, no size cap, no layout rules, and it is the one path every GL and GLES version agrees
on.

```cpp
//Screen-space, pixels, top-left origin. ~44 bytes; a 300-quad overlay is under 60 KB a frame.
struct ui_vertex{
    vec2     pos;           //where this corner lands
    vec2     uv;            //atlas cell, or the solid texel for untextured
    vec2     local;         //offset from the rect's centre, pixels
    vec2     half_extent;   //the rect's half size, pixels
    float    radius;        //corner radius, pixels
    float    outline;       //0 = filled, >0 = outline half-width
    uint32_t color;         //RGBA8
};
```

**Rebuilt from scratch every frame on the render thread, like ImGui, and this is the load-bearing
choice.** `TextMesh` needs `SetText`/`RebuildIfDirty` because it bakes geometry into a `Mesh` and
`SetMeshData` calls `glNamedBufferData` immediately, which makes it render-thread-only while game
logic runs on the physics thread. An overlay that regenerates its whole vertex list per frame from
a snapshot has no dirty flag, no staleness, and no cross-thread upload to get wrong. It also falls
out for free on Android, where GPU objects do not survive an EGL context loss (§7).

**Its own class, not a fifth `MESH_MODE`.** Every draw in this engine today goes through
`Mesh::Draw` as `glDrawArraysInstanced`; post-processing is compute plus `glBlitFramebuffer`. There
is no fullscreen-quad path and no draw call that is not a `Mesh`. This would be the first, and
keeping it in its own `core/UIOverlay.{h,cpp}` with its own vertex type and its own VAO shim is
what lets it be portable in isolation instead of adding a fourth vertex layout to `Mesh`'s three.

---

## 4. The shader: one distance, no branches

An SDF glyph and an SDF rounded box are the same shader — item 81's central claim, and it holds all
the way down to there being no conditional in the fragment stage.

```glsl
//Box distance is analytic, so rounded corners are EXACT at any size. Only the glyph samples a
//field, so the "SDF rounds off corners" objection applies to text and never to the buttons.
float d_box = RoundBox(local, half_extent, radius);

//The solid texel reads as far inside, so an untextured rect's glyph term never wins.
float d_glyph = (0.5 - texture(atlas, uv).r) * distance_range;

//max() is intersection. A glyph quad is therefore clipped to its own cell for free, and a plain
//rect is a "glyph" that is inside everywhere - one expression, both cases, nothing to branch on.
float d = max(d_box, d_glyph);

//Outline without an if: mix selects, it does not branch.
d = mix(d, abs(d) - outline, step(0.001, outline));

//Distances are in PIXELS, so coverage is a 1px linear ramp and needs no derivatives. No fwidth,
//no dFdx, hence no GL_OES_standard_derivatives question and bit-comparable output on both
//platforms - which is most of what "identical on both" means in practice.
float alpha = clamp(0.5 - d, 0.0, 1.0);
```

Keeping distances in pixels is worth more than it looks. It is what makes the antialiasing
resolution-correct without touching derivative instructions, and it is why this stage does not care
what MSAA configuration is underneath it — it antialiases itself, identically, on a 16x MSAA
desktop buffer and on the port's non-MSAA one.

For the solid texel to work, its stored distance must be more negative than the ~1 px the ramp
spans. Any sane distance range satisfies that; it is recorded here because it is the sort of thing
that silently degrades to a faint halo around every filled rect if the range is ever shrunk.

---

## 5. Where the pass runs

Last. After `CustomShaderPass`, after AA resolve, on the default framebuffer — where ImGui draws
today. Depth test off, depth write off, straight alpha blending.

**One thing to check rather than assume**, given this repo's history: `Renderer::DrawFrame` clears
the whole FBO to transparent black explicitly so ImGui can draw over it, and item 86 is an
unresolved local patch to `ImGui_ImplWin32_EnableAlphaCompositing`. A transparent window makes
straight-vs-premultiplied alpha a real question rather than a pedantic one. Get one opaque rounded
rect on screen over the transparent region early and confirm the edges composite correctly before
building anything on top of it.

**The API speaks millimetres, not pixels.** Item 67's buttons are laid out in mm via
`GetDisplayDPI()` precisely so they survive a change of screen, and a draw path that only spoke
pixels would quietly undo that. Convert mm to pixels at one boundary — the call that appends to the
batch — and keep the vertex buffer in pixels throughout. Text size is a mm quantity for the same
reason.

---

## 6. The `#version` line, and why the port's fork is not the answer here

The port duplicated whole shader files per platform. For its scene shaders, carrying a device
bug list, that was probably right. For two small overlay shaders it would be strictly worse: the
files are almost entirely the logic in §4, and a fork means every change to that logic is a change
in two places with nothing to catch a missed one.

**`core/Shader.cpp` already has the mechanism.** It preprocesses GLSL source for a `#include`
directive, and the comment at line 50 already states the invariant that matters — an included file
must not carry its own `#version`, because that has to stay the first line. So the source stage
already understands that the first line is special and already rewrites the buffer it hands to
`glShaderSource`.

Add to that stage: **the shader file omits `#version` entirely, and the loader prepends the right
one** (plus the precision block if §1's check says it is needed). One source of truth, and it
generalises — the same mechanism would let the scene shaders converge later, which the port would
like and cannot currently do.

---

## 7. The Android trap that will bite the atlas

**GPU objects do not survive the EGL context.** The port's `Mesh::ReUploadMeshData` exists for
exactly this, and its comment says window destroy/recreate on backgrounding does it *even with
orientation locked*. So it is not an edge case; it is what happens when the user takes a call.

The vertex buffer is free — §3 rebuilds it every frame anyway. **The atlas texture is not.** Keep
the loaded `.fnt` pixel bytes in RAM after uploading rather than freeing them, and re-upload on
context recreation. It is a few hundred KB and the alternative is text that vanishes when the app
comes back from the background, which will be blamed on the font system and is not the font
system.

---

## 8. Order of work

1. ~~**`tools/fontbake.cpp`** plus the `3rdparty/stb_truetype/` copy.~~ **Done 2026-09-14 — see
   §10.** Nothing in the engine changed.
2. **`#version` injection in `Shader.cpp`**, and settle the precision-qualifier question (§1).
   Small, and everything after it depends on the answer.
3. **`core/UIOverlay`** and the pass, on Windows. One filled rounded rect. Confirm the alpha
   compositing question in §5 here, with the simplest possible thing on screen.
4. **Text.** Load the `.fnt`, centred, sized in mm.
5. **Port back item 67's touch buttons** and draw them through `UIOverlay` instead of
   `ImGui::GetForegroundDrawList()`. Item 67 notes the buttons hit-test their own rect list in
   `InputController` and never consult ImGui, so input is already decoupled and nothing about this
   step touches it.
6. **Android**: the DSA shim (§1) and the context-loss re-upload (§7).
7. **Item 82 (`USE_IMGUI=0`) becomes reachable.** It is blocked today because
   `Application::DrawTouchButtons` borrows an ImGui draw list; step 5 is what unblocks it.

Steps 1-5 are all on Windows.

---

## 9. What this deliberately does not do

No layout, no wrapping, no hit-testing, no focus, no widgets, no state. Item 24 already warns that
everything above the text primitive is "weeks rather than days", and the way this stays a weekend is
by refusing to start. A `SubmitPointer` rect list plus three draw calls is a game pad, and a game pad
is all Tetris needs.

`TextMesh` stays and is not in competition with this. Extruded glyph geometry is for text that lives
in the world, lights, shadows and can be knocked over; SDF quads are for text stuck to the screen.
They share the pen arithmetic and none of the storage, which is the seam item 24 already identified.

Worth knowing for sequencing: the Android port has **no `TextMesh`**, so its Tetris says everything
through ImGui today. This stage is therefore the only route to real text on Android, which makes it
worth more there than it is here.

---

## 10. Step 1 as built (2026-09-14)

`tools/fontbake/` bakes `shared_assets/fonts/consola.ttf` to
`shared_assets/fonts/mono_sdf.fnt` plus a debug `.png`. Nothing in `core/` or any app changed —
the only new engine file is `core/UIFont.h`, which is header-only, included by nothing yet, and
describes the format for both sides.

**Tools now have the same shape as apps.** `tools/tools.mk` is `engine.mk`'s counterpart: a tool is
a folder under `tools/` with its own makefile setting `ROOT`, `PROJECT` and `TOOL_SRCS`. Tools are
win32-only by decision — assets for Android are made on a PC — so a tool may use anything in
`core/` or `3rdparty/` without a thought for GLES.

One rule in there is worth knowing before writing the second tool: a tool that wants engine code
**names the core sources it wants** (`CORE_SRCS += $(ROOT)/core/File.cpp`) and they compile into
that tool's own `build/`. It must not compile into `$(ROOT)/build/core`, which is shared between
all fourteen apps on the guarantee that everything in it was built with identical flags — a
guarantee make cannot see and cannot warn about. `fontbake` needs none of it and links nothing.

### What the defaults actually produce

| | |
|---|---|
| atlas | 704x408, single channel |
| grid | 16x6 cells of **44x68** |
| em | 48 px, advance 26.391, line height 56.203 |
| pen origin in cell | 8.0, 48.0 |
| field | ±8 px reach, edge at 128/255, range 15.94 px |
| file | 287,316 bytes (84 header + atlas) |

**The cell is not square and is not a round number, and §2's guess at "64 px cells" was wrong in a
way worth keeping.** The cell is *sized from the glyphs* — the union of every glyph's field extent,
rounded up to a multiple of 4 — because that is the only setting that cannot silently clip a
descender. `--cell N` forces a size and is refused if it would clip, naming the size that fits.
The natural cell is much taller than it is wide (44x68) since it must span ascender to descender
plus padding on both, which no square guess would have landed on.

Note **`cell_w` (44) is larger than `advance_px` (26.4)**, so adjacent glyph quads overlap by about
two thirds of a cell when a string is drawn. That is correct rather than a bug: the padding is the
field's reach and has to be rasterised for the edge to antialias at all. The overlap is transparent
in both quads, so the blend is a no-op there — but it does mean the overlay's text draws roughly
1.7x the fill it looks like it should, which is the thing to remember if text ever measures as
expensive.

### Three decisions the build settled

- **`padding` is a shader budget, not a packing detail.** It is the number of pixels the field
  stays meaningful for, so it is the ceiling on every outline and glow width the overlay will ever
  be able to draw. Past it the field clamps and the effect silently stops growing. This is why it
  travels into the header as `distance_range_px` instead of being a local in the tool.
- **A non-monospace font is refused, not warned about.** Arial bakes perfectly well and produces a
  HUD whose error accumulates along a line — far harder to diagnose than a failed bake. Verified:
  `--font arial.ttf` reports 93 of 95 glyphs disagreeing with `M` and exits 1.
- **The solid texel fills a whole spare cell**, not one pixel. A lone texel gets averaged with its
  neighbours the moment a sample lands off-centre, and a rounded rect would come out faintly
  translucent — which would get blamed on the blend state. 95 glyphs in a 16-wide grid leave
  exactly one cell over, and `UIFontHeaderIsValid` refuses a bake with no spare rather than
  overwriting `~`.

### Verified

The tool validates its own header with the same `UIFontHeaderIsValid` the loader will use, which
proves self-consistency and nothing more — so the bake was also read back by an independent script
(scratchpad, not committed). Magic, version, `grid * cell == atlas`, file length, and then the
pixels: the solid texel reads 255, space has no ink, `A M g ! ~ 0` all do, **`A` has no ink below
`origin_y`** and **`g` does**. Those last two are the ones that matter — they pin the sign of the
pen origin, and getting it wrong shifts every string by a line in a way a single-glyph screenshot
cannot show. `consola` at em 48, `CascadiaMono` at em 48 and `consola` at em 24 all pass.

### Left for later, deliberately

The `.fnt` is uncompressed R8 at 287 KB, while its PNG of the same data is 73 KB — so deflating the
payload would cost about four fifths of the file, and `miniz` is already vendored and already
linked into every app. Not done, because the loader does not exist yet and an asset format is
easier to change before something reads it than after. Worth doing if the size ever matters; not
worth doing on speculation.

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
`cube_desktop.vert`'s header says `precision`/`highp` are "ES-only syntax", and forks the file over
it.

**Measured 2026-09-14, and the port is wrong about this.** `precision highp float;`,
`precision mediump int;`, `uniform mediump sampler2D` and `in highp vec3` were all added to a
`#version 430 core` fragment shader in this tree and compiled and linked with **no error and no
warning**. Desktop GLSL has accepted precision qualifiers since GLSL 1.30 — added for exactly this
compatibility, parsed and ignored.

So the ES-looking half of a portable shader can live in the shared source and desktop simply
ignores it. **`#version` really is the only irreconcilable token**, which is what makes §6 a
complete answer rather than a first instalment — and it means the port's shader fork is avoidable
when that merge comes.

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
2. ~~**`#version` injection in `Shader.cpp`**, and settle the precision-qualifier question (§1).~~
   **Done 2026-09-14 — see §11.**
3. ~~**`core/UIOverlay`** and the pass, on Windows. One filled rounded rect.~~ **Done
   2026-09-14 — see §12.** Tested in `apps/tetris` at Dick's suggestion, which is also where the
   pointer work in step 5 has to be validated.
4. ~~**Text.**~~ **Done with step 3** — the shader draws glyphs and rects through one expression,
   so splitting them across two steps would have meant writing the text path to verify the rect
   one. Sized in pixels, not mm; see §12 for why that moved to the caller.
5. ~~**Port back item 67's touch buttons** and draw them through `UIOverlay`.~~ **Done
   2026-09-14 — see §13.** Driven by the Win32 mouse as pointer 0 and verified end to end in
   `apps/tetris`.
6. ~~**Android**: the DSA shim (§1) and the context-loss re-upload (§7).~~ **Done 2026-09-14 — see
   §15.** Running on the mdt740, including a real background/resume cycle. So is the port half:
   `C:/code/android` now carries `UIOverlay`, §11's `#version` injection and the assets.
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

---

## 11. Step 2 as built (2026-09-14)

`core/Shader.cpp` gained two file-static helpers and four lines inside `LoadShaderSource`, which is
already the single funnel for vertex, fragment **and** compute sources. No shader file changed, no
app changed, and no API changed.

**The rule: a shader that states its own `#version` is left byte for byte alone; a shader that
omits one gets the platform's preamble.** Omitting the version is how a shader opts in to being
portable.

That is deliberately not the tidier design. Always stripping whatever version is there and imposing
one would be easier to describe, and it would silently change what all eleven existing shaders
compile as — including the compute ones — on the strength of an assumption that they did not care.
This way the existing tree is provably untouched and nothing had to be audited.

Two details that are not obvious and are load-bearing:

- **The version check skips comments, not just line 1.** A file may reasonably open with a licence
  or an explanation. "Does line 1 start with `#version`" would then inject a *second* version
  directive into a shader that already had one, producing a compile error naming a line the author
  cannot see — because the offending line is not in their file. Both `//` and `/* */` are skipped.
- **`#line 1 0` follows the preamble**, or every error in a portable shader is reported one line
  further down than it really is on desktop and three on GLES. The offset differing per platform is
  what makes this worth getting right rather than living with.

### Verified

Not by inspection — the engine loads shaders from disk at run time, so all of this was tested
against a prebuilt `breakout.exe` by editing the shader between runs.

| | |
|---|---|
| Precision qualifiers are valid desktop GLSL | four ES forms added to a `430 core` shader: compiled and linked, 0 errors, 0 warnings |
| A shader with **no** `#version` compiles | `breakout_shield.frag` with line 1 deleted — and its next content is a `/* */` block, so the comment-skipping path ran too |
| Error line numbers survive the injection | a deliberate syntax error appended as line 227 of 227 reported as **`0(227)`**. Without `#line 1 0` it would have said 228 |
| Existing shaders are unaffected | restored, byte-for-byte per `git status`; all 12 of breakout's shader compiles succeed, 0 errors |

### The Android half — written unexercised here, exercised on the device since

`ShaderVersionPreamble()` carries an `#if defined(__ANDROID__)` arm returning
`#version 310 es` plus `precision highp float;` / `precision highp int;`. Nothing compiles that arm
in this tree, so when this section was written it was a statement of intent.

**It is now a tested path** — ported into the Android tree and run on the mdt740 on 2026-09-14:
`ui_overlay.vert` and `ui_overlay.frag` carry no `#version` of their own and both compiled and
linked there. See §15. `highp` rather than the port's `mediump` default because what uses this first is an overlay
whose entire job is crisp edges at exact pixel positions; a pass that would rather have the tile-GPU
bandwidth can still say `mediump` per variable.

---

## 12. Steps 3 and 4 as built (2026-09-14)

`core/UIOverlay.{h,cpp}` plus `shared_assets/shaders/ui_overlay.{vert,frag}`, drawn from
`Application::DrawFrame` between the scene and the ImGui panels, with a new
`virtual void DrawOverlay()` for apps to fill. Rects and text landed together, because the shader
draws both through one expression and splitting them would have meant writing the text path anyway
in order to verify the rect one.

### The alpha-compositing worry was unfounded, and here is why

§5 said to check this first because this repo's history made it likely to bite. It does not.
`Window::CreateNewLayeredWindow` — the `WS_EX_LAYERED` / `UpdateLayeredWindow` path whose
`AC_SRC_ALPHA` blend would demand **premultiplied** pixels — exists but **nothing calls it**.
`Application::Start` uses `CreateNewWindow`, so `f_is_layered` is false in all fourteen apps and
the window is ordinary and opaque. Straight alpha is correct, matching the global
`GL_SRC_ALPHA`/`GL_ONE_MINUS_SRC_ALPHA`.

Recorded because the risk is real if that path is ever revived: `ui_overlay.frag`'s final line and
the blend func are the two places that would have to change together, and they now say so.

### The real trap was somewhere else entirely

**`Renderer::SetOpenGLState` runs ONCE, from `Renderer::Init` — not per frame.** So every piece of
state the overlay changes stays changed into the next frame's scene pass. Leaving the depth test
off would have flattened the entire game one frame later, which would have read as a renderer bug
with nothing pointing back here. `UIOverlay::Draw` restores depth test, depth mask and culling
explicitly, and the comment there says why rather than what.

Two smaller ones worth recording so they are not rediscovered:

- **`sample` is a reserved word in GLSL 4.00+.** The obvious name for the value fetched from the
  atlas does not compile on desktop. It is `field`.
- **`Add*/Draw` inside a `/* */` block comment ends the comment.** Cost one build. Comes up
  naturally when documenting an API whose methods are being glob-abbreviated.

### Pixels, not millimetres — a deliberate narrowing of §5

§5 said the API should take millimetres so layout survives a change of screen. `UIOverlay` takes
**pixels**, and the mm conversion belongs to the caller. A rasteriser legitimately speaks pixels;
it is *layout* that must speak millimetres, which is the same seam ImGui uses. Doing it this way
avoids inventing a dpi abstraction before item 70 lands a real win32 `GetDisplayDPI`, and item 67's
button layout — the actual consumer — is where the conversion will live.

### Drawn under ImGui

The overlay is the app's UI and ImGui's windows are debug panels on top of it, which is the right
way round while both exist and moot once item 82 drops ImGui from shipping builds. **One exception
to watch:** item 67 records that a touch-button cluster drawn *under* an app's own HUD is live and
invisible, which is worse than being drawn over — so when those move here they may need to be last
rather than first. `Application::DrawOverlay`'s comment carries that warning.

It also means the overlay lands after Renderer's scene-only screenshot capture and before the
UI-inclusive one, so `screenshot include_ui:false` still gives the clean 3D scene. That split
already existed for ImGui; the overlay joins the UI side of it.

### Verified by measurement, not by eye

A screenshot proves something drew. It does not prove the distance field is right. So the test
recomputes the rounded-box SDF on the CPU for each pixel along a diagonal through a rounded corner,
predicts the colour through both quads (fill, then outline, straight alpha over the background),
and compares with what the GPU produced:

```
( 24,544) d= +4.26  got (0, 0, 0)        want (0, 0, 0)
( 26,546) d= +1.44  got (6, 12, 17)      want (6, 12, 17)      <- outline at 6.5% coverage
( 27,547) d= +0.02  got (90, 190, 255)   want (90, 190, 255)   <- dead on the edge
( 28,548) d= -1.39  got (24, 38, 52)     want (24, 38, 52)     <- outline fading into fill
( 29,549) d= -2.81  got (16, 20, 28)     want (16, 20, 28)
```

**Worst channel error across the whole corner: 0.** Byte-exact. That single result covers the
distance field, the pixel-space antialiasing ramp, the outline band, the straight-alpha blend and
the solid texel at once — if any one of them were wrong the partial pixels would not match.

Also checked: the bbox corner of a radius-12 rect is background while the same offset into a
radius-0 rect is fully filled (so the corner is missing because of the radius, not because the
overlay is offset); the outline peak sits within 1 px of the rect edge; text renders at 12, 14, 22
and 56 px **from the one 48 px bake**, which is the claim that justified SDF over a bitmap atlas.

One measurement worth keeping for whoever tests this next: **a perfectly axis-aligned edge at an
integer coordinate shows no antialiasing ramp at all** — pixel centres land exactly on the ends of
the 1 px ramp, giving a hard 0-to-1 step. That is correct, not broken. Test antialiasing on a curve
or a fractional position, or the first measurement will look like a bug.

### Still temporary

`ApplicationTetris::DrawOverlay` is a smoke test — a panel, a radius sweep and two text sizes —
marked as such and to be deleted when the real HUD moves across.

---

## 13. Step 5 as built (2026-09-14)

The on-screen buttons, ported back from the Android port, driven by the Win32 mouse as pointer 0,
and drawn through `UIOverlay`. Verified end to end in `apps/tetris` — a synthetic click on the
LEFT button moves the piece left, and the whole path from `PostMessage` to `piece_x` is exercised
without reaching into the input system anywhere.

**The plan's own test held.** `docs/touch_input_plan.md` said the design would be right if
`ApplicationTetris::SetupInput` gained a handful of lines and nothing else in the app changed.
It did: seven `AddTouchButton` calls plus a `LayoutTouchButtons` override. `GatherInput`, DAS/ARR,
the HUD, the MCP tools and the snapshot are all untouched.

Where the three pieces meet is worth stating because it is the whole point of the shape:
**`InputController` owns the rectangles and emits keycodes, `UIOverlay` owns the pixels, and
neither knows about the other.** `Application::DrawTouchButtons` reads `GetTouchButtons()` and
nothing flows back — `SubmitPointer` hit-tests the same list independently, so a button works
exactly as well when nothing draws it.

### One real defect found, and it is not the one the port warned about

The port's `AddTouchButton` returns `TouchButton*` — a pointer into a `std::vector` that the *next*
`AddTouchButton` invalidates. Adding buttons in a row is the normal usage, so a caller that stored
one would be holding a dangling pointer with nothing to say so. **This version returns an index.**

The bigger one is a layout bug that the plan predicted in the abstract and that turned out to be
immediate rather than hypothetical:

> **The window size is not final during `Init()`.** `ApplicationTetris::Init` calls `SetupInput()`
> and *then* `main_window->Resize(1200,900)`. A layout computed in `SetupInput` is therefore built
> against 1280x800 for a window that becomes 1200x900, and the right-hand cluster lands off the
> edge. Confirmed from the log: buttons at x=1164..1252 in a 1200px window.

So **identity and geometry are now separate**. `AddTouchButton` allocates the synthetic keycode and
its `KeyMap` once, at setup; `SetTouchButtonRect` moves a button afterwards; and a new
`Application::LayoutTouchButtons(w,h)` is called before the first frame and again whenever the
surface changes. That split is not tidiness — `keymap` is walked lock-free by `PollDevices` on the
physics thread, so re-binding on every resize would both race that walk and grow the vector without
bound. Moving four floats does neither.

This also makes an Android orientation change work by construction rather than by luck, and it was
verified live here by accident: a screenshot taken after the window had been resized to 1086px wide
shows the right-hand cluster still correctly inset from the new edge.

### Verified

`scratchpad/touch_test.sh`, all checks passing against a clean build. Every press goes through
`PostMessage` → `WndProc` → `HandleMessage` → `SubmitPointer`, the same path a real click takes.

| | |
|---|---|
| LEFT / RIGHT move the piece | paused + `tetris_step`, so each result is a function of the input and not of timing |
| a press on empty space does nothing | hit-test returns -1 and no key is submitted |
| a held button auto-repeats | DAS/ARR still counts in ticks on the physics thread, unchanged |
| releasing stops the repeat | the capture is released by the same pointer that took it |
| CW / CCW rotate | unpaused — see the limitation below |
| the button lights while held | measured: the held button reads markedly bluer than an idle one |

Hit-testing was confirmed exactly right from a temporary trace before it was removed: `hit=0` for
LEFT, `2` for RIGHT, `4` for CW, `3` for CCW and `-1` for the middle of the board.

### Two traps for whoever tests this next

**The window must have focus.** `SubmitSystemKey` drops key-*downs* while `!f_has_focus` — a click
in a background window has no business driving the game. The pointer still hit-tests and the button
still lights; only the key is gated. Driving this from a script means re-asserting focus before
*every* click, because each `curl`/`python`/`powershell` the harness spawns puts a console in front.
Without that, the log shows flawless hit-testing and nothing happens, which is a confusing half
hour.

**Edge-triggered actions are lost while the simulation is paused**, and this is an engine
limitation rather than anything to do with the buttons. Rotation fires on `WasKeyPressed`, true for
exactly one tick. While paused the physics loop keeps running *non-ticking* passes, each calling
`UpdateInput` → `ApplyPendingEvents`; the press is drained there, raises its edge on a pass that
does not tick, and `NextInput` clears it before any tick can see it. Measured: CW rotates the piece
unpaused and does nothing under `tetris_step`.

**That is backlog item 84's other half.** Item 84 was closed on 2026-09-14 having fixed exactly
this for *synthetic* holds, by advancing them inside the ticking branch — the comment at
`Application.cpp`'s tick loop says so. Real asynchronous events (a touch button, a gamepad button,
any queued key) still fall through. The test asserts the limitation explicitly, so that fixing it
makes the test fail loudly rather than quietly continuing to work around it.

### Still temporary

`ApplicationTetris::DrawOverlay` is still the step-3 smoke test. The buttons themselves are real.

---

## 14. The real HUD (2026-09-14)

The step-3 smoke test is gone and `apps/tetris` now carries the Android build's actual button
layout, so the two are close to copy-over equivalent:

```
   top-right     NEW   II   MUTE        (64 px - chrome, not played with)
   bottom-left    <     v    CCW        (88 px - left thumb)
   bottom-right  DROP  CW    >          (88 px - right thumb, built from the right edge)
```

**Move-left is on the left and move-right is on the right**, one under each thumb, with the
rotations as the inner button of each cluster. That is the port's arrangement and its reasoning
holds: the direction you press should be the side you press, and DAS means a direction is *held*,
so the two are never wanted by the same thumb at once. Each cluster is anchored to its own corner
rather than laid out from one origin, which is what keeps that true at every window size.

### All three chrome buttons work while paused, and that drove where each is handled

A HUD button that only works while the game is running is broken in the one case a player most
needs it — you pause, *then* you reach for "new game" or "mute". So each lands somewhere that runs
on non-ticking passes:

| button | action | handled | why it survives a pause |
|---|---|---|---|
| **II** | `INPUT_PAUSE` | nowhere — the engine's own | `Scene::BeginPass` services it on every pass, before the pause gate it controls |
| **MUTE** | `INPUT_TETRIS_MUTE` (new, also `M`) | `UpdateView` | runs on ticking and non-ticking passes alike, like the existing UI toggle |
| **NEW** | `INPUT_TETRIS_RESTART` | `UpdateView`, as a command | `BeginPass` drains the command queue *before* deciding whether to tick |

**Restart moved out of `RunSimulationTick` to make that true.** It used to call `NewGame()` directly
from inside the tick — which does not run while paused, so pressing it did nothing at exactly the
wrong moment. It now submits `TETRIS_CMD_RESTART`, which is what the ImGui "New game" button already
did, so there is one path rather than two that could drift. The cost is one tick of latency on a
restart, which is nothing.

`tetris_state` gained a `sound` field. That is not decoration: without it the only way to tell
whether a mute press landed is to listen, which no automated check can do.

### Verified

Two suites, each run repeatedly against a clean build: the gameplay cluster (3 consecutive clean
runs) and the chrome cluster (2). Pause toggles both ways, mute toggles both ways **while paused**,
new game resets the game clock **while paused** and does not resume the simulation.

### The harness was flaky, and the fix is worth knowing

The first runs passed and failed alternately, with perfect hit-testing in the log either way.
`SetForegroundWindow` is refused by Windows for a background process under a pile of conditions, so
it worked most of the time and silently failed the rest — and a press that arrives unfocused is
dropped by the focus gate.

The harness now posts **`WM_ACTIVATE`** itself as well as asking for the foreground. That is the
same message `InputController::HandleMessage` listens for to set `f_has_focus`, so it drives the
real code path rather than reaching past it, and it made both suites deterministic. It does not
weaken what is being tested: that the gate works is proven separately, by presses being dropped
when it is not set.

### One cosmetic collision left

The **DROP** button overlaps the `LEVEL` readout, which is world-space `TextMesh` geometry the app
positions itself. The port hit the same class of problem in the same corner and solved it by moving
a button; here it is the readout and the button disagreeing about who owns that space. Not fixed,
because where the score readouts sit is a game-layout decision rather than an overlay one.

---

## 15. Step 6 as built (2026-09-14)

The engine half of Android support. `core/UIOverlay.{h,cpp}` only — no app changed, nothing in the
port changed, and the win32 build is byte-identical in behaviour.

### The DSA shim was already right, and is now measured rather than asserted

§12 wrote the `#if defined(__ANDROID__)` arms and said plainly that nothing in this tree compiles
them. That is no longer true: the file now compiles **for aarch64, with the NDK's own clang against
the real GLES headers**, clean under `-Wall -Wextra`.

Two things were checked rather than believed:

| | |
|---|---|
| The Android arm is what compiled | preprocessed output contains **0** occurrences of `glNamedBufferData`, `glCreateBuffers`, `glVertexArrayAttribFormat`, `glBindTextureUnit`, `glCreateTextures`, `glTextureStorage2D` — and the bind-then-call replacements in their place |
| DSA really is absent from GLES | **all ten** DSA entry points this file would otherwise use appear **0** times across `GLES3/gl3.h`, `gl31.h` **and `gl32.h`** |

That second row is the one worth keeping. §1 asserted "no DSA at any GLES version, including 3.2";
it is now a grep against the shipping headers. The shim is not defensive programming — the desktop
path cannot compile there at all.

**`glad.h` was the one thing genuinely missing**, and it is in the header rather than the body:
Android has no glad, and the GLES entry points come straight out of the NDK's `libGLESv2.so`. The
include is now the same `#if defined(__ANDROID__)` / `<GLES3/gl31.h>` split that
`android_core/Mesh.h` already carries, **spelled the same way on purpose** — the port merge should
be a copy, not a translation.

### §7 asked for something this engine already does

§7 said to keep the loaded `.fnt` pixel bytes in RAM rather than freeing them after upload. It
turns out there was never anything to change: `core/File.h`'s contract is **"THE FILE LAYER OWNS
THE BUFFER. DO NOT free() IT"**, valid for the life of the process, one file one buffer one owner.
The bytes are already retained, by design, for every asset in the engine.

So `UIOverlay` keeps a **borrowed pointer** into that buffer and copies nothing. What was actually
missing was not the retention but the **entry point** — something to call once the context is back.
That is `ReUploadGPUObjects()`.

It also means a re-upload touches no disk, needs no error path for a file that has since moved, and
cannot fail for a reason the first load did not already catch.

### Forgotten, not deleted — and why that is the whole subtlety

The handles are zeroed and **not** passed to `glDeleteBuffers`/`glDeleteTextures`, and the comment
in the code says why at length, because it reads like missing cleanup:

> Those names died with the context that issued them. Deleting them now frees nothing — the objects
> are already gone — and the names are live in the **new** context, where the driver is free to have
> reissued them to somebody else's buffer. The delete is a no-op on a good day and destroys an
> unrelated object on a bad one, from a line that reads like tidying up.

The port's `Mesh::ReUploadMeshData` does the same thing and its comment asks the question out loud
— *"Can we assume the old VBO is dead?"* — so this is an answer worth carrying **back** to the port
when the merge happens, not just forward.

The shader gets the same treatment one level up. `Shader::Build` assigns a fresh `progid` and does
**not** delete the previous one (only `Shader::Reload` does, deliberately), so rebuilding calls
`Build` again on the surviving `Shader` object. No `delete`, no new allocation, no stale
`glDeleteProgram`.

### One code path, not two

`Init` and `ReUploadGPUObjects` both end in a private `CreateGPUObjects()` that builds the shader,
the buffers and the atlas. A separate re-upload path that duplicated those three steps is exactly
the kind of thing that works on the day it is written and silently stops matching `Init` a month
later — and it would drift on the platform where nobody is looking, since win32 never calls it.

`vbo_capacity` is **gone** while here. It was declared, commented as "only grown when it must be",
and never read by anything — `Draw` respecifies the whole buffer every frame regardless. One less
piece of state to reason about across a context loss, and the comment no longer describes a
behaviour the code does not have.

### It runs on the device

Brought up on the port (`C:/code/android`) the same day and **run on the mdt740 — the Mali-T720
whose limits shaped §1 and §3.** The overlay draws there, and this is what the plan has been
aiming at since §1: *one stage, the same on both platforms*, now demonstrated rather than argued.

| what was proven | how |
|---|---|
| The stage draws on GLES 3.1 | translucent rounded panel, cyan outline, a radius sweep ending in a circle, and text at three sizes **from the one 48 px bake** |
| Straight alpha is right there too | the panel composites over the running game — the scene reads through it |
| **§11's Android arm works** | both overlay shaders carry **no `#version`** and compiled and linked on the device. That arm was "written but unexercised" until now |
| **§7 does what it claims** | HOME, then resume. The log shows `APP_CMD_TERM_WINDOW` → `APP_CMD_INIT_WINDOW` → `UIOverlay: GPU objects rebuilt after context loss` |
| The rebuild is CORRECT, not merely survived | the screenshot after the resume is the same overlay — text crisp, panel and outline intact, quad count unchanged — with only the frame counter advanced, 716 to 2153 |

That last row is the one worth keeping. A context-loss path that runs but re-uploads a wrong or
empty atlas looks exactly like one that works until you read the text, and "the font vanished after
a phone call" is the bug §7 was written to prevent.

### What the port needed, as predicted plus one

The gap list this section carried before the bring-up was accurate, and all of it is now done in
`C:/code/android`:

1. **`Shader::Setvec2`** added — the port had `Setint`/`Setfloat`/`Setvec3`/`Setmat3`/`Setmat4`.
2. **`Shader::Build(vert,frag)`** added, and the two-argument constructor now **delegates to it**,
   so the port has one build path rather than a constructor's and a `Build`'s that could drift.
3. **§11's `#version` injection ported**, into all three `glShaderSource` sites (the port has no
   `LoadShaderSource` funnel). Its own shaders all state `#version 310 es`, so they are untouched —
   the same "state your version and nothing happens to you" rule that made this safe here.
4. **The name collision is real and is now documented at both call sites.** The port's existing
   `Renderer::InitUIOverlay`/`DrawUIOverlay`/`ui_overlay_texture` are a hand-drawn PNG of the
   button artwork (`uioverlay.psd` at its root) blitted full-screen. The new stage draws in the
   same slot, immediately after it. **Retiring the PNG is the obvious next step** and was left
   alone deliberately: it is a deletion, and deletions belong in their own change.
5. **The port's `LoadFile` ownership contract held** — the borrowed `atlas_pixels` pointer is valid
   there, which the resume test proves rather than assumes.

Assets needed no new mechanism at all. The port packs `ASSET_DIRS` **recursively**, naming a file
by its path below the root, so `shared_assets/fonts/mono_sdf.fnt` and
`shared_assets/shaders/ui_overlay.{vert,frag}` resolve under exactly the names
`UIOverlay::Init` already defaults to. Worth noting in passing: the packer deflates the atlas from
287,316 to 72,401 bytes — **75% off**, which is §10's "worth doing if the size ever matters",
measured.

Plumbing was one new `Application::BringUpOverlay()` called from both `InitGraphics` bodies
(Android's and Win32's — the two moments a GL context comes into existence), plus a
`virtual void DrawOverlay()` hook and a `Begin`/`DrawOverlay`/`Draw` bracket in the shared
`DrawFrame`. The app therefore cannot forget to upload and cannot draw a stale batch.

### Two pre-existing port breakages found on the way, and both are ours

`apps/tetris` **did not build on the port before any of this**, and neither failure had anything to
do with the overlay. Both come from the same place: the port and this engine share one
`C:/code/reactphysics3d` checkout, which has moved to branch `size/no-iostream-dependency` — the
rp3d side of **item 85**, getting libstdc++'s stream and locale machinery out of every binary.

- `RP3D_VERSION` is a `constexpr const char*` there now, not a `std::string`, so
  `PhysicsWorld.cpp`'s `.c_str()` no longer compiles.
- `DefaultLogger` is behind `IS_RP3D_DEFAULT_LOGGER_ENABLED` (off by default, because it writes
  through `std::ofstream`) while the port's makefile **globs every rp3d source**, so it compiled a
  file full of references to a class that no longer exists — 13 errors in an untouched file.

Both are fixed there (`.c_str()` dropped; `DefaultLogger.cpp` filtered out of the glob, which is
what an Android build wants anyway). Recorded here because the lesson is not about either fix:
**a shared third-party checkout means a change made in this tree lands in the port as a compile
error in a file nobody edited**, and the next one will look just as mysterious.

# Text rendering: the options, and a recommendation

Design note for backlog item 24 (world-space text) and, further out, for a UI layer that does not
depend on ImGui. Written 2026-09-11, from the Tetris audit (`docs/tetris_findings.md` §3.4).

Everything below was checked against the source in this tree, not recalled from elsewhere.

---

## 1. Three problems, not one

"Text rendering" bundles three separable jobs, and most of the confusion in comparing methods comes
from arguing about one while meaning another:

1. **Where do the glyph shapes come from?** A TTF rasterised to a bitmap, a distance field, a
   triangulated outline, a hand-modelled mesh, or a procedural shader.
2. **How does a string get on screen?** Which pass, which mesh layout, how glyphs are selected, how
   the buffer is updated when the text changes.
3. **What sits on top?** Layout, wrapping, hit-testing, focus, input routing — the actual UI
   framework. This is the ImGui replacement and it is much bigger than the other two combined.

**They should be built in that order, and each is useful before the next exists.** A world-space
text primitive pays for itself on day one (labels, floating score numbers, signs, a debug overlay
that survives an ImGui-less build) without any UI framework at all. Do not let "replace ImGui" gate
"draw a word in the world".

A note on scope for §3: when the ship-without-ImGui build flag eventually arrives, what it should
drop is the **debug panels** — `Application::RenderApplicationUI`, the Scene/Inspector/Engine
windows, the menu bar. Those are genuinely development-only. The text primitive is a rendering
feature and belongs in every build.

---

## 2. What the engine already has

This is the part that decides the answer, and most of it was a surprise.

**`3rdparty/imgui/imstb_truetype.h` is stb_truetype 1.26, complete**, and `-I3rdparty/imgui/` is
already on the include path. It provides all three of:

| | |
|---|---|
| `stbtt_BakeFontBitmap`, `stbtt_PackBegin`/`PackFontRange` | rasterise glyphs into an atlas |
| `stbtt_GetGlyphSDF`, `stbtt_GetCodepointSDF` (line 4583) | **generate a signed distance field** |
| `stbtt_GetGlyphShape` (line 851) | glyph outline as quadratic béziers |

So options A, B and E below need **no new dependency at all**. In particular, SDF generation does
not require msdfgen or any offline tool.

ImGui compiles its copy inside `imgui_draw.cpp` with `STBTT_STATIC` and with
`IMGUI_STB_NAMESPACE` left commented out (`imgui_draw.cpp:92`), so those symbols have internal
linkage and are not visible to link against. Including the header in our own `.cpp` with
`STB_TRUETYPE_IMPLEMENTATION` gives us a private copy with no clash.

> **But copy the header to `3rdparty/stb_truetype/` rather than including it out of
> `3rdparty/imgui/`.** The whole point of this exercise is a build that does not contain ImGui. A
> font system that includes a header from ImGui's folder has not achieved that.

**Both bundled fonts are monospace.** `fonts/consola.ttf` and `fonts/CascadiaMono.ttf`. That is
worth more than it sounds — see §4, where it removes the need for a per-glyph UV table entirely.

**`SpriteSheet` only supports equal-size cells** (`SpriteSheet.cpp:41`, "Adding sprite's with
different sizes/widths is super extra not supported"). The limitation that makes it useless as a
general sprite packer makes it exactly right for a monospace font grid.

**Meshes are built from a plain vertex array.** `Mesh::SetMeshData(vertex*, int)` with
`vertex{pos, normal, tangent, uv, matid}` (`core/type_vertex.h`). A string as one mesh is about
twenty lines. Note `matid` selects among the object's four material slots, so one string can carry
up to four colours without splitting the mesh.

**Two passes can host text, and they behave differently:**

- The **colour pass** renders into a **16x MSAA** framebuffer — `Renderer::Init` calls
  `SetNumAASamples(16)` unconditionally, before the deferred branch, and the colour pass binds
  `msaa_fbo_id`. So alpha-*cutout* text gets coverage antialiasing for free, and `alpha_clip`
  already exists as a uniform.
- **`CustomShaderPass` runs last** (`Renderer::DrawFrame`, after the skinned pass), with `GL_BLEND`
  already enabled from `SetOpenGLState` and the deferred G-buffer bound. It is the only stage that
  can do genuinely *blended* text, and it gets depth occlusion for free.
  `shaders/raymarch_volume.frag` is the worked example of a custom-shader material.

**`Mesh::SetMeshData` uploads immediately** — `glNamedBufferData(...)` at `Mesh.cpp:51` — so it is
**render-thread only**, and it uses `GL_STATIC_DRAW`. Both facts matter to the API, see §5.

---

## 3. The options

| | New deps | Effort | Any size | Arbitrary strings | Outline/glow | Fit |
|---|---|---|---|---|---|---|
| **A** Bitmap atlas + quads | none | ~4h | ✗ | ✓ | ✗ | ★★★★ |
| **B** SDF atlas + quads | none | ~6h | ✓ | ✓ | ✓ free | ★★★★★ |
| **C** MSDF atlas | msdfgen | ~1d | ✓ | ✓ | ✓ free | ★★★ |
| **D** Glyph meshes in a `.glb` | none | ~3h | ✓ | ✓ | ✗ | ★★ |
| **E** Triangulated TTF outlines | none | ~1-2d | ✓ | ✓ | hard | ★★ |
| **F** Procedural shader (nixie) | none | ~2h/style | ✓ | ✗ | ✓ free | ★★ as a *material* |
| **G** String baked to its own texture | none | ~3h | ✗ | ✓ | ✓ | ★★★ |
| **H** GPU vector (Loop-Blinn) | none | ~1w | ✓ | ✓ | hard | ★ |

### A — Bitmap atlas

`stbtt_PackBegin` into one 512x512 single-channel texture, `Texture::Create2D` + `UploadTexture`,
one `Material` with `diff_texture` set, alpha cutout in the normal pass so the 16x MSAA does the
edges.

*For:* the boring correct answer, smallest possible amount of new concept.
*Against:* a bake is size-specific. 16px HUD text and a 100px "GAME OVER" want two atlases or one
of them looks wrong. No outline, which matters the moment text sits over a bright, busy scene.

### B — SDF atlas

Identical pipeline to A; each glyph cell stores distance-to-edge rather than coverage, and
`stbtt_GetGlyphSDF` already generates it. The fragment shader is a `smoothstep` around the 0.5
isoline.

*For:* one atlas serves every size. Outline, drop shadow and glow are one extra `smoothstep` each
at a different threshold — which is how HUD text stays readable over a board without needing a
backing panel, and the engine has no other way to get an outline. Composes with the existing
`emissive` glow idea. About two hours more work than A for a great deal more capability.
*Against:* sharp corners round off slightly — a distance field cannot represent a corner exactly.
Invisible in body text, visible on a hard-edged display face at billboard size. Wants the custom
shader pass for real blending (or accept a cutout in the normal pass).

### C — MSDF

Three channels instead of one, which restores the sharp corners. Needs msdfgen as a build-time
tool and an asset pipeline step.

*Against:* solves a problem only visible at sizes we are not rendering. Revisit if B's corners ever
actually bother anyone.

### D — Glyph meshes in a `.glb`

The loader already exists, and text becomes ordinary lit geometry: real shadows, real depth, and it
can be given a rigid body and knocked over.

*For:* genuinely the right answer for *chunky physical* text — a logo, a sign, a "GAME OVER" that
topples into the physics world. Nothing else on this list can do that.
*Against:* you become a typographer in Blender; metrics and kerning are yours to invent; it is
fixed to the glyphs you modelled; and it is much heavier per character.

**D is not exclusive with B.** See §4 — they share all of the string-layout code and differ only in
what geometry each glyph contributes. Build B first, add D later as a second glyph *source*.

#### The glyph set exists (added 2026-09-11)

`data/glyphs_unispace.glb` — 94 meshes, printable ASCII 0x20-0x7E, Unispace Bold, 8264 triangles
in total. So the "become a typographer in Blender" half of the objection above is paid off already,
and the metrics half is generated rather than invented. Three scripts in `tools/` rebuild it:

```
blender --background fonts.blend --python tools/blender_glyph_meshes.py
blender --background fonts.blend --python tools/blender_glyph_export.py  -- --out data/glyphs_unispace.glb
blender --background fonts.blend --python tools/blender_glyph_preview.py -- --out sheet.png --wire
```

The first is the one that matters: it rebuilds each glyph **from the font** into a `Glyphs`
collection in `fonts.blend`, rather than cutting up the pre-converted `text_all` mesh. Splitting
that mesh by loose parts looks fine on `A-Z0-9`, because a filled and extruded capital is one
connected component, and then silently shatters `i j : = % ?` into two or more pieces each.
Re-running is idempotent.

What the layout loop in §4c needs is in `fonts_glyphs.json` next to the .blend, keyed by codepoint.
Unispace is monospace, so per-glyph metrics are mostly a formality:

| | |
|---|---|
| advance | 0.50917, uniform — `pen_x += advance`, no table lookup needed |
| line height | 1.0 |
| ascender / descender | 0.7967 / -0.2025 |
| extrusion depth | 0.1 |

Node names are `glyph_0041_A` and `glyph_002E` — codepoint as four hex digits, plus the character
itself when it is alphanumeric, so `sscanf(name, "glyph_%4x", &cp)` reads either form. Space has an
advance and no mesh.

Two things worth knowing before regenerating:

- **The glyphs are pre-rotated, and have to be.** A Blender text object lies in the XY plane, and
  the exporter's +Y-up conversion is `gltf(x, y, z) = blender(x, z, -y)` — so an unrotated glyph
  arrives with its vertical axis on **-Z**, lying flat like a floor decal. The generator rotates
  each glyph onto Blender +Z first, which lands it upright: x from the pen origin, y up from the
  baseline, z the extrusion depth, and an identity node transform. Bearing stays in the geometry,
  so placing a glyph is `pos.x + pen_x` and nothing else.
- **Decimation is a per-glyph triangle budget, not a flat ratio.** `text_all` carries Decimate at
  0.125, which is right for a curve-tessellated glyph and destructive on a straight-stroked one:
  `A` is only 48 triangles undecimated and `.` is 12, so a flat 0.125 leaves them with 6 and 1.
  `--target-tris 144` (roughly what 0.125 gives a curved glyph) leaves the 42 straight-stroke
  glyphs untouched and evens the density out — median 128 triangles. `--target-tris 0` restores the
  flat `--ratio` behaviour. The modifier is left unapplied on each object, so the ratio stays
  tweakable per glyph in the .blend; the exporter bakes it.

The meshes carry no materials, matching `text_all`. Per §4c, drawing a string still means
concatenating the chosen glyph meshes into one vertex buffer — the `.glb` is a glyph *source*, not
a text system.

### E — Triangulated TTF outlines at load

`stbtt_GetGlyphShape` gives contours; flatten the béziers and triangulate at load.

*For:* true vector text, any size, sharp corners, and the result is meshes, so it feeds the existing
batching and can be extruded into D automatically.
*Against:* you need a triangulator that handles holes (the counter in an 'o') and self-intersection.
That is the part that eats the two days, and B already delivers most of the benefit.

### F — Procedural fragment shader (the nixie tube)

`shaders/shadertoy_nixie_tube.hlsl` — note it is **GLSL** despite the extension (shadertoy is
GLSL), so it needs the `mainImage` to `main` wrap and a `layout(location = 0) out vec4` the way
`raymarch_volume.frag` does it. A working nixie score readout is about two hours from here.

*For:* beautiful, and the glow is free and physically sensible.
*Against:* it is not a text system. It draws *digits*; one shader per style; "GAME OVER" or "NEXT"
is out of reach; every new glyph set is new shader code.

**Recommended footing: keep it as a material for a specific stylised readout** — a nixie score
tube, a seven-segment level counter — sitting alongside a general text system rather than instead
of it. On that footing it is excellent, and it composes with B (an SDF glyph shaded with the nixie
glow is a very good-looking combination).

### G — String baked to its own texture

Rasterise the whole string once into its own texture, draw one quad.

*For:* trivially simple, perfect quality, one draw call, and it handles any layout complexity for
free because the CPU does it.
*Against:* every distinct string is a texture upload, and uploads are render-thread only — so a
score that changes every tick is a GL upload every tick, from the wrong thread. Right for
"NEXT"/"HOLD"/"GAME OVER"; wrong for a live counter.

### H — GPU vector rendering (Loop-Blinn, stencil-and-cover)

Correct and beautiful; a week of work and a lot of shader machinery. Not justified here.

---

## 4. How glyphs are selected — the actual mechanism

This is the question that decides whether "one mesh per string" is clever or painful. There are
three answers, and which one applies depends on how often the text changes.

### 4a. The default: UVs baked into the vertex data

For a quad-based method (A, B, C), **there is no selection mechanism at all** — the selection is
the UV. Each glyph contributes 6 vertices (two triangles) whose `uv` fields point at that glyph's
cell in the atlas. The vertex shader and fragment shader never know what a glyph is; they sample a
texture, exactly as they would for any other model.

```cpp
//Build a string into a vertex array. Local space, origin at the text's baseline start, so the
//Object's own transform does all the positioning.
void BuildTextMesh(const char* text, const FontAtlas& font, std::vector<vertex>& out){
    float pen_x = 0.0f;
    for (const char* c = text; *c; c++){
        const Glyph& g = font.glyphs[(uint8_t)*c];      //metrics + uv0/uv1 in the atlas
        float x0 = pen_x + g.bearing_x;
        float y0 = g.bearing_y - g.height;
        float x1 = x0 + g.width;
        float y1 = y0 + g.height;

        //Two triangles. Normal points at the reader, tangent along +x, so the existing lighting
        //and normal-mapping paths do something sensible without a special case.
        AppendQuad(out,
                   vec3(x0,y0,0), vec3(x1,y1,0),         //corners
                   g.uv0, g.uv1,                         //THIS is the glyph selection
                   /*matid*/ 0);
        pen_x += g.advance;
    }
}
```

Then one `Object`, one `Mesh`, one `SetMeshData`, one material pointing at the atlas. **One draw
call for the whole string**, and it batches with every other string sharing that mesh... no, it
does not batch across strings, because each string is its own mesh — but one draw call per string
is already far better than the alternative.

**Why not one Object per glyph.** Per-instance data here is `instancedata_t`
(`mat_transformscale`, `material_slot[4]`, `morph_factors[4]`, ...) and an instance *is* an
`Object` in the scene tree. Instancing per glyph would make a six-character score six scene nodes,
six entries in `renderable_objects`, six rows in the Scene panel, and six transforms to cull. For a
HUD with four readouts that is ~40 objects doing the work of 4. Don't.

**Why monospace makes this easier still.** With a fixed cell grid the whole `Glyph` table collapses
to arithmetic — `uv0 = vec2((c % 16) / 16.0, (c / 16) / 16.0)`, advance is a constant — so the
atlas needs no metrics table at all for a first version. Proportional fonts just fill in the table.

### 4b. When the string changes every tick: index the glyph in the shader

4a rebuilds and re-uploads the mesh whenever the text changes. `Mesh::SetMeshData` calls
`glNamedBufferData` immediately (`Mesh.cpp:51`) with `GL_STATIC_DRAW`, so that is a render-thread
call with the wrong usage hint, every tick, for a score counter.

The fix, if it ever measures as a problem: build the mesh **once** as N blank character cells with
UVs in `[0,1]` per quad, and put the *glyph indices* in a buffer the vertex shader reads.

```glsl
//Vertex shader. The mesh is a static row of N unit quads; which glyph each one shows comes from
//the SSBO, so changing the text writes N bytes instead of re-uploading a mesh.
int slot  = gl_VertexID / 6;                 //which character cell this vertex belongs to
int glyph = text_glyphs[text_base + slot];   //SSBO, one int per character
vec2 cell = vec2(glyph % 16, glyph / 16) / 16.0;
uv_out = cell + uv_in / 16.0;                //uv_in is the quad's own 0..1
```

This is idiomatic for this renderer, which already drives everything through SSBOs
(`instdata_ssbo`, `materialdata_ssbo`, `lights_ssbo`, `boneinstdata_ssbo` — adding a `text_ssbo` is
the same pattern). It needs a fixed cell grid, which is exactly what a monospace font gives.

**Do not build this first.** 4a is simpler, has no new GPU-side concept, and a handful of short
strings rebuilt on change costs nothing measurable. 4b is the answer to a profiler, not to a
prediction.

### 4c. For glyph meshes (option D): merge at build time

A `.glb` glyph is a mesh, and a mesh is not selectable per-vertex. The options are one Object per
glyph (rejected above), multi-draw indirect (which this renderer has no path for), or:

**concatenate the chosen glyph meshes into one vertex buffer, transformed into place as you go.**
Which is the same loop as 4a, with `AppendQuad` replaced by `AppendGlyphMesh` — copy the glyph's
vertices, add `pen_x` to each position, push.

That is the useful structural point: **A, B, C and D all share the same string-layout loop and the
same "one mesh per string" output.** They differ only in what each glyph contributes to the vertex
array — two textured triangles, or forty lit ones. So the layout code, the `TextMesh` object, the
dirty-flag plumbing and the API are written once and serve both. Start with B; adding D later is a
new `GlyphSource`, not a new text system.

---

## 5. Proposed shape, and the one constraint that shapes it

**`Mesh::SetMeshData` is render-thread only** (it calls `glNamedBufferData` directly). Game logic
runs on the physics thread. So the API must not rebuild on assignment:

```cpp
class TextMesh : public Object{
public:
    //Callable from ANY thread, including RunSimulationTick. Stores the string and raises a dirty
    //flag; nothing touches GL. Cheap enough to call every tick with an unchanged string.
    void SetText(const char* text);

    //Render thread, from Application::PreRender. Rebuilds and re-uploads only if dirty.
    void RebuildIfDirty(const FontAtlas& font);
};
```

`Application::PreRender()` already exists for exactly this class of problem ("GL work that has to
happen before the colour pass and cannot be hung off a shader's uniform_callback... UpdateView is
NOT the place, close as it sounds: it runs on the physics thread, which may not touch GL"). A `Scene::RebuildDirtyTextMeshes()`
called from there closes the loop.

Two small core changes fall out and should be logged when they happen:

1. `Mesh::SetMeshData` hardcodes `GL_STATIC_DRAW`. Text wants `GL_DYNAMIC_DRAW` — the line mesh
   path already uses it (`Mesh.cpp:63`), so it is a parameter, not a new concept.
2. Nothing owns "a texture that is not a material texture". The font atlas is a `Texture` that
   wants to live somewhere sensible; `AssetManager` is the natural home.

### Recommendation

**Option B, drawn as one mesh per string, in the custom shader pass, with the glyph selected by
baked UVs (4a).**

One 512x512 single-channel SDF atlas from `fonts/consola.ttf` covers every size including a
full-screen "GAME OVER"; it needs no new dependency; outline, shadow and glow — the things that
make HUD text legible over a scene — come out of the same shader for a few extra lines. It reuses
`Texture`, `Material`, `Mesh::SetMeshData` and `AddCustomShader` without inventing a pass.

Then keep **F (the nixie shader) as a material** for one stylised readout, and add **D** later as a
second glyph source when something wants physical letters, sharing B's layout code.

---

## 6. What was actually built (2026-09-11)

**D, not B** — because between writing this note and building anything, Dick exported one mesh per
glyph to `data/glyphs_unispace.glb` (`tools/blender_glyph_meshes.py`, Unispace Bold, 94 meshes,
8,264 triangles). With the glyph source already in the tree, D stopped being the expensive option.

`core/TextMesh.h` bakes a string into a single `Mesh`; `APP=Tetris` uses it for its captions,
stats and game-over banner. Three things the exercise settled that this note had left open:

- **§4's glyph-selection question does not arise for D at all.** There is no atlas and no UV
  lookup: the glyph *is* the mesh, selected by an array index (`codepoint - 0x20`), and layout is
  the pen arithmetic §4 describes minus the texture bookkeeping. The selection problem is
  specific to the atlas options.
- **What a font reduces to here is two floats.** `advance` 0.509167 and `line_height` 1.0, from
  the JSON sidecar the export script writes, because glTF has nowhere to put metrics. Everything
  else this note calls “atlas” was storage, not layout.
- **The prediction in §5 held.** Depth is real — glyphs are 0.1-unit extrusions that light,
  shadow and occlude like any other geometry, visibly so where the banner casts onto the back
  panel — and the per-string vertex count is trivial: a 10-character label is about 900 vertices.

B is still the recommendation for the *general* case and is still open (backlog item 24). It now
has less to build than this note assumed: `BuildTextMesh`'s layout, alignment and string-relative
UVs are source-agnostic, so an SDF path is a second loader and a second builder beside the first,
not a replacement for it.

---

**For the Tetris app alone, A would have been enough** and roughly half the work: one small size for
SCORE/LEVEL/LINES, two static labels, one big GAME OVER, and Consolas being monospace means layout
is `x += advance` with no kerning. B earns its extra hours at the *engine* level, not in that app —
it is the choice that does not need revisiting when the next app wants text at a different size.

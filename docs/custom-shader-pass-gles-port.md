# Porting the custom-shader pass to GLES 3.1

What it takes to draw `MESH_MODE_SHADER` geometry — the bomber's raymarched explosion, the ship's
clouds, the water — on a GLES 3.1 device, what is already impossible there, and what is merely
work. Written against the Vehco MDT740 (Mali‑T720, `OpenGL ES 3.1 v1.r12p1`, GLSL ES 3.10), which
is the floor this port targets.

**Read this from the Win32 side too.** Two of the findings below are not "Android is limited",
they are **latent portability bugs in the engine's own files** that happen to be invisible on a
desktop GL 4.5 driver. Fixing them upstream costs nothing there and removes the blocker here.

---

## 0. Where this stands

**Surveyed 2026-09-18, when there was no custom-shader path in `android_core/Renderer` at all.**
Steps 1 and 2 of §3 have since been built and measured — see §2b — so `AddCustomShader` and
`CustomShaderPass` now exist and a trivial custom material draws. §1 is what the survey found and
still describes what the REAL shader needs; nothing in it has been done.

The state it started in is worth keeping, because it is the failure mode any engine app would
have arrived into: **the hooks came across without the path**, which is worse than nothing. `MESH_MODE_SHADER` and
`Mesh::custom_shader_index` both came across with the merged `win32_core/Mesh.h`, so a mesh can be
tagged and look supported. `Renderer::GroupObjectsByMesh` buckets on `IsSkinnedMesh()`, and a
`MESH_MODE_SHADER` mesh answers `false` — so it lands in the **ordinary opaque bucket** and gets
drawn by `default_android.vert`/`default_android.frag`. A blast would appear as a solid grey box.

The engine does not have this problem: `Renderer::RenderUniqueMeshes` filters on
`rendering_mode` and skips a `MESH_MODE_SHADER` mesh whose `custom_shader_index` is unset, with a
one-shot warning. Ours now does the same — that guard is step 0 and is already in.

---

## 1. The four blockers, measured

### 1.1 Fragment-stage SSBOs do not exist on this device

```
GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS    = 0
GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS  = 0
GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS   = 35
```

(`docs/gles31-ssbo-ubo-limits.md`, measured.) This is legal: GLES 3.1 allows per-stage storage
block limits to be zero, and this driver supports SSBOs **only in compute**.

`apps/bomber/assets/shaders/bomber_explosion.frag` declares two:

```glsl
layout (std430, binding = 0) buffer InstanceDataBuffer{ ... };
layout (std430, binding = 2) buffer LightBuffer{ ... };
```

Both fail at **link**, exactly as `default_skinned.vert` did in the vertex stage. Same wall, same
fix, and **this is the cheapest of the four** because the data already exists in the right shape
on the Android side: the instance UBO is bound at binding 2 and the light UBO at binding 3, both
std140, both already read by `default_android.vert`/`.frag`. The custom shader declares the same
two blocks instead of the SSBOs and reads them unchanged.

The one real consequence is the **fixed array size**. A UBO needs a compile-time bound where an
SSBO does not, so `MAX_INSTANCES`/`MAX_LIGHTS` become `#define`s that must agree with
`RENDERER_MAX_INSTANCES`/`RENDERER_MAX_LIGHTS`. That is already the convention in every other
shader here.

> **For the Win32 side:** the engine assumes throughout that a fragment shader can read an SSBO.
> That assumption is not portable to GLES 3.1 *at all* — not "on old devices". Anything that is
> meant to run on both wants its per-instance and per-light data in a UBO, which costs desktop GL
> nothing.

### 1.2 `sampler3D` bound at texture unit 25, on a 16-unit device

```glsl
layout (binding = 25) uniform sampler3D noise_texture;
```

```
GL_MAX_TEXTURE_IMAGE_UNITS          = 16   (per fragment stage)
GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS = 96
```

Unit 25 is out of range for the fragment stage. It has to be remapped into this port's unit map,
which is allocated in `android_core/Renderer.h`:

| unit | owner |
|---|---|
| 0–7 | material diffuse textures (`RENDERER_MAX_TEXTURE_UNITS`) |
| 8 | blit (`RENDERER_BLIT_TEXTURE_UNIT`) |
| 9 | shadow map (`RENDERER_SHADOW_TEXTURE_UNIT`) |
| 10 | `UIOverlay::ATLAS_TEXTURE_UNIT` |
| 11+ | free |

That map exists because **texture units are global GL state and the last binder wins**. Two
separate bugs in this port have come from ignoring that: the blit sampling the scene into a
material, and the UI overlay's SDF atlas displacing whichever material held unit 0 (which made a
grass tile render red, and cost an afternoon). A custom shader that binds a 3D noise texture every
frame owns whatever unit it picks, forever — so it gets a reserved one, named next to the others.

`sampler3D` itself is fine: GLES 3.0+, and `GL_MAX_3D_TEXTURE_SIZE = 4096` here.

> **For the Win32 side:** `binding = 25` reads as "somewhere out of the way", which is true on a
> desktop driver advertising 32+ units and false on the GLES minimum of 16. A reserved-unit
> constant shared by both trees would make this a non-issue.

### 1.3 `noise3d.comp` — portable, but better tooled out

```glsl
#version 430 core
layout (local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(rgba8, binding = 0) uniform image3D imgOut;
```

Compute genuinely works on this device (`MAX_COMPUTE_SHADER_STORAGE_BLOCKS = 35`), and GLES 3.1
has compute, `image3D` and `imageStore` in core. So this is a straight `#version 430 core` →
`#version 310 es` port plus precision qualifiers.

**But it runs exactly once, at startup, to fill a texture that never changes.** Porting it drags
in `Texture::Create3D`, a dispatch, image-unit binding and a memory barrier — a whole subsystem of
runtime machinery for a constant.

**Bake it instead.** A tool writes the 3D worley once and the result ships as an asset. At 32³
RGBA8 that is 128 KB, at 64³ it is 1 MB, against an APK whose size ledger is already tracked. This
repo already has the shape for it — `tools/pack_assets` is a host-side C++ tool built with MinGW
and driven from `tools/pack_assets.mk`, and the same pattern would give a `tools/make_noise3d`
that shares the generator source with nothing and outputs a raw or KTX volume.

The generator is ~90 lines of worley and the compute shader is the reference implementation, so
the tool is a transcription rather than a design. Doing it this way also makes the noise
**reviewable and diffable** — a baked asset can be looked at, and a dispatch cannot.

### 1.4 The pass itself

`Renderer::CustomShaderPass` is the actual work. What it does upstream, and what each part needs
here:

| upstream | on GLES 3.1 |
|---|---|
| draws after the opaque/deferred pass, translucent | same, but into `deferred_fbo`'s colour attachment **before** `BlitRenderTargetToScreen`, so it can depth-test against the solid scene |
| binds the G-buffer so the march can stop at solid geometry | **our deferred depth is a RENDERBUFFER, not a texture** — it cannot be sampled. Plain depth testing covers a simple volume; a raymarch that terminates on scene depth needs `InitDeferredTarget` changed to a depth texture |
| `glClearNamedFramebufferfv` | DSA, desktop-only. Needs the `glClearBufferfv` arm (the pattern is already all over `android_core/Renderer.cpp`) |
| `glBlendFuncSeparate`, `GL_ONE/GL_ONE_MINUS_SRC_ALPHA` on alpha | core in GLES 3.0, ports unchanged. **Do not simplify this** — the engine's comment on it is right: ordinary blending squares the coverage in the alpha channel, which only shows up once something composites the result |
| `MRT` colour + normal + objectid bound | must restrict `glDrawBuffers` to attachment 0 for this pass, or a translucent shader writes garbage into the integer normal/objectid attachment |
| the low-resolution half (`Shader::f_lowres`, `lowres_scale`, its FBO, `lowres_composite.*`) | **see §2 — this is not optional here** |

---

## 2. Cost, and why the low-res half is load-bearing

Measured on the MDT740 with `bomber_test_port`, deferred pass, three skinned rigs (56k vertices),
four tiles, shadows on:

| | deferred pass |
|---|---|
| static meshes | 30.59 ms |
| skinned, reference pose | 33.44 ms |
| skinned + animated | 33.93 ms |

So the scene is **already ~30 FPS with nothing translucent in it**, and it is fill-bound rather
than geometry-bound (CPU `DrawFrame` is 5–8 ms against 30+ ms of GPU).

`bomber_explosion.frag` is a 913-line per-pixel raymarch which, by its own accounting, costs
"between two and nine noise fetches per view step" plus an optional light march that is "nine noise
evaluations PER LIGHT". Over whatever screen area a blast box covers, at full resolution, on a
Mali‑T720.

**It will not fit in what is left.** So the low-resolution path is not an optimisation to add once
it is working — it is the thing that makes it feasible at all, and it should be ported *with* the
pass rather than after it. A volume drawn at 1/2 or 1/4 and composited is the standard answer and
the engine already has the whole mechanism.

**Port the shader's own instrument early.** `bomber_explosion.frag` has a debug mode that reads
out, per pixel, the number of 3D noise fetches actually performed — "the honest unit of this
shader's expense". That is worth more than a frame counter, because it tells you *where* the cost
is rather than that there is some.

---

## 2b. Measured, once step 2 was built

Steps 1 and 2 are done. `Renderer::AddCustomShader` / `CustomShaderPass` exist, and
`bomber_test_port` draws N instanced analytic fireballs through them — one ray/sphere
intersection, no march, no texture, no noise. Three things came out of it that were not in the
survey above, and two of them matter more than anything in §1.

### 2b.1 A separate render pass into the same FBO costs a full tile reload

**This is the single most important finding here, and it is invisible on desktop.**

`CustomShaderPass` was first written the obvious way: bind `deferred_fbo`, draw, unbind. But
`DeferredPass` had *already* unbound it a moment earlier. On a tile-based GPU an unbind ENDS the
render pass, and coming back to the same FBO means the colour and depth attachments are reloaded
from main memory into tile memory and written out again.

Measured on the MDT740, same scene, same camera, three blasts:

| | Deferred | Custom | Sum |
|---|---|---|---|
| custom pass binds/unbinds its own render pass | 0.007 ms | 42.627 ms | 42.6 ms |
| custom pass continues the deferred render pass | 33.797 ms | 0.001 ms | 33.8 ms |

**8.8 ms — about a quarter of the frame — for nothing but the round trip.** Note also how it
presents: the cost did not *appear*, it *moved*. The deferred timer read 0.007 ms, which looks
like the deferred pass got faster.

So `DeferredPass` no longer unbinds; `Render()` closes the render pass once, after
`CustomShaderPass`. Anything else added between them — a translucent pass, a decal pass — must go
inside the same binding.

### 2b.2 The per-pass GPU timer cannot see the custom pass on a tiler

The same property that makes §2b.1 work makes the timer useless for it: fragment work is executed
when the render pass RESOLVES, not when the draw is issued, so the whole frame's fragment cost is
charged to whichever query is open at the flush. `custom_pass_ms` reads ~0.001 ms however
expensive the shaders are, and `deferred_pass_ms` absorbs it.

Read the **Sum**, and change one thing at a time. The row is kept and labelled rather than
removed, because the split still means what it says on an immediate-mode desktop GPU.

### 2b.3 The baseline, and what it implies

The bench's blast is about as cheap as a screen-covering effect can be. Deferred+custom, one
render pass, same camera:

| blasts | GPU (Sum) | delta vs none | per blast | FPS |
|---|---|---|---|---|
| 0 | 31.51 ms | — | — | 21.1 |
| 3 | 33.80 ms | +2.29 ms | 0.76 ms | 18.5 |
| 8 | 46.68 ms | +15.17 ms | 1.90 ms | 16.9 |
| 16 | 72.17 ms | +40.66 ms | 2.54 ms | 13.4 |

**Per-blast cost rises with count**, which is the fill-bound signature: these overlap more as they
are added, and what is paid for is covered pixels times overdraw, not objects. Sixteen of them
more than doubles the frame.

And that is with **one ray/sphere intersection per pixel**. `bomber_explosion.frag` does two to
nine 3D noise fetches per march step, over tens of steps, plus an optional light march at nine
evaluations per light. Two orders of magnitude more work per pixel, on a device where the trivial
version already costs 2.5 ms a blast.

**So §2's conclusion is now measured rather than argued: the low-resolution half is mandatory.**
Half resolution is a 4x cut in the pixels that do the work, and it is the difference between this
being possible and not.

### 2b.4 A trap worth writing down: GL from the physics thread silently does nothing

The blast's `uBlastAge` was pushed from `RunSimulationTick`. That runs on the physics thread,
which has **no EGL context** — a context is current per thread. So `glProgramUniform` there does
not fail, it does nothing at all, and `glGetError` has nowhere to record it.

The symptom: the pass ran, cost 0.001 ms and drew nothing, because the age stayed at its initial
0.0 and an age-zero blast has radius zero and discards every fragment. It looked exactly like a
culling bug.

The value is simulation state and still belongs on the physics thread; only the PUSH moved, to
`Application::PreRender` (render thread, before anything is drawn). Anything an app computes in
the simulation and feeds to a shader needs that same split.

## 3. Suggested order

Staged the way the skinned path was, for the same reason: each step is a strictly larger slice, so
the first one that misbehaves names the layer.

1. **The guard** — `MESH_MODE_SHADER` meshes are skipped by the opaque bucket and warn once,
   instead of being drawn as solid boxes. *(done)*
2. **The pass, against a trivial shader.** *(done — see §2b.)* `AddCustomShader` +
   `CustomShaderPass`, drawing an analytic shape with no march and no texture. This separated "the
   pass works" (blending, depth, draw buffers, instancing, and a FRAGMENT-stage read of the
   instance UBO) from "the march is affordable", and produced the baseline in §2b.3.
3. **The low-resolution half** — `f_lowres`, the FBO, the composite. Do it before the real shader,
   not after, and measure the same trivial shader through it so the saving is known rather than
   assumed.
4. **The noise**, baked by a tool (§1.3).
5. **`bomber_explosion.frag` itself**, with its two SSBOs swapped for the existing UBOs and its
   sampler moved to a reserved unit. Bring its fetch-count debug mode across in the same step.

## 4. What this does not cover

- **`bomber_water.frag`**, the other custom shader in that app. Not surveyed; likely much cheaper
  (a surface, not a volume) but it has not been looked at.
- **Field shadows** (`field.vert`/`field.frag`/`field_jfa.comp`). A separate subsystem with a
  jump-flood compute pass; `Renderer::EnableFieldShadows` is a stub returning `false` here.
- **The ship's clouds** (`raymarch_volume.frag`), which the explosion descends from and which would
  come almost free once the explosion works.

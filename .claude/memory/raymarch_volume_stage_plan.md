---
name: raymarch-volume-stage-plan
description: "Raymarched volume material in ApplicationShip - clouds, depth occlusion and cloud shadows all working; Henyey-Greenstein phase function and a perf measurement are what is left"
metadata: 
  node_type: memory
  type: project
  originSessionId: 8982ea9a-a226-4342-b91e-141dae3f216d
  modified: 2026-09-10T14:38:52.023Z
---

Seb Lague-style raymarched clouds (github.com/SebLague/Clouds), built as a reusable render stage
rather than a fullscreen post pass, so only the volume's own pixels march.

**Working clouds as of 2026-09-11.** The reusable stage, the scene-depth clamp, 3D worley
density, occlusion of geometry inside the volume and cloud shadows onto the world are all in and
verified in-app. What is left: a Henyey-Greenstein phase function (for
sun-facing silver lining), and performance work - the cost is view_steps x light_steps 3D
texture fetches per pixel and has NOT been measured, because the `screenshot` MCP path costs
~250ms in readback+PNG encode and swamps the frame time. Use the in-app PerfTimer UI for that.

`shaders/noise3d.comp` generates the density: 3D worley, inverted so cell centres are dense,
four doubling frequencies packed in RGBA so `density_at()` builds an FBM from ONE fetch. It is
tileable - cell lookups wrap modulo the cell count - which matters because the wind scrolls
`uvw` past 1.0 within seconds and a non-tiling texture would put a hard plane through the cloud.
Generated once at startup into a 128^3 RGBA8 `GL_TEXTURE_3D` (`Texture::Create3D`, LINEAR +
REPEAT on all three axes) and bound at `TEXUNIT_APP_RESERVED` (25, above the cubemap at 24;
`UploadMaterials` now warns if material textures grow that far). Shape comes from three things,
all on sliders in the Volume panel: the FBM weights, a threshold that cuts most of the box to
empty air (without it the box just fogs up uniformly), and an object-space edge falloff so the
cloud has no flat sides.

## Lights in the volume (2026-09-10)

The march is lit by the sun AND every point/cone light, following the user's reference shader
(pasted at `shaders/shadertoy_smoke_lights.hsls` - Shadertoy URLs 403 to WebFetch, so ask for a
paste rather than trying to fetch one). Deliberately NO per-light culling: at most about three
are ever active because the lasers and particles condense into one light, so a reach test would
cost more than it saves.

Two things here are easy to get wrong:

- **Never normalise a light direction in object space.** The whole shader depends on `t` being a
  world distance, and under non-uniform scale the normalised local vector is not the local image
  of the normalised world one. Divide the object-space offset by its WORLD length instead: that
  gives the local vector whose world image is a unit vector, and the same length is the falloff
  distance. `mat3(mat_transformscale)` does the offset conversion.
- **`light_march` has to stop AT a point light**, not at the box face, or density behind the
  light shadows it.

`light_energy` is a vec3 now, since lights of different colours must be accumulated coloured.

Falloff is `brightness/pow(dist,light_falloff)` with light_falloff defaulting to 2 (the
reference's inverse square). NOTE that default.frag lights SURFACES with an exponent of 1, so a
light reads dimmer at range in fog than on a hull beside it - the slider is there to match them.

The sun's own `brightness` was ignored by the volume until 2026-09-10 - only its colour was
read - so dragging Brightness in the light inspector did nothing to the clouds. It is now
multiplied in, and `sun_intensity` (0.15) is a SCALE on it rather than a replacement: a scale is
needed at all because a surface turns radiance into pixels through a BRDF and an NdotL while the
volume integrates over density and step length. 0.15 x the sun's 7.0 reproduces the previous
look. Point and cone lights always used brightness; the sun was the only one that did not.

**Cone lights**: `ConeLight` had existed in Light.h all along with nothing using it, so
`UploadLights` had no case and one in a scene was silently dropped. Added, with `cos_angle` =
cosine of the HALF angle (cone_angle is the full opening angle in degrees), clamped below 180
because cos(90)=0 is how the shaders recognise a plain point light. `default.frag` needed a cone
branch too - a cone has a direction, so without one it was shaded AS the sun. Its `light_value`
already carries the full intensity, so that branch passes 1.0 as the shading brightness; the
point and sun paths still double-apply brightness (once in light_value, again as radiance in
CalcDirectionalPBRLight) and were left alone because every existing light is tuned around it.

The ship carries a `headlight` ConeLight at the nose. Its local rotation is a half turn about up
because `Object::ref_forward` is (0,0,-1) while the ship model's nose is local +Z (the laser
emitter sits at +2 on that axis, the exhaust at -1) - without it the beam shines out of the tail.

**`Object::GetForward()` uses only the object's OWN rotation**, so for a child it is relative to
the parent - `GetWorldForward()` is the composed one. `UploadLights` was pairing a world
`GetWorldPosition()` with a local `GetForward()`, so the ship's headlight pointed in a fixed
direction and never followed the hull. Both the cone and directional cases use
`GetWorldForward()` now. The sun was unaffected only because it happens to be a root object.

That bug survived a round of "verification" because every test had the ship at identity
rotation, AND because `object_get` reported `forward` from the LOCAL rotation - so the readback
looked right. `world_forward`/`world_up` are reported alongside now. Check a child's world axis,
not its `forward`, and always test a parented thing at a NON-zero parent rotation.

Design points worth not re-deriving:

- **The volume's box IS the Object's transform**, read in the fragment shader from
  `instance_data[vmatselect].mat_transformscale` (`default.vert` passes `vmatselect` =
  `gl_InstanceID` at location 9). No box uniforms, and volumes still batch - N volumes are N
  instances of ONE shared mesh in a single draw call (`ApplicationShip::AddVolume`).
- **`vmatselect` was DECLARED in default.vert but never assigned** until 2026-09-11. Every
  instance therefore read `instance_data[0]` and drew at the first one's transform. Invisible
  with a single volume, which is why it survived all of step 1's verification - the second
  volume added a year later rendered nothing at all. If instanced custom-shader geometry ever
  goes missing again, check that the varying is actually written, not just declared.
- Volumes are NOT depth sorted (one instanced draw) and write no depth, so two that OVERLAP on
  screen blend in instance order rather than back to front. Keep them apart.
- The ray is taken to object space **without renormalising**, so `t` stays a world-space
  distance. Renormalising silently breaks non-uniformly scaled volumes.
- `Renderer::custom_shaders` + `AddCustomShader()` returning an index that goes in
  `Mesh::custom_shader_index`. Replaced the single `deferred_shader_custom` slot; both
  ApplicationShip's volume and ApplicationIsoAnimation's targeting arc now coexist.
- `CustomShaderPass` runs LAST of the geometry passes (after skinned) and binds the G-buffer on
  `TEXUNIT_GBUFFER_*` (units 1-3: 0 is the shadow map, materials start at 4, 24 is the cubemap).
  `DeferredPass` moved to BEFORE the color pass to make that possible. `MESH_MODE_SHADER` meshes
  are no longer drawn in `DeferredPass` at all.
- The march clamps to the **position** buffer, not the depth buffer - a world-space point needs
  no inverse projection. Depth is read only as the "is there geometry here" test, because the
  position attachment clears to (0,0,0), which is a real place.

## The trap that cost the most time

`Shader::Setmat4` / `Setint` / `Setvec3` look the uniform location up in their own `progid` but
then call `glUniform*`, **which writes to whatever program is currently bound**. Setting a
uniform on a shader that is not bound silently writes it into a different shader. Inserting
`DeferredPass` (which binds `deferred_shader`) ahead of `shader->Setmat4("mat_worldcam", ...)` in
`DrawFrame` sent the scene camera into the wrong program and left the main pass rendering from
the sun's shadow matrix - the whole scene drawn from the sun's viewpoint, which looked like a
plausible isometric view rather than an obvious error. Always `Use()` before setting uniforms.
**Fixed 2026-09-10**: the setters now use `glProgramUniform*` (added to `core/glad.h`/`.cpp` by
hand, following that file's typedef + GLAPI + wglGetProcAddress convention; the context is 4.5
core so 4.1 entry points are guaranteed). Uniform sets no longer depend on bind order.

That change breaks any call site that was relying on the old behaviour, and there was exactly
one: `RenderDepthPasses(shader,MESH_MODE_SKINNED)` passed the DEFAULT shader while
`skinned_shader` was bound, and only worked because the write landed in the bound program. It
now passes `skinned_shader`. Verified by checking skinned character shadows still render in
ApplicationIsoAnimation - the Ship app has no skinned meshes, so it cannot catch this.

## Verifying visual work in this app

- `screenshot` and `camera_get`/`camera_set` are CORE MCP tools (the user promoted them
  2026-09-10). `camera_set` only holds BRIEFLY in ApplicationShip - `RunLogic` pulls the camera
  back toward the tracked ship every frame, so it is fine for grabbing one screenshot but not
  for a multi-step A/B. Drive those by moving OBJECTS instead, and re-read `camera_get` rather
  than assuming where the camera is.
- **The grid cells are outline geometry only** - the cell interiors write nothing to the
  G-buffer. Do not use "over the grid" pixels as a stand-in for solid geometry; the ship and the
  door panel are the solid things in that scene. This invalidated two measurement attempts.
- The volume shader's `f_show_box` uniform (Volume panel > Debug View) has a G-buffer readout
  mode: red where the depth buffer says geometry, blue where it does not. All blue means the
  G-buffer is not reaching the shader.
- The app's `uniform_callback` overwrites shader uniform defaults every frame, so changing a
  default in the .frag does nothing while the app pushes its own value.

## Volume occlusion: the depth TEST had to go (2026-09-11)

The volume rasterises the box's BACK faces, so its fragment depth is the box's FAR side. With
`GL_DEPTH_TEST` on (set once in `Renderer::SetOpenGLState` and never turned off), anything solid
standing INSIDE the volume is nearer than that far face and the fixed-function test threw the
fragment away - on exactly the pixels that needed fog in front of it. The scene-depth clamp in
`raymarch_volume.frag` was written for that case and could never run: it was only reached where
the far face had already passed, i.e. where nothing solid was nearer than the box exit, and
there it is a no-op.

Fix: `glDisable(GL_DEPTH_TEST)` in `ApplicationShip::SetVolumeUniforms` alongside the existing
`glCullFace(GL_FRONT)`/`glDepthMask(GL_FALSE)`, restored in `Renderer::CustomShaderPass` next to
the cull/depth-mask restore. The shader then owns depth entirely - it clamps to the G-buffer
world position and discards when the scene is in front of the box. Cost is the loss of early-Z
for a volume hidden behind a wall; those pixels now run the prologue and discard before the
march loop.

Two things NOT to redo here:

- **Do not add `MESH_MODE_LINE` to `DeferredPass`.** It looks like the obvious follow-up (the
  G-buffer is now the only thing stopping the volume painting over something) but the ONLY line
  mesh in the codebase is the rp3d debug wireframe built in `Scene.cpp` - a transient,
  deliberately non-pickable debug overlay. Putting it in the G-buffer would let a debug overlay
  occlude clouds and would pollute hover picking and SSAO. The grid is `gridcell` assets, i.e.
  ordinary `MESH_MODE_NORMAL` meshes, and was always in the G-buffer.
- **The grid is not a test case for volume occlusion.** It sits at y=-1, below the big bank's
  box (y 0..6), so it is beyond the box's far face and always passed the depth test - it was
  correctly fogged before this change too. Measuring grid pixels proves nothing about it. The
  test case is something INSIDE the box, and because the density threshold makes the noise very
  patchy, a ship parked at one spot often sits in a clear column and looks crisp either way.
  Let the wind drift cloud over a stationary object, or pick the spot by measurement.

## Cloud shadows: a Beer shadow map (2026-09-11)

Clouds now cast onto the world. `shaders/cloud_shadow.comp` builds a 3D transmittance map from
the sun, `default.frag`'s `CalcCloudShadow` multiplies it into the sun term next to `CalcShadow`.
Verified in-app: soft cloud-shaped shadows land on the ground, and the map allocates at exactly
the predicted 256x256x32 R8 = 2048 KB.

**It does NOT replace the depth shadow map** (4096^2 DEPTH_COMPONENT32F, ~67 MB,
`Renderer::shadow_texture_size`). The two store different quantities - nearest opaque blocker,
compared against, versus accumulated transmittance, multiplied in - and neither can express the
other. They multiply. The cloud map is ~3% of the depth map's memory.

**Why it is 3D.** XY is position in the sun's frame like any shadow map; Z is how far along the
sun ray the RECEIVER is. A flat 2D map only answers "how much sun reaches the ground", which is
wrong the moment the ship is inside or above a bank - it would be shadowed by cloud beneath it.
The march already walks front to back, so storing running transmittance per slice is only extra
imageStores, not a second march.

The elegant part: **CLAMP_TO_EDGE on R is what makes the ends correct**, so `CalcCloudShadow`
bounds-checks XY but deliberately NOT Z. A receiver in front of the layer clamps to slice 0
(nothing in front of it yet); one below clamps to the last slice (the whole column). Adding a Z
bounds test breaks the second case.

**It is per light, like the existing one.** The third axis buys receiver depth, not more casters.
The engine is single-caster anyway: `RenderDepthPasses` takes the first `DirectionalLight` and
returns. Point/cone lights are deliberately not done - the volume's own `light_march` already
self-shadows from them, and they are all short-range things on the ship. Many lights would want
a froxel grid (per camera, not per light), not N of these.

**Its camera is NOT the sun's.** `ApplicationShip::FitCloudShadowCamera` refits an ortho box onto
the volumes' 8 corners every frame. The sun's own frustum is fitted to the scene (half-extent 30,
near 1, far 200) and spreading 32 slices over 200 units puts ~6 units in each slice while a bank
is 6 units thick - every receiver would land in one slice and the depth axis would buy nothing.
The fit measures corners against the camera's own forward/up/left rather than inverting its
matrix, so it does not depend on which handedness `lookatmatrix` uses.

Things that cost time or would:

- **`fmat4::inverse_transform()` only inverts RIGID transforms** - it is a transpose plus a
  translation, with no scale handling. The volumes are scaled 20x6x20, so inverting a volume
  transform with it silently gives the wrong box. The compute shader uploads FORWARD transforms
  and inverts in GLSL instead. The user said 2026-09-11 they would patch that function; check
  whether it handles scale before trusting it.
- **`GL_R8` is not in scope** in this project. It sits on the 1.1 `<GL/gl.h>`, so sized formats
  past that are hand-added to `core/glad.h`. `GL_R8` and `GL_R16F` are there now. `GL_R16F` is
  unused - it is the one-line upgrade (with the texture's storage format) if 8 bits ever bands
  on a large flat receiver, which is the thing to look at first if the shadows look stepped.
- **The wind moved out of `SetVolumeUniforms` into `PreRender`.** That callback fires during the
  colour pass, which is after the shadow map is built, so the shadow marched last frame's cloud
  and sat permanently one frame behind its caster.

## GLSL #include now exists

`Shader::ResolveIncludes` (core/Shader.cpp) handles `#include "x.glsl"` relative to the including
file, with `#line` directives so compile errors keep real line numbers - an error in the main
file reports as `0(line)`, one in an include as `<n>(line)`. An included file must not carry its
own `#version`.

It exists so `density_at()` has ONE definition (`shaders/density.glsl`), shared by
raymarch_volume.frag and cloud_shadow.comp. If those two ever describe different clouds the
shadow lands where the cloud is not, and it reads as a subtle misregistration rather than a bug.
Both programs get the shape uniforms from `ApplicationShip::SetSharedDensityUniforms`.

## Application::PreRender

New virtual, called at the top of `Application::DrawFrame` on the FRAME thread. For app GL work
that must happen before the colour pass and so cannot hang off a shader's `uniform_callback`
(that fires mid-pass, with the shader already bound - too late to fill a texture the pass will
sample). RunLogic is not an option: it is the logic thread and may not touch GL.

Related: [[project-overview]], [[running-app-is-user-driven]], [[mcp-native-tools-setup]].

# `testfx` — a fullscreen shader bench

A fourteenth app under `apps/testfx/`, for writing and looking at fragment shaders with nothing
else in the frame. Shadertoy's workflow, on this engine's pipeline, so that a shader that works
here can be lifted into one of the games as an effect.

Status: **built**, 2026-09-13. Everything below describes the app as it is; section 12 at the
bottom lists where it ended up differing from the plan and why. The core changes in section 9
are done and closed backlog item 61.

---

## 1. What it is, and what it deliberately is not

**Is:** one window, one screen-filling quad, one fragment shader, an ImGui panel to pick which
shader and to drive its uniforms, and MCP tools so the whole loop is reachable without a keyboard.
Edit a `.frag`, press reload, look at it.

**Is not:** a shadertoy clone. There is no in-app text editor and no attempt at byte-level
compatibility. Snippets are pasted into a file in a normal editor. The `i*` uniform names are
honoured so a snippet compiles unchanged, and that is the whole of the compatibility claim.

**Why it exists:** the engine already has the hard part — `Renderer::AddCustomShader` and
`MESH_MODE_SHADER` — but the only place to exercise it is inside a game, where a shader error
kills the app and where getting the camera back to the interesting spot costs forty seconds a try.
The ship's cloud shaders were developed that way and it showed.

---

## 2. How it renders

Through the existing custom-material pass, not around it. That is the single most important
decision here, and it is what makes an effect transplantable: whatever the bench draws, a game can
draw the same way, because it *is* the same way.

```
Application::DrawFrame
  └ Scene::DrawFrame
      └ Renderer::DrawFrame
          ├ depth passes          (nothing, unless test geometry is on)
          ├ DeferredPass          → fills the G-buffer the effect may sample
          ├ skybox / normal / skinned
          └ CustomShaderPass      → our quad, with our shader   ← the effect
          └ ResolveAA
  └ DrawImGuiUI                   → the bench panel
```

The quad is one `Object` holding a `MakeQuad(2,2)` mesh (`core/Primitives.h`), tagged
`MESH_MODE_SHADER` with `custom_shader_index` set from `Renderer::AddCustomShader`. Three things
make this work without touching the renderer:

- **`shaders/fullscreen.vert` ignores the camera.** `MakeQuad(2,2)` spans `-1..+1` in XY, which is
  already clip space, so the vertex stage is `gl_Position = vec4(position.xy,0,1)` and an
  `out vec2 vuv = position.xy * 0.5 + 0.5`. No `mat_worldcam`, no instance transform. The quad
  covers the viewport at any window size and at any camera position, which is the point.
- **Nothing culls it.** `Renderer::CullObjects` renders every visible object — there is no frustum
  test in this engine — so a quad whose vertices are nowhere near the camera cannot vanish.
- **The uniform callback owns the depth state.** `glDepthMask(GL_FALSE)` and
  `glDisable(GL_DEPTH_TEST)`, exactly as `ApplicationShip::SetVolumeUniforms` does, so the quad
  neither writes depth nor gets rejected by it. `CustomShaderPass` restores both after the pass.

`vuv` rather than `gl_FragCoord` is what the prelude builds `fragCoord` from, because the renderer
supports a restricted, offset viewport (`Renderer::viewport_x/_y`) and `gl_FragCoord` is in window
coordinates. Deriving `fragCoord = vuv * iResolution.xy` is correct whatever the viewport is doing;
`gl_FragCoord` would be off by the offset. This app never offsets its viewport, but a shader
written here is meant to be pasted into one that might (Tank does).

### Test geometry

Off by default, one checkbox on. Turning it on spawns a lit cube and a ground plane through the
ordinary pipeline, so the effect has a real G-buffer to composite against —
`TEXUNIT_GBUFFER_DEPTH/_POSITION/_NORMAL` are bound for every custom shader whether or not
anything was drawn. An effect that fades where it meets geometry cannot be developed against an
empty scene, and that is most effects worth transplanting. Read the `TEXUNIT_GBUFFER_*` block at
the top of `core/Renderer.h` before using it: **depth is the only channel that can say whether
anything was drawn at a pixel**, and reaching for position first is the trap.

---

## 3. Files

```
apps/testfx/
    makefile                    ROOT/PROJECT/APP_SRCS, includes engine.mk
    main.cpp                    asset roots, then ApplicationTestFX
    ApplicationTestFX.h/.cpp    the whole app
    assets/
        shaders/
            fullscreen.vert     the clip-space vertex stage, shared by every effect here
            shadertoy.glsl      the prelude: iTime, iResolution, iMouse, iChannel0..3, ...
            shadertoy_main.glsl the epilogue: main() calling mainImage()
            engine.glsl         the engine-side extras: G-buffer, camera basis, lights
            bluecube.frag       the first effect (section 8)
            bluecube2.frag      the same reference, second take: one light in a scattering cube,
                                analytic throughout, nine knobs - the file's header explains it
            _template.frag      copy this to start a new one
            gbuffer.frag        a diagnostic, not an effect: shows what the deferred pass left
        textures/
            (whatever gets dropped in; channels are assignable from the UI)
```

`main.cpp` declares `../assets` first and `../../../shared_assets` second, the same order the ship
uses, so the bench's own `shaders/*` win and anything it does not have falls through to the shared
defaults.

---

## 4. Shader conventions

**Every shader file is complete and self-describing.** The app compiles whatever file you picked
and has no notion of "kinds" — no C++ text splicing, no line-number fixups in compile errors. The
difference between a native effect and a pasted shadertoy snippet is which includes the file
carries, and the engine's own GLSL `#include` (`core/Shader.cpp`) does the work.

A shadertoy port, in full:

```glsl
#version 460 core
#include "shadertoy.glsl"

// ---- paste between the two includes, unchanged -------------------------------
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
// ------------------------------------------------------------------------------

#include "shadertoy_main.glsl"
```

**The include path is relative to the INCLUDING FILE'S DIRECTORY, not an asset name.** So it is
`#include "shadertoy.glsl"` and not `#include "shaders/shadertoy.glsl"` — the latter resolves to
`shaders/shaders/shadertoy.glsl` and fails. `Shader::ResolveIncludes` documents this and
`raymarch_volume.frag`'s `#include "density.glsl"` is the existing example.

`shadertoy_main.glsl` is one function: `main()`, calling
`mainImage(frag_color, vuv * iResolution.xy)`. `frag_color` itself is declared in
`shadertoy.glsl` rather than here, so that a NATIVE effect — which never includes the epilogue —
has it too; the rule that follows is *do not declare your own output at location 0*. An included
file must not carry its own `#version`, which `Shader::ResolveIncludes` also documents and the
prelude obeys.

Both `shadertoy.glsl` and `engine.glsl` carry an `#ifndef` guard, and `engine.glsl` includes
`shadertoy.glsl`. So a native effect gets the whole set from one include, a port gets the
shadertoy half from one, and a port that needs scene depth can include both in either order
without double-declaring `vuv`.

A native effect skips `shadertoy_main.glsl`, writes its own `main()`, and may include
`engine.glsl` for the G-buffer samplers, the camera basis and the light SSBO. That is the form
that gets pasted into a game.

The two are not exclusive: a shadertoy port that needs scene depth includes both.

---

## 5. What the bench sets on every shader

Pushed from `ApplicationTestFX::SetEffectUniforms`, the `Shader::uniform_callback` the pass calls
with the program already bound.

| uniform | | notes |
|---|---|---|
| `iResolution` | `vec3` | viewport size, `z = 1` — shadertoy's pixel aspect |
| `iTime` | `float` | **tick × timestep** — see below |
| `iTimeDelta` | `float` | `GetPhysicsTimestep()`, constant by construction |
| `iFrame` | `int` | the tick counter |
| `iMouse` | `vec4` | xy current, zw click origin, shadertoy's sign convention |
| `iChannel0..3` | `sampler2D` | fixed bindings on texture units **28-31** — the next four free above the cloud-shadow and occluder-field maps at 26/27; see the `TEXUNIT_*` block in `core/Renderer.h`. Assignable from the UI |
| `iChannelResolution[4]` | `vec3[]` | |
| `iChannelTime[4]` | `float[]` | all four are `iTime`; declared so a snippet using it compiles |
| `iFrameRate` | `float` | `1 / iTimeDelta`, so constant |
| `iDate` | `vec4` | `w` is `iTime`, the date components are **zero**. Declared so a snippet using it compiles, and fed from the tick rather than the calendar — a bench whose claim is "frame N looks like this" must not contain a value that changes at midnight |
| `iCamPos/Right/Up/Forward` | `vec3` | the engine camera's basis, pre-scaled by fov and aspect, so a native effect raymarches the scene's real camera with `dir = normalize(iCamForward + uv.x*iCamRight + uv.y*iCamUp)`. There is no general matrix inverse in `type_fmat4.h` — `inverse_transform` is rigid-only — so the basis is built on the CPU from the camera rather than inverting a view-projection in GLSL. |
| `gbuffer_depth/position/normal` | `sampler2D` | bound by `CustomShaderPass` on units 1–3, not by us |
| `mat_worldcam`, `eye_position` | | set by `CustomShaderPass` for every custom shader |

**`iTime` comes from the simulation tick, not from a wall clock.** `iTime = scene->GetPhysicsTick()
* GetPhysicsTimestep()`. Durations in this codebase are ticks and this is no exception, but the
real payoff is that `sim_pause` freezes the effect and `sim_step` advances it by an exact number of
frames — so a screenshot of frame N is reproducible, and "is this a timing bug or a shader bug" is
answerable. The app calls `SetPhysicsTPS(120)`: it simulates nothing, ticks are nearly free, and
50 Hz would visibly step a slow rotation.

`physics_tick` is a `std::atomic<uint64_t>`, so reading it from the render thread is fine.

---

## 6. Noise, as a texture

`iChannel0` defaults to a noise texture generated once at startup from `Application::rrand`:
`rrand->Generate(256,256)`, then `GetBuffer()`/`GetSquareSide()` uploaded as a `GL_R8` 2D texture.
`core/RRandom.h` already documents this path ("for an app that wants this noise on the GPU"); this
is its first caller.

Preferring the texture over a GLSL hash is deliberate and not only about speed: the bytes are
seeded and reproducible, the simulation can draw from the *same* stream, and a value read in a
shader can be checked against the same value read in C++. A hash function in GLSL can be none of
those things.

Caveat worth recording rather than discovering: `Application::rrand` is a single shared stream and
`docs/engine_backlog.md` already has an open item about off-tick draws shifting it. The bench draws
from it once, in `Init`, before anything else runs — which is the one safe place.

Any image under `assets/textures/` can be assigned to any channel from the UI
(`Directory::GetFiles` to list, `Texture::LoadFromFile` to load).

---

## 7. UI and MCP

One ImGui panel, "Effect". The engine's Scene and Inspector windows default off here
(`f_show_scene_window` etc. are already app-settable); the Engine window stays on for frame timing.

- **Which shader.** A list of every `*.frag` under `shaders/`, found with `Directory::GetFiles` at
  startup and on demand — adding an effect is dropping a file in the folder, not a rebuild.
- **Reload**, with the compile log shown in the panel when it fails, and the last good program
  still on screen behind it. This is the feature the whole core change in section 9 exists for.
- **Uniforms, reflected.** Walk `GL_ACTIVE_UNIFORMS` on the program and emit a widget per type
  (`float`/`vec2..4`/`int`/`bool`), reading the current value with `glGetUniform*` and writing it
  back through the `Shader` setters. `Application::RenderShaderUI` is a half-built version of
  exactly this; the bench finishes it. It means a new knob in a shader is a new `uniform` line and
  nothing else — no C++, no rebuild.
- **Time.** Pause/step/reset, and the tick and `iTime` shown as numbers.
- **Channels.** Four combos, one per `iChannel`.
- **Test geometry** checkbox (section 2).

MCP tools, so the same loop runs headless — `screenshot` already exists and does the looking:

| tool | |
|---|---|
| `fx_list` | the discovered effects, which is current, its compile state |
| `fx_select` | switch effect by name |
| `fx_reload` | recompile the current effect, return the compile log |
| `fx_set` | set a named uniform to a float / vec / int |
| `fx_state` | current effect, tick, `iTime`, uniform values, last compile log, and the source files it was built from |
| `fx_scene` | the test geometry on/off — **a sixth tool, added during the build**. The plan made it a checkbox and nothing else, which excluded the one caller the bench is most for: section 2 says an effect that fades where it meets geometry cannot be developed against an empty scene, so a headless caller working on exactly that would have needed somebody at the keyboard |

Each takes the usual `include_screenshot` passthrough via `MaybeAttachScreenshot`.

---

## 8. The first effect: `bluecube.frag`

> **This section was written from memory and described the wrong object.** It is corrected below;
> the original text is kept at the end of the section because what it got wrong is worth keeping.

A tilted, frosted, **translucent slab with a light inside it**, standing in a fog bank on a dark
navy ground. A rounded-box SDF, much thinner in z than in x and y, raymarched against the scene's
real camera; the emission is a soft field *integrated through the volume* between the ray's entry
and exit; the bevel carries a hard, near-white rim where light escapes; the seeded noise on
`iChannel0` supplies the surface grain and the specks; and the ground is lit by the slab rather
than reflecting it.

**One knob walks all four reference frames.** `glow_level` at 0.18, 1.15, 2.40 and 4.50 reproduces
`cube_example_1` through `_4` — measured, those are the values the verification screenshots were
taken at. That was the design goal and it drove three decisions that are not obvious:

- the emission is a **field integrated through the volume**, not a colour painted on the surface.
  A painted version matches any *one* frame and cannot walk between them, because what changes
  across the four is how far the light gets through the slab before the frost scatters it away;
- the source **spreads as it brightens** (`glow_spread`), or the bottom band only gets brighter
  while the top stays black and the sweep stalls around frame 2;
- the emission **desaturates as it saturates**, or a strongly coloured emitter under a per-channel
  Reinhard never reaches white and frame 4 is unreachable at any value.

Written in the **native** form — its own `main()`, raymarching `iCamPos`/`iCamForward` so the
engine camera orbits it — because that is the form a game would paste, and the point of the bench
is the paste. A second file, `_template.frag`, carries the shadertoy form so both paths are
exercised from day one.

Everything worth tuning is a plain `uniform` with a default — 34 of them — which the
reflected-uniform panel turns into widgets for free.

### What the original text said, and why it is worth recording

> A slowly spinning blue cube, per the reference frames: a rounded-box SDF raymarched against a
> dark blue vignette, lit by one key light; hard cyan highlights along the edges where the light
> grazes; a fresnel-weighted reflection of a procedural sky gradient, which is what makes it read
> as polished rather than matte; ground haze with the noise texture driving it; a soft contact
> reflection below.

That is an **opaque, polished object lit from outside**. The frames are an **emissive frosted one
lit from within**, and the tell is in the frames themselves: the cube is *darker than its
background* in frame 1 and *brighter than everything* in frame 4, which no externally lit surface
does. The first build followed the description faithfully and produced a blue plastic dice.

Two smaller things went the same way. "A soft contact reflection below" was built as a second
raymarch off the ground plane — a true mirror, the most expensive thing in the shader — and the
frames show a *pool of light*, because the object is self-luminous; driving it from the same
emitter the interior integral uses is both cheaper and correct. And "slowly spinning" at a rate
that reads as gentle on paper is 4.3°/s, which loses the tuned pose inside twenty seconds; it
rocks now, with the spin still available and off by default.

The lesson is not that the description was careless — it is that a *mechanism* cannot be recovered
from a memory of how something looked, and a plan that names one is making a claim that has to be
checked against the reference before anything is built on it.

---

## 9. Core changes

Two, both additive, both agreed before starting.

### 9.1 A compile error must not kill the process

`Shader::CompileVertex/CompileFragment/CompileCompute` and `LinkProgram` call `debug->Fatal` on
failure, which is `exit(1)` (`core/Debug.cpp:149`). A typo in a shader closes the app. For a bench
whose entire purpose is compiling shaders that do not work yet, that is the difference between
usable and not.

Add to `Shader`:

```cpp
bool f_fatal_on_error = true;   //unchanged for every existing caller
std::string compile_log;        //the info log from the last build, success or failure
```

When `f_fatal_on_error` is false the four functions log through `debug->Err`, record the log, and
return failure instead of exiting. Every existing call site keeps the current behaviour, because
the default keeps it.

**Two sharp edges to handle while in there**, both latent today:

- `LinkProgram` returns `0` on failure, not `-1`, so `progid` becomes `0` — and every check in the
  repo is `if (progid != -1)`. A soft failure would sail straight past
  `ApplicationShip::ReloadVolumeShader`'s guard. The soft path must set `progid = -1`.
- If a compile returned `0`, linking must be skipped entirely: `glAttachShader(prog, 0)` is a GL
  error, and today that case cannot arise only because the compile already exited.

### 9.2 `Shader::Reload()` and a core `shader_reload` MCP tool — backlog item 61

Backlog 61 asks for this and names the agent-driven shader iteration case as the reason. The bench
is that case, so it gets built here.

**`Shader::Reload()` reloads in place.** It `ReleaseFile`s every entry in `source_files` (the whole
include set, not the two filenames — `density.glsl` arrives through an `#include` and is the file
most worth editing live), rebuilds into a *new* program id, and swaps `progid` only on success,
deleting the old program. In place matters: nothing holding a `Shader*` goes stale, so the
`renderer->custom_shaders.at(index) = new_one` dance disappears and
`ApplicationShip::ReloadVolumeShader` collapses to roughly one call.

`FILE_RELEASE_EMBEDDED` is an answer, not a failure — a packed build has no file behind the asset,
so `Reload` reports "not available in this build" rather than rebuilding identical bytes and
claiming success. That is the failure the whole backlog thread was about.

**A registry, so the tool can find shaders without every app writing one.** `Shader`'s constructor
adds `this` to a static list and the destructor removes it. `shader_reload` then walks it and
matches on `fname`/`vname`. Every shader in every app becomes reloadable at no cost to the app,
which is what item 61 actually asked for.

**The render-thread hook.** Reloading touches GL, and MCP handlers run on their own threads. The
tool sets a flag; core services it at the top of `Application::DrawFrame`, which runs on the frame
thread and — checked — is overridden by no app. The tool then blocks until it has been serviced so
it can return the real compile log, the same shape as `StepPhysicsAndWait`.

---

## 10. Order of work — all done

1. Core 9.1 — soft compile. Smallest, and everything after it is nicer to develop with.
2. `apps/testfx/` skeleton: makefile, main, Init, quad, `fullscreen.vert`, a flat-colour
   `bluecube.frag` stub. First build, first screenshot.
3. The prelude/epilogue pair and the shadertoy template; verify a stock snippet compiles unchanged.
4. The noise texture from `rrand`.
5. The Effect panel: shader list, reload, compile log, reflected uniforms, time, channels.
6. Core 9.2 — `Shader::Reload` + registry + `shader_reload`; retire
   `ApplicationShip::ReloadVolumeShader`'s hand-rolled copy onto it and close backlog 61.
7. The `fx_*` MCP tools.
8. `bluecube.frag` for real. Written from this document's description, then **rewritten against the
   reference frames** once they arrived - the description named the wrong mechanism, see section 8.
9. Test geometry toggle, plus `gbuffer.frag` to see what it puts in the G-buffer.
10. Docs: `readme.md` app list, `CLAUDE.md` app list (`testfx` is the fourteenth),
    `docs/mcp_server.md` for `shader_reload` and the `fx_*` tools, backlog 61 moved to
    `engine_backlog_done.md` with its verification note.

**Verified while building, on 2026-09-13:** `bluecube.frag` and `_template.frag` both compile
and draw; a syntax error two includes deep in `shadertoy.glsl` comes back through `fx_reload`
as a GLSL log with per-source line numbers, leaves the previous program on screen and does not
take the app down; restoring the file and reloading produces a new program id, which is what
proves the file cache really was released; `fx_scene` turns the test geometry on and
`gbuffer.frag` then shows the cube and the ground plane out of the depth channel alone; and two
screenshots taken two seconds apart with `sim_pause` held are **byte-identical**, while one
`sim_step` changes them — which is the whole of the tick-driven `iTime` claim in section 5.

## 11. Known risks

- ~~**The reflected-uniform panel writes uniforms from the UI thread**, which runs with
  `physics_mutex` held but is *not* the render thread.~~ **This premise was wrong.** `DrawImGuiUI`
  is called from `Application::DrawFrame`, which runs on the render thread; it holds
  `physics_mutex` as well, but it is not a third thread. So the panel could have written uniforms
  directly and been safe.

  **The value map was built anyway, for a different reason that does hold: `fx_set` runs on an MCP
  thread, which really is neither.** One rule for the map — everything goes through `fx_mutex`, and
  `SetEffectUniforms` pushes the whole map on the render thread — is cheaper to keep true than two.
  It also buys something the plan did not anticipate: a value survives a recompile, so iterating on
  a shader does not cost you six sliders every reload.
- ~~**`Directory::GetFiles` needs a resolved path** (`ResolveAssetDirectory`).~~ **Already handled
  in core**, and has been since the asset-layout work: `Directory::GetFiles` takes an asset NAME for
  the folder and resolves it itself, returning full paths. So the app passes `"shaders"` and gets
  this app's own folder, because its root comes first. The real wrinkle was elsewhere and is worth
  recording: `GetFiles` logs `debug->Err` when a pattern matches nothing, so scanning
  `*.png *.jpg *.tga *.bmp` over a textures folder holding only a readme printed four errors at
  every startup. One scan of `*.*` filtered in C++ replaced it — an engine that cries wolf about a
  normal empty folder is how a real missing-asset error gets skimmed past.
- **The `apps/` list appears in three places** (`CLAUDE.md`, `readme.md`, and habit). Both written
  lists were *already* stale by one when this was built — `pinball` had been added and neither said
  so. Both now name all fourteen apps individually rather than counting them, which is the version
  that cannot silently go out of date by one.

---

## 12. Where the build differed from the plan

Everything above is the app as built; this is the short list of what changed on the way, so that
reading the plan against the code does not turn up surprises.

- **`#include "shadertoy.glsl"`, not `#include "shaders/shadertoy.glsl"`.** The engine's GLSL
  include resolves relative to the including file's directory, so the category prefix would have
  produced `shaders/shaders/shadertoy.glsl`. Section 4 now says so.
- **`frag_color` is declared in `shadertoy.glsl`, not in `shadertoy_main.glsl`.** So a native effect
  has it too, and the two files can be included together without declaring it twice. Both `.glsl`
  files carry `#ifndef` guards and `engine.glsl` includes `shadertoy.glsl`.
- **One extra MCP tool, `fx_scene`**, because the test-geometry toggle was otherwise keyboard-only.
  See the table in section 7.
- **One extra shipped shader, `gbuffer.frag`.** Not an effect — it draws the G-buffer, which is the
  part of the custom-material contract that is easiest to get wrong and hardest to see, and it is
  the only way to confirm section 2's test-geometry claim without guessing. It is also what a
  depth-aware effect that "looks broken" should be checked against first.
- **Three additions to `core/glad.h`/`.cpp`**, which is a hand-written loader that declares only
  what is used: `glDeleteProgram` (a reload that could not delete the superseded program would leak
  one per edit), `glGetUniformfv`/`glGetUniformiv` (how the panel learns a `uniform float fog = 0.4`
  default — there is no other way), and the GLSL type enums `GL_FLOAT_VEC2..4`, `GL_BOOL`,
  `GL_SAMPLER_2D` and friends, which GL 2.0 introduced and the ancient `GL/gl.h` behind this loader
  does not carry. That last one is why `Application::RenderShaderUI` could only ever tell a float
  from an int.
- **`Shader::Build`/`BuildCompute` were split out of the constructors.** A constructor that compiles
  cannot be told `f_fatal_on_error = false` first, and "construct it, then tell it not to die" is
  too late. `Shader::Reload()` is then the same two functions plus the file-cache handling.
- **`ApplicationBreakout::ReloadShieldShader` was converted too**, not just the ship's. It was the
  same hand-rolled copy and it never called `ReleaseFile` at all, so F5, the HUD button and
  `breakout_reload_shader` had all been recompiling the start-up bytes since the cache was added.
- **Section 8's effect was rewritten, not tuned**, once the reference frames arrived. See that
  section; the original description is kept there.
- **Four app-side fixes came out of tuning it**, none of which a plan would have predicted and all
  of which the bench found in one screenshot each:
  - the `rrand` noise texture is now **mipmapped**. White noise is the worst case for minification
    and an unmipped one striped the ground plane toward the horizon; it read as a fog bug.
  - the camera's **wheel zoom is clamped** to 0.4-40 units. A step proportional to distance is
    geometric in both directions, so scrolling out compounded - forty notches put the camera 300
    units away, and a black screen does not say which of the camera and the shader is wrong.
  - the default camera is framed for the effect rather than for a unit cube at the origin.
  - `fx_select` rescans the folder when a name does not match, so a file dropped in a moment ago
    is found by the call that names it rather than only by the `fx_list` after it.
- **The noise is 256x256 of WHITE noise, and that is a design constraint on every effect here.**
  Its features are one texel wide, so any sampling scale that puts a texel under a pixel averages
  to a flat 0.5 - the detail is not faint, it is mathematically absent, and no `strength` uniform
  brings it back. Two of bluecube's terms were invisible for three rounds because of it. The rule
  is written down at `speck_scale` in that file.

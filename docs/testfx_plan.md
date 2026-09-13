# `testfx` — a fullscreen shader bench

A thirteenth app under `apps/testfx/`, for writing and looking at fragment shaders with nothing
else in the frame. Shadertoy's workflow, on this engine's pipeline, so that a shader that works
here can be lifted into one of the games as an effect.

Status: **plan**. Nothing below is built yet.

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
            _template.frag      copy this to start a new one
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
#include "shaders/shadertoy.glsl"

// ---- paste between the two includes, unchanged -------------------------------
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
// ------------------------------------------------------------------------------

#include "shaders/shadertoy_main.glsl"
```

`shadertoy_main.glsl` is three lines: declare `layout(location = 0) out vec4 frag_color;`, call
`mainImage(frag_color, vuv * iResolution.xy)`, done. An included file must not carry its own
`#version` — `Shader::ResolveIncludes` documents that, and the prelude obeys it.

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
| `iChannel0..3` | `sampler2D` | fixed bindings, assignable from the UI |
| `iChannelResolution[4]` | `vec3[]` | |
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
| `fx_state` | current effect, tick, `iTime`, uniform values, last compile log |

Each takes the usual `include_screenshot` passthrough via `MaybeAttachScreenshot`.

---

## 8. The first effect: `bluecube.frag`

A slowly spinning blue cube, per the reference frames: a rounded-box SDF raymarched against a dark
blue vignette, lit by one key light; hard cyan highlights along the edges where the light grazes;
a fresnel-weighted reflection of a procedural sky gradient, which is what makes it read as
polished rather than matte; ground haze with the noise texture driving it; a soft contact
reflection below.

Written in the **native** form — its own `main()`, raymarching `iCamPos`/`iCamForward` so the
engine camera orbits it — because that is the form a game would paste, and the point of the bench
is the paste. A second file, `_template.frag`, carries the shadertoy form so both paths are
exercised from day one.

Everything worth tuning (rotation rate, roughness, fresnel power, glow colour, fog density) is a
plain `uniform` with a default, which the reflected-uniform panel turns into sliders for free.

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

## 10. Order of work

1. Core 9.1 — soft compile. Smallest, and everything after it is nicer to develop with.
2. `apps/testfx/` skeleton: makefile, main, Init, quad, `fullscreen.vert`, a flat-colour
   `bluecube.frag` stub. First build, first screenshot.
3. The prelude/epilogue pair and the shadertoy template; verify a stock snippet compiles unchanged.
4. The noise texture from `rrand`.
5. The Effect panel: shader list, reload, compile log, reflected uniforms, time, channels.
6. Core 9.2 — `Shader::Reload` + registry + `shader_reload`; retire
   `ApplicationShip::ReloadVolumeShader`'s hand-rolled copy onto it and close backlog 61.
7. The `fx_*` MCP tools.
8. `bluecube.frag` for real, tuned against the reference frames by screenshot.
9. Test geometry toggle.
10. Docs: `readme.md` app list, `CLAUDE.md` app list (`testfx` is the thirteenth), backlog 61 moved
    to `engine_backlog_done.md` with its verification note.

## 11. Known risks

- **The reflected-uniform panel writes uniforms from the UI thread**, which runs with
  `physics_mutex` held but is *not* the render thread. `glProgramUniform` against a program the
  render thread may be drawing with is the kind of thing that works until it does not. Safer: the
  panel edits a C++-side value map and `SetEffectUniforms` pushes it on the render thread. Costs a
  map, removes the question.
- **`Directory::GetFiles` needs a resolved path** (`ResolveAssetDirectory`) — `core/File.h` is
  explicit that anything not going through `LoadFile` must resolve the name first, and a fourth
  place forgetting to is a bug that looks like a missing file.
- **The `apps/` list appears in three places** (`CLAUDE.md`, `readme.md`, and habit). A thirteenth
  app that only updates one of them is how the list goes stale.

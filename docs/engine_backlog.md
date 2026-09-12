# Engine backlog

**Open work only.** Everything already done or decided against moved to
`docs/engine_backlog_done.md` — 48 closed items, with their verification notes intact, because
those notes are what a later regression gets checked against.

Items are drawn from two runs in which an agent built a game on this engine as an audit of it:

- `docs/tetris_findings.md` — `APP=Tetris`, items 1-43.
- `docs/breakout_findings.md` — `APP=Breakout`, items 44-62, plus new notes on 15, 23, 25 and 39.
  Items 63, 64 and 65 came out of reviewing and working on that run rather than out of the report.

Ordered by **implementation effort, not by importance** — that is what the bands are, and it is
deliberate: this list is read when someone has an hour free as often as when someone is deciding
what matters. Where an item is more important than its band suggests, it says so in its own text.

Numbers are stable. Nothing is renumbered, nothing is reused, and **28 was never assigned**. An
item that moves between bands keeps its number.

Status key: `[ ]` open · `[~]` partially done.

Last updated 2026-09-12, after closing item 47.

---

## Band A — minutes each, no risk

*Empty. Everything in this band has been done; new items land here as they are found.*

- [ ] **41. One light radius for the whole renderer.** `field_light_radius` sets how fast every
  shadow edge softens, because `light_t` has no size field to read it from. Adding one is the
  natural next step the moment two lights in a scene want different softness.

  *Note on cost:* `light_t`'s layout is repeated by hand in several shaders, so growing it means
  keeping those in step — the same care `Material.h` documents for its own struct, where the
  `emissive` field was appended last precisely so no existing offset moved. See the occluder field
  reference at the bottom.

- [ ] **43. The field is rebuilt every frame with no dirty flag.** One geometry pass plus
  log2(size)+2 dispatches, all of it repeated whether or not anything moved. Its share of the
  90 us (see the reference at the bottom) was not measured separately from the march's, so that is
  the first thing to find out - but either way an app whose world changes rarely is paying for a
  map that did not change.

- [ ] **45. No per-object "does not cast a shadow".** Text is geometry, so a label casts a real
  shadow — and a label two units clear of the panel behind it throws a crisp, perfectly legible
  second copy of itself onto that panel. It reads as a rendering fault rather than as a shadow.

  There is no way to say "this object is not an occluder". `Light::f_casts_shadow` turns a whole
  light off; `Object` has no say in whether it appears in a depth pass. Breakout worked around it
  by pressing every label up against the back panel (`TEXT_Z = BACK_Z + 0.40f`) so the offset
  collapses to a few pixels — which also pins the labels to a plane, and its "GAME OVER" banner
  would genuinely have looked better floating in front of the arena.

  What it should look like: `bool f_casts_shadow = true;` on `Object`, tested in
  `Renderer::RenderSingleDepthPass` next to the mesh-mode test already there, and in
  `RenderFieldPass` for the occluder field. The one subtlety that makes this Band B rather than
  Band A: depth passes batch per mesh and draw every instance, so skipping a single *object*
  means filtering the instance list rather than adding one `if`.

  Ranked second of seven in what the Breakout author would do to the engine next, and it is a
  precondition for the brick-dissolve effect they cut (see item 52's neighbourhood): a brick that
  stopped casting a shadow the moment it began dissolving would look worse than one that simply
  vanished.

- [ ] **60. `debug->Fatal` on the render thread produces no window and no visible reason.** A
  shader typo calls `debug->Fatal` (`core/Shader.cpp:175,205,235,273`) → `exit(1)` from inside
  `Init()`, before any window is shown. The result is a program that appears not to start, with the
  explanation only in a stderr log the user has to know to go and find. It is documented, and it is
  still the thing most likely to make a newcomer conclude the build is broken.

  Cheapest useful fix is a `MessageBox` on a fatal when no window exists yet, or a
  `wind_fatal.log` written next to the exe. **Item 44 removed one of the two ways to land here** —
  a missing uniform no longer calls `Fatal` — but the compile and link failures at the line numbers
  above are the common case and still do, so this stands on its own.

- [ ] **61. A core `shader_reload` MCP tool.** `ApplicationShip::ReloadVolumeShader` is the
  hot-reload pattern and it transfers directly, but every app that registers a custom shader has to
  write its own — and, more to the point, so does every app that wants one reachable by anything
  other than a keypress.

  **The half nobody had noticed was missing is now done: the cache can be told a file has
  changed.** `ReloadVolumeShader` was commented "recompiles from disk" and did not - it calls
  `LoadFile`, which served the `BinaryAsset` cache, so it recompiled the bytes read at start-up
  and produced an identical program. Every hot reload in this engine was a no-op from the moment
  the cache was added. Measured, not inferred: write a file, load it, rewrite it, load it again,
  and the original bytes come back.

  `ReleaseFile` (see `core/File.h`) is the fix, and `ApplicationShip::ReloadVolumeShader` now uses
  it. Three points that the MCP tool will need to carry over:

  - **It releases the whole source set, not the two filenames.** `shaders/density.glsl` arrives
    through a `#include` and is shared with `cloud_shadow.comp`, so it is the file most worth
    editing live; `Shader::source_files` records every file a program's source came from, at any
    include depth, for exactly this.
  - **`FILE_RELEASE_EMBEDDED` is an answer, not a failure.** With assets packed into the binary
    there is no file behind the asset, so the tool must report "not available in this build"
    rather than rebuild an identical program and claim success. That is the failure this whole
    thread was about.
  - **Releasing is the one thing that can invalidate a pointer `LoadFile` handed out.** Safe for
    shader source, which `Shader` copies into a `std::string`; not safe for a `Texture` or a
    `WaveFile`, which hold their bytes for life.

  **What is still open is the trigger.** The only way to reload anything today is one ImGui button
  in one app, which is the whole point of this item - and it is also why the reload path has not
  been exercised end to end: that button sits below the fold in a docked panel and the mouse wheel
  over it is taken by the camera. An MCP tool would make it testable as well as reachable.

  The Breakout run wired reload to F5 and to an ImGui button on the brief's advice and then barely
  used it, because it was changing C++ alongside the shader anyway. Where it paid was the one
  shader-only iteration: tuning a 45-tick flare that was drowning the effect it announced. A human
  can press F5; the agent doing the tuning could not reach the keyboard, and each attempt cost a
  rebuild, a relaunch and replaying the game back to the state worth looking at — about 40 seconds.
  Adding `breakout_reload_shader` turned that into an edit and a call, and its author named a core
  version as **the one piece of ergonomics this pass should get next**.

  Note what the app-level implementations have in common and must keep: the key, the button and the
  tool all only *raise a flag* that `PreRender` acts on, because `UpdateView` runs on the physics
  thread and may not touch the GL context. A core tool has the same constraint, so it needs a
  render-thread hook to act in, not just a registry walk.

## Band C — one to three hours each

- [ ] **23. `Destroy()` / reap policy.** `Object::Destroy()` only marks; only 2 apps of 11 call
  `Renderer::DeleteDestroyedObjects()`, so everywhere else a destroyed object leaves its rigid body
  in the physics world forever. Needs a decision about *where* reaping safely happens, not just a
  call added. *Later.*

  *Breakout note (2026-09-12):* now **4 of 12** — Dozer, Ship, Tetris and Breakout. The trend is
  itself the argument: every app that creates objects at runtime eventually discovers this and adds
  the call by hand, in a place it has had to reason about independently (Breakout calls it from
  `RunSimulationTick`, where the mutex is held and the render thread is not walking the object
  list). Four independent correct answers to the same question is a default waiting to be written.

- [ ] **42. Skinned meshes do not cast into the field.** `RenderFieldPass` draws
  `MESH_MODE_NORMAL` only; a skinned mesh would need its own variant of `shaders/field.vert`
  applying the bone transforms, exactly as `default_skinned.vert` does. A skinned character
  receives field shadows but does not cast one. Nothing that uses the field has skinned geometry
  yet.

- [ ] **52. No way to express "unlit" or "emissive only".** With `f_render_skybox = false` there is
  no environment to reflect, so a high `metallic` value gives up its diffuse term and gets nothing
  back. `metallic = 0.92` rendered Breakout's tough bricks **almost black**; they read correctly at
  0.45 with a little emissive.

  That is not wrong — it is what metallic means — but the failure looks exactly like a material
  that failed to load, and nothing in `core/Material.h` warns that a scene with no reflections
  cannot afford metal. A one-line note there is the cheap half. The real item is a material flag
  for "this surface is its own colour, do not light it", which is what a HUD element, a marker or a
  stylised game actually wants, and which today can only be faked with emissive.

- [ ] **63. `SoundSystem` welds an OpenAL buffer to an OpenAL source, so a sound cannot overlap
  itself.** This is *why* item 53's heap corruption was reachable, and it is worth recording
  separately: fixing `LoadFile` makes the workaround safe, it does not remove the reason anyone
  reaches for it.

  A **buffer** is immutable PCM data; a **source** is a voice with its own gain, pitch and play
  state. Many sources may play one buffer at once — that split is how OpenAL expresses polyphony.
  `SoundSystem` allocates `NUM_AL_BUFFERS` of each, pairs them 1:1 and keys both off one handle:

  ```cpp
  alGenBuffers(NUM_AL_BUFFERS, buffers);
  alGenSources(NUM_AL_BUFFERS, sources);
  map_handles[handle_name] = buffer_index;                 //one index means both
  alSourcei(sources[handle], AL_BUFFER, buffers[handle]);  //fixed pairing
  alSourcePlay(sources[handle]);
  ```

  So one sound is one voice. `alSourcePlay` on a source already in `AL_PLAYING` rewinds it rather
  than layering, so two bricks breaking on the same tick do not overlap — the second cuts the first
  off mid-attack, which is audible and reads as a dropout.

  **The forced workaround, and what it costs.** The only way to a second voice is a second source,
  the only way to a second source is a second handle, and the only way to a second handle is to
  call `AppendFile` on the same file again. That is why `ApplicationTetris` registers `click.wav`
  and `bleep.wav` twice each and why `APP=Breakout` wanted three handles on one WAV — and the
  redundant load is precisely the cache hit that made item 53 fire. Each duplicate also burns a
  buffer holding bytes identical to one already resident, against a ceiling of **16 shared by
  buffers and sources together**, which `AppendFile` enforces with `debug->Fatal` ("I'm lazy: no
  more sound buffers") — so the seventeenth registration exits the process. Dozer is already at 9
  handles, Breakout 8, Tetris 6.

  **The fix is to un-weld them:** buffers keyed by *filename* and deduplicated, and a pool of
  sources that nothing owns. `Play(name)` takes any source not currently `AL_PLAYING`, binds that
  buffer and plays it, stealing the oldest when they are all busy. Overlap then costs nothing and
  needs no thought: one `AppendFile`, ten simultaneous plays.

  **What makes this a design decision rather than a patch**, and the reason it is Band C: `Pause`,
  `Rewind` and `FinishedPlaying` all take a handle and assume it names exactly one voice, which a
  pool cannot answer — "pause the brick sound" is ambiguous once three of them are in flight.
  Those three really serve *sustained* sounds (music, an engine loop), which genuinely do want a
  dedicated source. So the API probably wants both concepts and should say which is which: a
  fire-and-forget one-shot off the pool, and an explicitly acquired voice for anything that will be
  paused, rewound or queried. Settle that before writing the pool, not after.

- [ ] **64. `tank_drive` does not drive the tank, and the recorded baselines say it used to.**
  Found on 2026-09-12 while smoke-testing the input merge, and **confirmed pre-existing**: the same
  test against a clean worktree at `0b3778e`, before any of today's changes, produces identical
  numbers.

  ```
  tank_reset, settle 60 ticks, then tank_drive {direction:"forward", amount:1.0, duration_ms:3000}
    step0 gas=0.00 fwd_speed=-0.00 pos=[-4.19,-0.01,-0.36]
    step1 gas=0.00 fwd_speed=-0.00 pos=[-4.20,-0.01,-0.37]
    step2 gas=0.00 fwd_speed=-0.00 pos=[-4.20,-0.01,-0.38]
  ```

  Same with `tank_step` instead of `sim_step`, and same free-running with no pause at all: the hull
  creeps a couple of centimetres and `gas_pedal` never leaves 0. The rp3d baseline recorded from
  this same scenario had the tank reaching its top speed, so something between that recording and
  now broke it. (That baseline has since been deleted as stale - see `docs/mcp_server.md` - so the
  evidence it used to work is the write-up rather than the file.)

  **Not the input layer.** That was the first suspicion and it is ruled out: the path this uses is
  `AddKeyMap(0,INPUT_AXIS_DRIVE)` + `HoldAxis` + `GetAxis`, and the harness written for items 48/49
  exercises exactly that and passes.

  Where to look next, in order:
  - `ApplicationTank`'s scripted block applies `ThrottleInput(ax_drive)` **only when non-zero**, so
    either `GetAxis(INPUT_AXIS_DRIVE)` really is 0 in the app (different from the harness — how?)
    or `controlled_vehicle` is not the vehicle being measured.
  - `window_focus` is false throughout an automated run, which skips the hardware block. The
    comment there says `TankCharacter` re-asserts an idle brake at the end of every tick and that
    the gamepad block is what normally clears it — so an unfocused scripted drive may be fighting a
    brake that nothing releases. That would explain a tank that creeps instead of accelerating, and
    it is the single most likely cause.
  - `gas_pedal` in the telemetry may itself only be written inside the focused block, in which case
    the 0 is a reporting artefact and only `forward_speed`/position are evidence. Worth settling
    first, because it decides whether the other two lines of enquiry are even needed.

  Band C is a guess: the fix may be one line once the cause is known, but the cause is not known.
  *Whoever picks this up: `tools/vehicle_mcp.py` already drives this scenario, so the before/after
  is one command each side of the fix. This wants doing BEFORE the rp3d baselines are re-recorded,
  or the new baseline will bake in a tank that does not drive.*

## Band D — half a day to a day each

- [~] **24. World-space text.** *The geometry half is done; the SDF half and everything above the
  primitive are open.* No bitmap font, no text mesh, one 13px ImGui font. `SpriteSheet`
  already loads an atlas — it just cannot reach the world. Pairs with item 19.
  **Options compared in `docs/text_rendering_options.md`** (2026-09-11): seven methods with
  pros/cons, how glyph selection actually works, and what is already in the tree. Headline: the
  vendored `imstb_truetype.h` is complete stb_truetype 1.26 and does bitmap baking **and SDF
  generation** (`stbtt_GetGlyphSDF`) **and** outline extraction, so the three most attractive
  options need no new dependency. Recommendation is an SDF atlas, one mesh per string, glyph
  selected by baked UVs, drawn in the custom shader pass — outline/shadow/glow then cost a
  `smoothstep` each, and one atlas serves every size. Copy the header out of `3rdparty/imgui/`
  first: a font system that includes from ImGui's folder has not achieved an ImGui-less build.
  Build the text primitive before any UI layer — it is useful on its own, and the UI framework
  (layout, hit-testing, focus) is weeks rather than days.

  **The GEOMETRY half is done** (2026-09-11), which is option D of that note rather than the
  recommended B, because Dick had already exported one mesh per glyph to
  `data/glyphs_unispace.glb` from `tools/blender_glyph_meshes.py`. `core/TextMesh.h` bakes a
  string into a single `Mesh` — glyph triangles copied along a pen and welded into one vertex
  buffer. `APP=Tetris` uses it for its captions, its three stats and its game-over banner, so the
  board no longer says anything through ImGui.

  Two things about its shape are worth keeping when the SDF atlas arrives:
  - **There is no atlas in it, and that is not an omission.** An atlas is texture bookkeeping.
    What a layout needs from a font is *metrics*, and for a monospaced set that is two floats
    (`advance` 0.509167, `line_height` 1.0, from `fonts_glyphs.json`). The two paths will share
    the *layout* and none of the storage; letting “atlas” into this API would have welded them.
  - **The builder loads nothing.** It takes a `GlyphSet` and depends on `Mesh` alone;
    `LoadGlyphSetFromGLB` is the only part that knows what glTF is and lives in its own
    translation unit. An SDF path is a second loader and a second builder, not a rewrite.

  Still open: the SDF/quad path itself, and everything above the primitive — layout, hit-testing,
  focus. Also still true that `imstb_truetype.h` must be copied out of `3rdparty/imgui/` before it
  is used, or an ImGui-less build has not been achieved. *Later.*

  *Breakout note (2026-09-12):* the geometry path was used again and held up — its author singled
  out the `reuse` parameter and the thread split (`MeasureText` safe anywhere, `BuildTextMesh`
  render-thread-only) as the right shape. The one thing that made it awkward in a real scene was
  item 45: extruded glyphs cast real shadows, and there is no way to say they should not.

- [ ] **26. Scene save/load.** `BuildSceneFromJSON` restores name/position/rotation of asset-backed
  objects and nothing else. *Later.*

  *Breakout note:* felt as the absence of a level format — that app's five layouts are hand-drawn
  C++ tables because inventing a scene format was not the assignment. Any game past its first one
  wants this, and a level file is a smaller and better-specified problem than a general scene
  format, so it may be worth splitting out.

- [ ] **35. Transform ownership, once `UpdateView` runs at framerate.** Item 17 keeps `UpdateView` on
  the physics thread under the lock, so it changes nothing here. Moving it to the render thread —
  which is where camera work belongs, since a camera should be smooth at display rate and not at
  50 Hz — raises three things, in increasing order of difficulty:
  1. **Writes.** An `Object`'s local transform needs exactly one writer. The mechanism already
     exists and is per-object: `Object::UpdatePhysicsState` writes the transform only `if
     (physics)`, and recurses into children without writing theirs. So the rule is simply *`physics`
     set means the tick owns the transform and the view may only read it; `physics` null means the
     view owns it and the tick already never touches it*. A camera that should follow a physics body
     is therefore **parented to it, not given a body of its own** — the parent's local transform
     stays tick-owned, the camera's local offset stays view-owned, and `GetWorldTransformScaleMatrix`
     composes the two. That covers chase and cockpit cameras with no new machinery. Worth an assert
     in the view path rather than a type restriction, so the genuinely physical camera stays possible.
  2. **Reads, and judder.** The view reading tick state at framerate is *safe* under `physics_mutex`
     (the render thread already holds it across `Renderer::DrawFrame` and `DrawImGuiUI`) but it is
     not *smooth*: a 50 Hz sim sampled at 144 Hz gives a chase camera a target that jumps 50 times a
     second, and camera smoothing will show it. Wants the previous tick's pose kept on `Object` and
     an interpolation alpha in `DrawFrame`. This is the real work of the item, larger than (1).
  3. **Feedback into the sim.** Camera-relative controls ("forward" meaning camera-forward) make a
     view-owned, framerate-driven orientation into simulation input, which breaks replay (item 25).
     Whatever the sim reads must be sampled at a tick boundary. Cheap to get right if it is designed
     in, nasty to retrofit.

  Nothing in the repo attaches physics to a `Camera` today, so all of this is latent. Note
  `Application::UpdateUICameraControls` is already a render-thread camera writer, called from
  `DrawImGuiUI` under the lock — the pattern half-exists and is correctly synchronised. *Later.*

- [ ] **39. A column is one slab, so it fills in its own gaps.** R and A are the extremes of
  everything in a column, so a falling piece above a stack merges with it and light cannot pass
  between the two. Exact for anything extruded from the ground, conservative for everything else,
  and hard to see in a fixed top-down view - but it is the reason this is not a general shadow
  technique. Two more channels would carry a second slab if an app ever needs it.

  *Breakout note (2026-09-12):* a second app used the field — a point light riding the ball,
  described as the best-looking thing in the game for two lines of setup — and this never bit,
  because the ball sits at the same depth as the bricks. Its author flagged that as **luck rather
  than design**: the moment something passes in front of something else along the field axis, the
  slab merges them. Still the first thing to fix if the field is ever used by a scene with depth.

- [ ] **40. The march still has a step floor, and it is load-bearing.** The distance field is zero
  everywhere directly above an occluder, so a ray running along the top of a wall would step by
  nothing, stall, and report "unoccluded" - the wrong answer in the place with the most geometry.
  `min_step = dist / field_shadow_steps` stops that, at the price of degrading to the old even
  spacing for such rays. The real fix is a max-mipmap pyramid over the height channel, which would
  give a conservative vertical bound to go with the horizontal one; the 2D distance alone cannot,
  because a neighbouring column one texel away may rise to just under the ray.

## Band E — multi-day, strategic

- [ ] **25. Record and replay** (step 7 of the deterministic-sim plan). Unblocked by item 22, but see the thread-ordering caveat there. Tetris
  is a better test case than a vehicle: 200 cells of `int8_t` either match or they do not, with no
  float tolerance to argue about. *Later.*

  *Breakout note (2026-09-12), and it changes the specification.* That run measured determinism
  directly (`tools/breakout_bot.py determinism`, same seed and same scripted input twice). Score,
  bricks remaining, shield charge to four decimal places, saves, power-ups caught and the whole
  board match **exactly** every time, and the ball's velocity is bit-identical. The ball's
  *position* matches on some runs and is a couple of ticks of travel out on others.

  The cause is the finding: **an MCP-driven input is not tick-aligned.** `HoldAxis` starts on
  whichever tick the physics thread happens to be on when the call lands, so two runs of one script
  begin their holds one or two ticks apart, and the same script then produces the same trajectory
  sampled at a different point along it. So the simulation is reproducible and the *harness* is
  not, and no amount of seeding fixes it.

  The consequence for this item: **the recording must capture the tick an input landed on, not
  merely the input.** Also see item 49 — a run driven by a real gamepad would today record none of
  the stick's motion at all, because the polled path never becomes an event.

- [ ] **27. Finish the skeletal animation system.** `ObjectAnimation.cpp:108` still has a
  `debug->Fatal` for any clip carrying a scale track. The probe was deliberately never started.
  *Later.*

---

## Reference: the occluder field

Context for items 39-43, which were split across bands above. Unchanged from when the field was
added on 2026-09-11.

Point lights cast shadows without a cube map. `Renderer::EnableFieldShadows` builds a single
top-down `RGBA16F` texture per frame holding, per world column, the height of the highest surface
(R), the lowest (A), and the 2D distance to the nearest occupied column (G, jump-flooded by
`shaders/field_jfa.comp`). `CalcFieldShadow` in `shaders/default.frag` sphere-traces the segment
from a receiver to a point light against it, stepping by that distance and using it again for the
penumbra estimate. The map mentions no light, so N lights cost N marches against one texture
rather than N shadow maps. `APP=Tetris` was the first user - see
`ApplicationTetris::SetupFieldShadows`, and the Engine panel's Renderer section for the live
controls. `APP=Breakout` is the second, for a point light that rides the ball.

Measured on the Tetris board at 512x512, vsync off, by alternating `f_field_shadows` every 300
frames inside one process: **349.5 us per frame with it on against 259.3 us off**, so about 90 us
for the geometry pass, eleven jump-flood dispatches and the per-fragment march together. That is
CPU-side frame time (`tmr_frame`), which for dispatches is submission rather than execution, so
treat it as an upper bound on what it costs the frame, not a GPU profile.

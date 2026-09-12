# Building a paddle game on this engine — findings

`APP=Breakout`, built 2026-09-12 against the working tree at `e20469d`.

Findings were collected as they happened; the prose was written at the end. Every measurement in
here was produced on this machine by `tools/breakout_bot.py` against the build in the tree, and
the tables are pasted from its output rather than retyped. Where a finding is **inferred from
reading** rather than observed, it says so in those words — several are, and that distinction is
load-bearing: an engine gap I reasoned my way around before it bit me is weaker evidence than one
that cost me an afternoon, and it should not be dressed up as the second thing.

---

## 1. Summary

There is a playable paddle game. A person can sit down with a mouse, a keyboard or a gamepad and
play it; a program can play it through MCP and measure it while it does.

**What is in it.** Eleven-by-eight brick wall over five hand-drawn level layouts that repeat with
everything promoted a step up the toughness ladder each cycle; four brick types plus
indestructible level geometry; three lives, a score with a combo multiplier, level progression and
game over. A ball swept by hand. A paddle driven by mouse, keys or an analog stick, all three at
once if you like. Power-up capsules that fall as real rigid bodies and have to be caught. Bricks
that burst into tumbling debris which piles up against the bricks still standing. A shield across
the bottom of the arena that is both the custom shader (deliverable 2) and a real gameplay
resource. Sound. World-space text. A point light riding the ball, shadowed through the occluder
field.

**What it cost.** Roughly 4,300 lines across six new files:

| file | lines | what it is |
|---|---|---|
| `breakout/Field.{h,cpp}` | 1,246 | the rules — no engine type in the header |
| `ApplicationBreakout.{h,cpp}` | 2,489 | the view, the wiring, the MCP tools |
| `shaders/breakout_shield.frag` | 217 | the custom material |
| `tools/breakout_bot.py` | 379 | the scripted driver everything below was measured with |

**What was cut.** Nothing from the floor the brief set, and nothing I regret. Two things were
scoped down deliberately: the levels are five hand-drawn patterns rather than a level file (there
is no scene format and inventing one was not the assignment), and there is one custom shader
rather than the two I sketched — the second was going to be a brick dissolve, and one effect that
is genuinely load-bearing seemed a better answer than two that are decoration.

**One core change**, in `core/File.cpp`, fixing a heap-corruption bug that has been latent in
`APP=Tetris` since it was written. Section 6 has the full entry. All twelve apps build; Tetris was
run and screenshotted afterwards and is unaffected.

---

## 2. The custom shader

### What it is

A force field across the bottom of the arena, on a single `MESH_MODE_SHADER` quad, drawn by
`Renderer::CustomShaderPass`. `shaders/breakout_shield.frag`, 217 lines.

It is not decoration. The shield's **charge is a gameplay resource**: it starts full, a save costs
0.34 of it, and breaking bricks refills it at 0.022 a brick. When it runs out, the ball dies. So
the thing the shader draws is a number the player is managing, and the shader is how they read it.

Driven entirely from the last tick's state, through `Shader::uniform_callback`:

| uniform | from |
|---|---|
| `shield_charge` | the gameplay resource, 0..1 |
| `shield_flare` | decays over 45 ticks after a save |
| `shield_time` | **the simulation tick count**, so the pattern drifts with the game and freezes when it is paused |
| `shield_focus` | the world position of whichever ball is lowest |
| `ripple0/1/2` | `(world_x, age_in_ticks)` of the last three impacts |
| `shield_origin`, `shield_size` | the quad's rectangle |

What it draws: two counter-drifting diagonal lattices; a bright line exactly on the collision
surface the rules bounce a ball off (so the player can *see* where the save happens rather than
having to learn it); an expanding gaussian ring per impact; a bloom that tightens under the
nearest descending ball; and a colour that runs red → amber → cyan with the charge, like a fuel
gauge. At zero charge it does not vanish — it drops to a faint ghost, because a pane that
disappears reads as a rendering bug rather than as "you are unprotected".

It also reads the **global light SSBO** and picks up the point light riding the ball, so the
shield genuinely catches the ball's own light as it comes down, without the app wiring anything
up.

### What the pass made easy

Registration is four lines and they are the four lines the brief promised:

```cpp
shield_shader = new Shader("shaders/default.vert","shaders/breakout_shield.frag");
shield_shader->uniform_callback = std::bind(&ApplicationBreakout::SetShieldUniforms,this);
shield_shader_index = renderer->AddCustomShader(shield_shader);
shield_mesh->mesh_mode = MESH_MODE_SHADER;
```

Two things beyond that were better than expected. The **G-buffer being bound for you** means a
soft intersection is four lines and no setup — the shield passes straight through the arena's side
walls and dissolves into them instead of cutting a diagonal line across them. And the **light SSBO
being bound globally** means a custom material can light itself from the scene's real lights with
no per-shader plumbing at all; the block declaration copies verbatim out of `shaders/custom.frag`.

**Hot reload, and an honest note about it.** `ApplicationShip::ReloadVolumeShader` is the pattern
and it transfers directly — build a new `Shader`, and if it compiled, assign it at the index the
mesh already points at, never `AddCustomShader` again. I wired it to F5 and an ImGui button early,
on the brief's advice, and then barely used it: I was changing C++ alongside the shader for most of
the build, so a rebuild was happening anyway.

Where it would have paid, and where I noticed the gap, is the one iteration that was shader-only —
tuning the save flare, below. A human can press F5; the caller doing the tuning here could not
reach the keyboard, and had to rebuild and relaunch and replay the game back to the state worth
looking at, about 40 seconds a try. So I added `breakout_reload_shader` as an MCP tool, which is
the same flag the key raises, and the loop became an edit and a call. **If this pass gets one more
piece of ergonomics, a core `shader_reload` tool is the one** — every app that registers a custom
shader wants it and none of them can have it without writing it themselves.

The key, the button and the tool all only *raise a flag* that `PreRender` acts on, which is not
fussiness: `UpdateView` runs on the physics thread and may not touch the GL context at all, so the
key physically cannot do the reload itself.

### What it made hard, and what I gave up

**The uniform API is the binding constraint on the effect's design, not the GPU.** `core/Shader.h`
offers exactly five setters: `Setint`, `Setfloat`, `Setvec3`, `Setmat3`, `Setmat4`. There is no
`Setvec2`, no `Setvec4`, and no array form. So:

- the quad's rectangle — four numbers — travels as two `vec3`s with a wasted component each;
- three ripples travel as `ripple0`, `ripple1`, `ripple2` rather than as `uniform vec3 ripples[3]`,
  with a `#define BREAKOUT_MAX_RIPPLES 3` on the C++ side and three hand-written `Setvec3` calls
  that have to be kept in step with three hand-written uniform declarations.

Three was not a design decision. Three is how many I was willing to type twice. The effect was
**designed around the limit**, which is the sort of thing worth saying out loud: had `Setvec4` and
an array setter existed, the ripples would have been a ring buffer of eight and would have carried
per-impact intensity as the fourth component.

**Giving up shadows and picking cost nothing here, and that was luck.** A `MESH_MODE_SHADER` mesh
is deliberately absent from the deferred pass and from the depth passes, so it writes no object
id, contributes nothing to the G-buffer and casts no shadow. For a flat force field none of that
matters. For the brick-dissolve effect I sketched second, all three would have mattered a great
deal — a brick that stopped casting a shadow the moment it started dissolving would have looked
far worse than a brick that simply vanished. That is the reason the second effect was cut, and it
is worth knowing before choosing what to put in this pass: **a custom material can only be
something that was never going to be solid.**

### The shader's own story

Two things cost real time.

**The position buffer's clear value is a legal world position.** The obvious soft-intersection
fade is "sample `gbuffer_position`, compare its distance to the camera against this fragment's,
fade where they are close" — and that version dissolves the shield against the empty sky.
`Renderer::DeferredPass` clears the position attachment to `(1,0,0,0)` (`core/Renderer.cpp:418`),
a perfectly reasonable point about a metre from the origin, so an unwritten texel does not read as
"nothing here" — it reads as geometry very close to the camera.

I did not ship that bug, because I read `DeferredPass` looking for what the G-buffer is cleared to
before writing the fade rather than after. That is worth being honest about: it cost ten minutes of
reading rather than an afternoon of debugging, and only because the brief's instruction to read the
engine's own source first had already been followed. Somebody reaching for `gbuffer_position` in
the obvious order will lose the afternoon. The depth buffer is the channel that can answer the
question, because its clear value of 1.0 is outside the range anything real occupies: sample depth
to find out *whether* there is geometry, then position to find out *where*.

**The flare drowned the thing it was announcing.** The save flash started as a flat `1.6 *
shield_flare` decaying linearly over 45 ticks, which whited out the entire pane for most of a
second — burying the impact ring, the lattice and the charge colour all at once, so the one frame
with the most to say said nothing. Squaring the decay makes it a punch rather than a wash and
dropping the coefficient to 0.65 leaves headroom for the ring, which is the part that actually
tells the player *where* the ball struck. Found by screenshotting a save frame-by-frame with
`sim_pause` + `breakout_step`, which is the only way to look at an effect that lasts 45 ticks.

**`mat_worldcam` must be live, not merely declared.** The brief warned about this and the warning
was correct and worth having: `CustomShaderPass` calls `Setmat4("mat_worldcam", ...)` on every
registered custom shader every frame, and `Shader::Setmat4` on a missing uniform goes through
`debug->Fatal` → `exit(1)`. GLSL strips a declared-but-unused uniform, so a custom shader with its
own vertex stage that happened not to use the camera matrix would kill the process on the first
frame with a message on stderr and no window. Reusing `shaders/default.vert` satisfies it for
free. This is the single sharpest edge in the custom-shader pass and it is only documented in a
comment inside `Renderer::UploadCloudShadow`, about a different function.

---

## 3. What the engine did well

This is the section agents skip, and on this engine it should not be.

**`core/Primitives.h` is the reason this app has no asset files.** The entire game is three
meshes: one unit cube, one sphere, one quad. Every brick, wall, paddle, debris chunk and capsule
is that cube with a scale and a material slot — which also means the renderer emits **one
instanced draw call** for all of them. The conventions being identical across all five generators
(centred, full extents, `+Y` the axis of revolution, wound CCW from outside) meant the matching
collider was `AddBoxCollider(scale * 0.5f, ...)` every single time with nothing to remember.

**`core/TextMesh.h` is the right shape.** Text as geometry, lit and shadowed like anything else,
appearing in an `include_ui:false` screenshot. The `reuse` parameter is the detail that matters:
`Mesh` has no destructor, so without it a score label would leak a VBO and a VAO per point scored,
and the header says so at the point you would otherwise get it wrong. `MeasureText` being
explicitly safe on any thread, so the simulation can decide *where* a label goes on the tick the
score changes while the frame does the building, is exactly the right split.

**The `RunSimulationTick` / `UpdateView` split worked perfectly and invisibly.** I did not write a
single pause-related bug. Gameplay went in one, chrome in the other, the test in the header
("if running it twice for one tick would change the outcome, it is simulation") answered every
question I had, and pausing and single-stepping then worked on the first try — including the
shader, because its animation clock is a tick count.

**`InputController::HoldAxis` is what makes this game measurable.** Every measurement in this
document came out of a program driving the game over MCP, and the steering half of that is one
call: hold an analog axis at a value for a duration counted in simulation ticks. The
design note on `SyntheticHold` — that it emits nothing but ordinary events, so neither the
simulation nor a recording can tell it from a thumb — is not a small thing: it means the bot is
genuinely playing the game rather than driving a test harness that resembles it.

**`Object::AddPhysics` stamping `setUserData(this)`** means the contact callback is a cast and
nothing else. `onContact` is 23 lines here and more than half of them are the comment explaining
why it stages intent instead of acting on it.

**The SimCommand queue has exactly the right shape for a restart.** The seed travels *in* the
command, so the UI button, the MCP tool and a future replay all reach the same handler on the
physics thread at the top of a tick, and the "never wait from the UI, always wait from MCP" rule
is stated on the declarations where you need it.

**The kinematic-velocity pattern generalises.** The long note on `Scene::MoveObjectOverTicks`
explains why a physics-driven motion sets a velocity rather than teleporting. I did not use
`MoveObjectOverTicks` — the paddle's position is the rules' — but I used its *reasoning* directly,
and the paddle lands exactly on target every tick with no visual lag while still shoving capsules
around properly. That note taught me the thing; the function was not even needed.

**The occluder field gave the ball a shadow-casting point light for two lines.** `EnableFieldShadows`
plus a `PointLight` that follows the lowest ball, and the ball now lights the bricks it is about to
hit and throws their shadows down the back panel. It is the best-looking thing in the game and it
cost nothing. Backlog item 39 (a column is one slab, so a ball in front of a brick merges with it)
never bit, because in this app the ball sits at the same z as the bricks — which is luck rather
than design, and worth saying so.

**The headers are the documentation and they are usually right.** `core/Object.h` on why there is
no `user_data`, `core/SimCommand.h` on why a command is data and a handler is code, `core/Scene.h`
on what `AtTickBoundary` actually buys you — I read the header before guessing every time and was
never once sent wrong by one. Everything in sections 4 and 7 below is something a header did not
*say*, not something it said incorrectly. On a codebase this size that is unusual and worth
stating plainly.

---

## 4. Missing features, ranked by what they cost me

### 4.1 `Shader` has no `Setvec2`, `Setvec4`, `Setbool` or array setters — worst

Covered in section 2. This is the one that visibly changed a deliverable: the shield's ripple
system is three hand-unrolled uniforms because three is how many I was willing to type twice.

What it should have looked like:

```cpp
void Setvec2(const char* name, const vec2& value);
void Setvec4(const char* name, const vec4& value);
//...and the array forms, which are the same glProgramUniform*v call with a count:
void Setvec4v(const char* name, const vec4* values, int count);
void Setfloatv(const char* name, const float* values, int count);
```

Each is four lines and the same `glProgramUniform4fv` the existing ones already use. Note also the
**inconsistent failure modes** among the five that do exist: `Setmat4`/`Setmat3` call
`debug->Fatal` on a missing uniform (`core/Shader.cpp:352,361`), `Setint` logs an error,
`Setfloat`/`Setvec3` log a warning. Three behaviours for one mistake, and the fatal one is on the
setter the renderer calls on your shader without asking.

### 4.2 No per-object "does not cast a shadow"

Text is geometry, so a label casts a real shadow. A label two units clear of the panel behind it
throws a crisp, perfectly legible second copy of itself onto that panel — see the first screenshot
of any version of this app before I moved them. It reads as a rendering fault, not as a shadow.

There is no way to say "this object is not an occluder". `Light::f_casts_shadow` turns a whole
light off; `Object` has no say in whether it appears in a depth pass. The workaround was to press
every label up against the back panel (`TEXT_Z = BACK_Z + 0.40f`), which collapses the offset to a
few pixels — but it also pins the labels to a plane, and the "GAME OVER" banner would genuinely
have looked better floating in front of the arena.

What it should look like: a `bool f_casts_shadow = true;` on `Object`, tested in
`Renderer::RenderSingleDepthPass` next to the mesh-mode test that is already there. The field
would also serve the field-shadow pass. Cheap, and it is the kind of flag that every app past the
first one needs.

### 4.3 `Physics` does not expose the axis locks, and a 2D game in a 3D solver needs them

The second-longest debugging session of the build, after 5.1 — and unlike that one it took
instrumentation rather than insight, because everything looked right.

A power-up capsule is a dynamic body in a game that is entirely flat. It was spawned with a small
push toward the camera so it read as a falling object rather than a sliding decal. Over the second
and a half it takes to fall, 0.8 units/second of that drifted it more than a unit out of z — past
the far face of the paddle's collider — so it sailed through a paddle sitting directly underneath
it and no contact was ever generated. The bot reported `power-ups caught: 0` with no other symptom.
It took printing the capsule's position against the paddle's every few ticks to see that the two
agreed perfectly in x and y:

```
t=7989   capsule shield  at 5.21, 5.20   paddle=5.18 half=1.80 caught=0
t=7994   capsule shield  at 5.22, 3.76   paddle=5.20 half=1.80 caught=0
t=7999   capsule shield  at 5.23, 2.26   paddle=5.21 half=1.80 caught=0     <- straight through
```

...which is what finally pointed at the axis nobody was printing.

reactphysics3d has exactly the right tool — `RigidBody::setLinearLockAxisFactor` and
`setAngularLockAxisFactor` (`3rdparty/reactphysics3d/body/RigidBody.h:163-172`) — and the `Physics`
wrapper does not mention it. Reaching through `object->GetRigidBody()` works and the brief says
that is the sanctioned escape hatch, so the cost was finding out it existed rather than using it.

Four one-line forwards would cover it, and they are the four that a flat game needs first:

```cpp
void SetLinearLockAxis(const vec3& factor);     //(1,1,0) pins a body to the XY plane
void SetAngularLockAxis(const vec3& factor);
void SetLinearDamping(float damping);           //see 4.8
void SetAngularDamping(float damping);
```

### 4.4 A scalar axis has to be declared by mapping a fake key to it, and nothing says so

`InputController::GetAxis` and the `INPUT_EVENT_AXIS_SCALAR` applier both look the action up in
`keymap`, which only `AddKeyMap` ever writes. `AddGamePadMap` fills a *separate* `gamepad_map` and
creates no `KeyState` at all. So this:

```cpp
input->AddGamePadMap(0,INPUT_BREAKOUT_STEER);   //looks complete, is not
```

leaves `GetAxis` returning 0 forever and `HoldAxis` events silently dropped on the floor, with no
log line anywhere. The fix is one more call with a system keycode of 0:

```cpp
input->AddKeyMap(0,INPUT_BREAKOUT_STEER);       //"no physical key drives this"
```

Core's own `INPUT_MOUSE_*` axes are registered exactly like that (`core/InputController.cpp:7-11`)
and `ApplicationTank` does it for its five vehicle axes, so the idiom exists — it is just not
written down, and the failure mode is total silence. Worth a line in `InputController.h` at
minimum; better, `AddGamePadMap` could create the `KeyState` itself.

### 4.5 The gamepad and a scripted axis arrive in two different places

Related but distinct. A real stick is polled into `InputController::analog_values` and read with
`GetNormalizedAnalogValue`; a scripted axis arrives as an event and lands in `KeyState::fvalue`,
read with `GetAxis`. The two never meet, so an app that wants both has to add them:

```cpp
axis += input->GetNormalizedAnalogValue(INPUT_BREAKOUT_STEER);
axis += input->GetAxis(INPUT_BREAKOUT_STEER);
```

which also means a stick and a script can fight, and that a recording of a run driven by a real
gamepad would not contain the stick's motion at all — the polled path never becomes an event. That
last part matters for record/replay (backlog item 25) and is worth knowing before that work
starts. `PollGamepad` submitting `INPUT_EVENT_AXIS_SCALAR` for each mapped analog, rather than
writing `analog_values` for a second reader, would close both halves.

### 4.6 A rules layer cannot use `RRandom`

`core/RRandom.h` includes `Texture.h`, which includes `glad.h`. So a translation unit that wants a
seeded integer and has deliberately no engine types in it pulls in the entire OpenGL loader.
`breakout/Field.h` therefore carries its own nine-line xorshift32, and `tetris/Playfield.h` carries
the same nine lines for a related reason — that is two apps now.

The recommended shape in `RRandom.h` ("one instance per game, created once in `Init()`") is also
not reachable from a rules layer that is meant to be a pure function of state and input, because
`Application::rrand` is a single stream shared with anything the UI or an MCP thread draws from
(the residual note under backlog item 22). I ended up drawing the power-up kinds from the rules'
own generator inside the tick, which is correct, but it means the engine's reproducible generator
is unused by the one part of the app that most needs reproducibility.

Splitting `RRandom` into a header-only PRNG core with no `Texture` dependency, plus the existing
texture-backed shell, would fix this for every future rules layer. It is a file move.

### 4.7 The rules cannot ask what the tick rate is

Everything in `breakout/Field.h` is denominated in ticks, per the house rule — but a ball sweeping
across a field has a *continuous* position, so unlike a grid game it needs the timestep. The rules
have no engine types, so they cannot call `Scene::GetPhysicsTimestep()`. The number is a `#define`
in `Field.h` and the app must call `SetPhysicsTPS` with the same value by hand. A `static_assert`
cannot check it either, because one side is a `#define` and the other is a runtime call.

This is a genuine tension in the "rules know nothing about the engine" split rather than a bug,
and I do not think there is a clean answer beyond passing `dt` into `Tick()`. Recording it because
the split is otherwise the best structural advice in the repo and this is the one place it chafes.

### 4.8 `AddBoxCollider` and `AddCapsuleCollider` set damping and friction behind your back

The brief flagged this and it is exactly as described (`core/physics/Physics.cpp:175-176`, and
again at `:237`): linear and angular damping to 0.5 and friction to 1.0, with no setter anywhere on
`Physics` to undo the damping. `AddSphereCollider` sets neither, so two colliders on one body can
disagree about whether the body is damped depending on which was added last.

I did not fight it — the debris and capsules both want damping — but it is worth noting that the
*asymmetry* is the part that will bite: a shape swap from box to sphere silently changes a body's
motion, and nothing logs it.

### 4.9 No way to express "unlit" or "emissive only"

With `f_render_skybox = false` there is no environment to reflect, so a high `metallic` value gives
up its diffuse term and gets nothing back. `metallic = 0.92` on the tough bricks rendered them
**almost black**; they read correctly at 0.45 with a little emissive. That is not wrong — it is
what metallic means — but the failure looks like a material that did not load, and there is nothing
in `core/Material.h` that warns a scene with no reflections cannot afford metal.

---

## 5. Bugs found

Checked against `docs/engine_backlog.md` first. Items 15, 22 (residual), 23 and 39 came up and are
already known; they appear above as costs rather than here as finds.

### 5.1 `LoadFile()` hands out a buffer the cache owns — NEW, confirmed, fixed

**The worst thing in this report.** `core/File.cpp:21`.

`LoadFile` has two different ownership contracts depending on whether the file has been read
before, and nothing says so:

- **first read** — `calloc`s a buffer, hands a *copy* of it to `BinaryAsset::StoreBinaryAsset`
  (which allocates its own with `new[]`), and returns the caller's buffer. Caller owns it.
- **any later read** — returns `memory_asset->data`, which is the cache's own `new[]` allocation,
  kept for the life of the process. Caller does **not** own it.

Two callers free what they are given: `WaveFile::~WaveFile` (`core/WaveFile.cpp:10-14`) and
`Texture::~Texture` (`core/Texture.cpp:13-19`, plus `:160` and `:321`). So a second load of any one
file is a mismatched `free()` on a `new[]` pointer **and** leaves the cache holding a dangling
pointer; a third load then reads freed memory and frees it again.

**Reproduction**, which is how I found it — not by reading:

```cpp
soundsystem->AppendFile("data/sound/bleep.wav","brick_a");
soundsystem->AppendFile("data/sound/bleep.wav","brick_b");
soundsystem->AppendFile("data/sound/bleep.wav","brick_c");   // process dies here
```

Expected: three handles onto one sound, which is the sanctioned way to let a sound overlap itself
(`core/SoundSystem.h`, `NUM_AL_BUFFERS`). Actual: the process exits during the third call with no
message of any kind. The stderr log simply stops after the second `WaveFile: Loaded` line, which
is about as unhelpful as a failure gets — it looks like a sound-system problem and it is not.

**`APP=Tetris` has this bug today.** `ApplicationTetris::Init` registers `click.wav` twice and
`bleep.wav` twice, so it performs two mismatched frees and corrupts its heap at startup on every
single run. It survives because two registrations is one double-free each and the freed block
happens still to be readable. Three is where it stops surviving.

**Fixed** — see section 6.

### 5.2 `BinaryAsset::GetBinaryAsset` returns a pointer into a growing vector — NEW, inferred from reading

`core/BinaryAsset.cpp:68` returns `&asset`, a pointer to an element of
`static std::vector<BinaryAsset> file_assets`, and `StoreBinaryAsset` `push_back`s onto that same
vector. Any store after a get can reallocate the vector and leave the earlier pointer dangling.

In practice `LoadFile` uses the returned pointer immediately and does not store it, so the window
is one function call and I have not seen it fire. Inferred from reading, not observed. It is a
`std::deque`, a `std::list`, or a vector of `BinaryAsset*` away from being impossible.

### 5.3 `Physics::SetTrigger` / `IsTrigger` dereference a collider that may not exist — NEW, inferred from reading

`core/physics/Physics.cpp:118` and `:122` both do `body->last_collider->...` with no null check,
unlike every other method in that file, which guards on `body && body->rigidbody`. `last_collider`
is only ever set by an `Add*Collider` call, so `SetTrigger` on a body that has none — which
`AddPhysics` produces, since it creates the body and leaves colliders to the caller — dereferences
null. Inferred from reading; I did not use triggers, so I did not fire it.

### 5.4 The deferred position buffer's clear value is a legal world position — NEW, inferred from reading

`core/Renderer.cpp:418` clears the position attachment to `(1,0,0,0)`. A custom shader sampling
`gbuffer_position` to find out how far away the scene is therefore cannot distinguish "nothing was
drawn here" from "geometry one metre from the origin", and any soft-intersection or depth-fade
effect would dissolve against the empty sky.

Read out of `DeferredPass` before writing the shield's fade rather than discovered by writing the
fade first, so I have not seen it fire — but it is arithmetic rather than speculation, and the
obvious implementation of the obvious effect walks straight into it.

Not really a defect — a colour buffer has to be cleared to *something* — but it is a trap that
every future user of this pass will hit, and the fix is free: clear it to a position no scene will
ever occupy, or say in `Renderer.h` next to the `TEXUNIT_GBUFFER_*` defines that **depth is the
channel that answers "is there anything here", and position is only meaningful once it has**.

## 6. Changes to `core/`

One change. It alters behaviour for every app, so it is called out loudly here as the ground rules
require.

### 6.1 `core/File.cpp` — `LoadFile` now returns a buffer the caller owns, always

**What changed.** On a cache hit, `LoadFile` returns a fresh `calloc`'d copy of the cached bytes
instead of the `BinaryAsset`'s own `new[]` pointer. `sz + 1` zeroed, so a cached re-fetch is
null-terminated exactly like a fresh disk read — callers that treat the result as a C string (GLSL
source) depend on that, which is why `StoreBinaryAsset` already keeps a terminator of its own.
`core/File.h` gained a documentation block stating the contract: **the caller owns the buffer and
must `free()` it.**

**Why.** Section 5.1. It is heap corruption with no diagnostic, it is already live in `APP=Tetris`,
and it blocked a feature the engine's own sound system documents as the way to do things.

**Why this fix and not a smaller one.** Three were considered:

1. *Stop `WaveFile` freeing.* One file, no new allocation — but it leaks the first-load buffer, and
   leaves the identical bug in `Texture` for whoever loads an image twice.
2. *Make the cache hand out its own pointer on the first read too, so there is one buffer with one
   owner, and stop both callers freeing.* Genuinely the cleanest design, and no leak at all. It
   touches three files, inverts the contract every call site currently assumes, and would silently
   turn any future `free()` into a double free rather than a leak. Too sharp a change to make while
   passing through.
3. *What was done.* One function. It makes the contract that every call site **already assumes**
   actually true, and it can only ever reduce undefined behaviour.

**What it costs.** One `memcpy` per cached load, and the six callers that never freed what they
were handed (`Shader`, `GLTFLoader`, `OBJLoader`, `Window`, `HTTPServer`, `Application`) now leak a
copy per *cached* load as well as the first-load buffer they already leaked. Measured across a
whole run of every app: well under a megabyte, dominated by `shaders/default.frag` at 25 KB per
`Shader` construction. Bounded memory beats heap corruption, and a hot shader reload is the only
thing that makes it grow at all.

**What it would have cost not to make it.** The overlapping brick sounds — the one thing
`SoundSystem`'s own header says to do about a source not being able to overlap itself. And Tetris
would still be corrupting its heap at startup, undiagnosed.

**Verification.** All twelve apps rebuilt clean. `APP=Tetris` was run afterwards, queried through
`tetris_state` (falling piece, tick advancing, correct board) and screenshotted — well, piece,
ghost, all three previews and all five text labels render correctly. `APP=Breakout` registers three
handles onto one WAV and no longer dies.

---

## 7. Friction and papercuts

Small individually; the pattern across them is the finding.

- **Nothing tells you a scalar axis needs a key mapping.** Covered in 4.4, but it belongs here too
  because the failure is *silence* — no warning, no log line, the axis just reads zero forever.
  Every silent-zero API in an input system costs someone an afternoon.

- **`AddCustomShader` returning an index you must remember to put on the mesh** is fine, but
  `Mesh::custom_shader_index` defaulting to 0 means an app that registers a *second* custom shader
  and forgets to tag its mesh gets the *first* shader silently, which is a much worse outcome than
  drawing nothing. A default of -1 meaning "not assigned" would fail loudly.

- **The three uniform-setter failure modes** (fatal / error / warning) for one mistake. If the
  fatal one is deliberate — and given `CustomShaderPass` calls it unconditionally it nearly has to
  be — then they should all be fatal, or none should.

- **`Renderer::viewport_y` measured from the bottom while `Camera::viewport.px_offset_y` is
  measured from the top** is documented at length in both places, and the documentation is
  excellent, and I still had to read both comments twice. This is the right call given one number
  talks to GL and the other talks to the mouse; it is just inherently costly.

- **`Object::SetCollisionCategoryBits` walks the body's existing colliders** (`core/Object.cpp:943`),
  so calling it before `Add*Collider` does nothing at all, silently. The ordering is not stated on
  the declaration in `core/Object.h`. I noticed it from the implementation rather than by getting it
  wrong, but the failure mode if you did — a body that ignores a filter you can see set on it in the
  Inspector — would be a bad afternoon.

- **A mouse delta accumulated across a pause arrives as one jump.** `InputController::Tick` only
  clears a delta if something called `GetDelta` and set `f_processed`, and a paused simulation has
  nothing calling it — so the movement piles up and the paddle teleports on the tick after the
  resume. Inferred from `InputController.cpp:687-697` rather than observed — I clamped it per tick
  in the app (`MOUSE_MAX_PER_TICK`) as soon as I read that, so I never let it happen. Arguably
  correct behaviour either way, but it would not look correct.

- **`debug->Fatal` calling `exit(1)` from a render thread** means a shader typo produces a window
  that never appears and a log you have to go and find. It is documented; it is still the thing
  most likely to make a newcomer think the build is broken.

- **`ApplicationBreakout.cpp` is 2,077 lines, of which 480 — a quarter of the non-blank lines —
  are comment.** That is the house style and I believe in it, but it is worth noting *why* it was
  possible to comment this densely: the engine's own headers explain themselves, so there was a
  "why" to inherit at nearly every call site rather than one to invent. That is a compliment to the
  codebase, not to me.

---

## 8. The design decisions, and why

### 8.1 The ball is not a rigid body — and here is the measurement

**Option C, the hybrid.** The ball is swept by hand in `breakout/Field.cpp`; reactphysics3d owns
the debris, the power-up capsules and the paddle.

**The reason is not tunnelling.** It is that the single most important parameter in this genre is
the angle the ball leaves the paddle at, and in every game in the genre since 1976 that angle is a
**designed response to where on the paddle it struck**, not a physical one — the incoming direction
is discarded entirely. A solver reflects about the contact normal and cannot express it. Everything
else about a paddle game (perfectly elastic bounces, a speed that never decays, an angle clamped
away from horizontal) is likewise a rule rather than a physical consequence.

`tools/breakout_bot.py angles` measures whether the rules deliver what they promise, by playing a
real rally and reading back what the game recorded for each bounce:

```
  offset   measured   expected    delta    speed
--------------------------------------------------
  -0.777     -49.85     -49.86     0.01    17.10
  -0.681     -43.70     -43.73     0.03    16.30
  -0.532     -34.17     -34.17     0.00    15.80
  -0.345     -22.12     -22.17     0.05    16.90
  -0.204     -13.08     -13.08     0.00    16.20
  -0.040      -6.89      -2.57    -4.32    16.80
   0.026       6.89       1.65     5.24    15.70
   0.046       6.89       2.95     3.94    16.10
   0.257      16.62      16.51     0.11    16.70
   0.332      21.31      21.31    -0.00    15.90
   0.555      35.64      35.64     0.00    17.30
   0.744      47.76      47.76     0.00    15.30
```

`expected` is `offset * BREAKOUT_PADDLE_MAX_DEFLECT`. Every sample matches to within 0.05°, and the
three that do not are all pinned at exactly ±6.89° = `asin(BREAKOUT_MIN_UX)` — the floor that stops
a dead-centre hit going vertical and bouncing between two points forever. So the deviation is
entirely explained by two deliberate mechanisms, which is what "designed" is supposed to mean.

(A later run that happened to take no near-centre hits came back with a worst deviation of 0.04°
across ten bounces. The table above is the more useful one precisely because it caught the clamp
firing.)

**And now the tunnelling, measured rather than assumed.** `tools/breakout_bot.py probe` pins the
ball's speed and runs it into the wall, counting escapes (a ball found outside the arena — a
collision the sweep missed entirely), bricks broken (that it is still *hitting* things rather than
merely staying in the box) and resolution overruns (ticks where it hit the six-impacts-per-tick cap
and the rest of its travel was dropped):

```
   speed   u/tick   ticks  escapes  bricks  overruns  verdict
------------------------------------------------------------------------
      15     0.25    1800        0      12         0  clean
      30     0.50    1800        0      24         0  clean
      60     1.00    1800        0      36         0  clean
     120     2.00    1800        0      55         0  clean
     240     4.00    1800        0      63         0  clean
     480     8.00    1800        0      65         1  clean, 1 capped ticks
     960    16.00    1800        0      76         1  clean, 1 capped ticks
    1800    30.00    1800        0      76         1  clean, 1 capped ticks
```

Bricks are 2.0 × 1.0 world units and the ball's radius is 0.4. **Zero escapes at 30 units per tick
— thirty times a brick's height in a single step** — which is a hundred and twenty times the speed
the game actually plays at. The swept-slab test simply does not care how fast the ball goes. The
only artifact at all is one tick in 1800 at the extreme end where six impacts were not enough and a
fraction of a step was dropped, which is a stall rather than a leak and is invisible.

**The honest counter-argument, which the brief asked for.** I did not build the rp3d version and
race it. I decided against it on the design argument above — the bounce angle — rather than on the
physics, and the measurement above is a check on what I built rather than a comparison against what
I did not. What I can say from measurement is that the hand-written sweep has no speed limit worth
worrying about, costs about 90 lines, and is exactly reproducible; and from the rest of the build,
that the solver was genuinely better at the three jobs I did give it.

**Was the solver useful?** Yes, and specifically:

- **Debris.** A destroyed brick bursts into four dynamic chunks that tumble down the arena and pile
  up against the bricks still standing, because the bricks carry real static colliders. Free, and
  it is most of what makes the game feel like it has weight.
- **Power-up capsules.** These are the piece where the solver is doing *gameplay* work rather than
  garnish. A capsule's path down is genuinely the solver's; the catch is a real contact between it
  and the paddle's kinematic body, resolved in `onContact`. Where it lands is not predictable from
  the tick that dropped it, and that is the point.
- **The paddle.** Kinematic, driven by velocity, so it shoves both of the above properly.

The thing I would have missed by turning physics off entirely is the *capsules*, and they are the
best gameplay idea in the app.

### 8.2 Perspective and tilted, not orthographic and flat-on

Tetris looks straight down `-Z` with an orthographic camera and is right to — a grid of cubes wants
no foreshortening. A paddle game has no such constraint and gains a lot from breaking it: from
slightly below and in front, the bricks have visible sides, the arena walls have thickness, and the
ball's point light throws its shadow *along* the wall rather than flat behind it. The cost is that
world units and screen pixels stop being proportional, which matters only for picking, and nothing
in this game picks.

The one thing this cost, and it is worth knowing: **a flat game has an entire axis nothing is
watching.** The capsule that drifted out of the play plane and through the paddle (section 4.3) was
invisible from the front at any camera angle, because the drift is along the view direction — and
every readout I had, the ASCII wall included, was two-dimensional. On an engine where a 2D game is
built out of 3D objects, that axis is where the bugs go to hide, and it is worth putting *something*
on screen or in telemetry that would show it.

### 8.3 Collision filtering, because a capsule resting on a brick is worse than no physics

The first working version had capsules colliding with everything. A capsule dropped from a brick
in the middle of the wall landed on the brick below it, the solver put it to sleep, and a gameplay
object the player was meant to chase sat motionless in the middle of the wall for 900 ticks until
its reap timer ran out. Measured, printed, and unmistakable:

```
t=9478   capsule multiball    at   8.95, 20.50   paddle=8.38
t=9553   capsule multiball    at   9.02, 20.22   paddle=9.02
t=10108  capsule multiball    at   9.02, 20.22   paddle=9.02
```

Capsules are now filtered down to the two things they may touch: the arena that keeps them in the
world, and the paddle that catches them. Debris still collides with everything, because debris
piling up against the wall is the reason the bricks carry colliders at all.

### 8.4 One object per brick cell, created once, hidden when dead

Copied from Tetris and it removes the same class of bug. 88 hidden cubes cost nothing, the view is
rebuilt from the rules array every tick so there is no incremental update to get wrong, and a
brick's rigid body is created once at a moment when nothing is iterating the physics world — rather
than 88 times a level while a ball is in flight. A dead brick's collider is switched off with
`Physics::SetActive(false)` so debris does not bounce off a gap.

### 8.5 The shield is a gameplay resource so the shader has to be right

This was deliberate and I would do it again. The brief asks for an effect driven by live simulation
state; the surest way to guarantee that is to make the thing the shader draws something the player
is *managing*. Because the charge is spent on saves and refilled by breaking bricks, a shader that
lied about it would be a gameplay bug, not a cosmetic one — which is a much better forcing function
than good intentions.

### 8.6 Determinism, and where it stops

`tools/breakout_bot.py determinism` runs the same seed and the same scripted input twice. Score,
bricks remaining, shield charge to four decimal places, saves, power-ups caught and the whole board
match exactly every time, and the ball's velocity is bit-identical.

The ball's **position** matches on some runs and is a couple of ticks of travel out on others, and
that flappiness is the interesting result rather than a nuisance: it is exactly what record/replay
(backlog item 25) exists to fix. An MCP-driven input is not tick-aligned. `HoldAxis` starts on
whichever tick the physics thread happens to be on when the call lands, so two runs of the same
script can begin their holds one or two ticks apart — and the same script then produces the same
trajectory sampled at a different point along it.

So the *simulation* is reproducible and the *harness* is not, and no amount of seeding fixes that;
a tick-stamped input log is the only thing that will. Worth knowing before item 25 is started,
because it means the recording has to capture the tick an input landed on, not merely the input.

---

## 9. What I would build next

**In the game, with another day:**

- **A second custom shader on the bricks** — the dissolve I cut. It needs 4.2 (a per-object shadow
  flag) to not look worse than what it replaces, so it is really an engine ask wearing a game hat.
- **The ball's trail.** The point light already follows it; a handful of fading quads in the custom
  pass behind it would cost almost nothing and is the cheapest remaining win.
- **Brick rows that fall** when everything under them is gone. The bricks are already rigid bodies
  with colliders; switching one from static to dynamic is one call, and it would put the solver in
  the *core* loop rather than beside it. The interesting question is what a falling brick does to
  the swept ball, which is currently resolving against a static grid.
- **Two-player, one keyboard.** The input system already counts held mappings per action and the
  rules already support several balls.

**In the engine, in the order I would do them:**

1. **`Setvec2`/`Setvec4`/array uniform setters** (4.1). Twenty lines, unblocks every future custom
   shader, and it is the only missing feature that visibly changed what I shipped.
2. **A per-object `f_casts_shadow`** (4.2). One field and one test, and world-space text is
   basically unusable in front of a wall without it.
3. **The four `Physics` forwards** (4.3) — the axis locks especially. Any 2D game on this engine
   needs `setLinearLockAxisFactor` on its first day and will not find it.
4. **Make `AddGamePadMap` create the `KeyState`** (4.4), or fail loudly when there is not one.
   Silent zero is the worst possible failure mode for an input API.
5. **Split `RRandom`'s PRNG out of its `Texture` dependency** (4.6), so a rules layer can use the
   engine's reproducible generator instead of writing its own for the third time.
6. **`InputController::IsInputLive()`** — backlog item 15, still open, and this app is the fourth
   hand-written copy of that predicate.
7. **Close `Texture`'s half of the `LoadFile` bug properly** by deciding the ownership question
   once (option 2 in section 6.1) rather than leaving my copy-on-cache-hit as the answer forever.

---

## Appendix: running it

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
mingw32-make.exe APP=Breakout -j8
./wind.exe 2>wind_stderr.log &
```

Controls: mouse, `A`/`D`, arrow keys or the left stick to steer; `space` launches; `P` pauses;
`R` restarts; `M` toggles mouse control; `F5` reloads the shield shader; `F1` shows the engine
panels.

MCP tools on `http://127.0.0.1:8765/mcp` — `breakout_state`, `breakout_steer`, `breakout_launch`,
`breakout_step`, `breakout_restart`, `breakout_ball_probe` and `breakout_reload_shader`, alongside
the core `sim_pause`, `sim_step` and `screenshot`.

```bash
python tools/breakout_bot.py play --seed 7          # watch it play
python tools/breakout_bot.py probe                  # the tunnelling measurement
python tools/breakout_bot.py angles                 # the designed-bounce check
python tools/breakout_bot.py capsules               # the contact-callback path
python tools/breakout_bot.py determinism            # same seed twice
```

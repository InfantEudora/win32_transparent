# Building a game on this engine: what happened

An engineering report on writing `APP=Tetris` against `win32_transparent` at commit `9465ce9`.
Task and ground rules: `docs/tetris_agent_brief.md`.

---

## 1. Summary

**There is a playable Tetris, and it is finished.** `make APP=Tetris` builds it, `./wind.exe`
runs it. 10x20 well, seven tetrominoes with full SRS rotation and wall kicks, 7-bag randomiser,
DAS/ARR auto-repeat, soft drop, hard drop, ghost piece, hold, lock delay with move-reset, line
clears with a flash-then-collapse animation, guideline scoring, the NES gravity table, levels,
game over, restart. Sound on move/rotate/hold/lock/clear/game-over. A line clear bursts the
cleared row into reactphysics3d debris that tumbles out of the well and is reaped. Every duration
in the game is a count of simulation ticks; nothing reads the wall clock.

It is playable by hand and it is playable by a program. `tools/tetris_bot.py` drives it end to
end through its own MCP tools — no window focus, no keyboard. The soak run: **250 pieces, 99
lines, level 10, 69,760 points**, ending in an honest game over once the bot's round-trip latency
stopped keeping up with 6-ticks-per-cell gravity. Zero errors or fatals in the log over the whole
run. Scripted input goes through `InputController::HoldKey`, so the simulation genuinely cannot
tell the bot from a person.

Files added:

| File | What it is |
|---|---|
| `apps/Tetris.mk` | the app's build fragment |
| `ApplicationTetris.{h,cpp}` | engine wiring: view, input translation, HUD, MCP tools, sound, debris |
| `tetris/Tetromino.{h,cpp}` | the seven pieces, four rotations each, SRS kick tables |
| `tetris/Playfield.{h,cpp}` | the rules. A plain array and tick counters, no engine types at all |
| `tools/tetris_bot.py` | plays the game through MCP over HTTP, for testing |

Three files in `core/` were changed. All three are logged in §5, and one of them is the finding I
would most want the author to read.

**The headline is good news.** A complete, deterministic, animated, sounded, physics-garnished
game took one session on this engine, and most of that was gameplay code that would have been the
same anywhere. The engine got out of the way far more often than it got in it. The list below is
long because the brief asked for a long list, not because the experience was painful.

**The one thing that genuinely cost me an hour** was finding 3.1: a scripted "player" cannot press
an edge-triggered button while the window is unfocused, because of a one-tick-off flag. It is the
most important item here, because the brief actively recommends the pattern that breaks, and
because the symptom is silence rather than an error.

---

## 2. What the engine did well

Genuinely — these were a pleasure.

**`Scene::MoveObjectOverTicks` is the best API in the engine for a game.** The row-collapse
animation is one call per cube:

```cpp
vec3 target = CellWorldPosition(x,y - drop);
main_scene->MoveObjectOverTicks(cell,&target,NULL,TETRIS_COLLAPSE_TICKS);
```

Denominated in ticks, so it is simulation time: pausing freezes it mid-slide, single-stepping
walks it frame by frame, and a replay would reproduce it exactly. The same call does the active
piece's sideways slide. In an engine where I expected to hand-roll a tween system with a list and
a per-frame update, this was already there and already correct.

**The tick discipline is real and it pays off immediately.** `GetPhysicsTick()`, `StepPhysics(n)`,
`PausePhysics`, a constant `GetPhysicsTimestep()`, `physics_time_factor` that changes the *rate*
and never the *step* — this is a simulation that has decided what time is, and everything
downstream gets easier. Writing `TETRIS_LOCK_DELAY_TICKS 30` instead of `500 /*ms*/` is not just
tidier; it is the reason the MCP `tetris_step` tool can say "48 steps at level 1 is exactly one
cell of gravity" and mean it. Most engines I would have to fight to get this.

**Pause + step + screenshot is a superb debugging triangle.** I caught the collapse animation
mid-slide (a 10-tick window) by polling for `phase == "clearing"`, pausing, stepping 2 ticks at a
time until `phase == "collapsing"`, then screenshotting. That is a thing you simply cannot do in
most engines, and it took four lines of Python.

**`SimCommand` is the right design and the comments explaining why are the best documentation in
the repo.** The command-is-data / handler-is-code split meant my restart went on the queue
without core knowing what a Tetris is, and the seed rides in `value[0]` so a replayed restart
deals the same game. I did not have to think about it once; I read `SimCommand.h` and did the
obvious thing and it worked.

**`Application::MaybeAttachScreenshot` is one line for "and show me a picture".** Every tool I
wrote got `include_screenshot` for free. For an agent working without eyes this is the single
highest-leverage API in the codebase.

**`AddPhysics` starting a body static with gravity off is the right default.** Opting in to
dynamics is one line and it means a wall is free. Twenty static colliders and forty dynamic debris
cubes cost me about fifteen lines total.

**`KeyState::f_isdown` being a count of held mappings rather than a boolean.** I mapped both
`VK_UP` and `'X'` to rotate-clockwise and both `'C'` and `VK_SHIFT` to hold, and it just worked,
including holding one and pressing the other. Most input layers get this wrong.

**The deferred pipeline with an orthographic camera makes a beautiful 2D board.** §5 of the brief
was right: one `Object` per cell pointed at by an ortho camera gives crisp blocks, real shadows on
a back panel, and a piece that glows slightly through `emissive.w`. It looks better than a sprite
Tetris would have, for less code.

**The build system.** `APP=Tetris` and three new files, no edits to shared files, no CMake, header
deps tracked. First build compiled and linked with zero errors, which for a 1,400-line app against
an unfamiliar engine is a compliment to the headers.

---

## 3. Missing features, ranked by what they cost me

### 3.1 There is no way for a scripted player to press a button while unfocused — COST: ~1 hour

This is the big one. It is both a missing feature and a bug; the bug half is §4.1.

`InputController` has everything needed for a program to play a game: `HoldKey(mapped, ticks)`
emits ordinary events, deliberately not focus-gated, and `HasSyntheticHolds()` exists precisely so
an app can write the gate the brief recommends in §6.4:

```cpp
if (!main_window->f_has_focus && !input->HasSyntheticHolds()){ return; }
```

That gate silently discards **every edge-triggered scripted action**. A hold is erased in the same
`AdvanceSyntheticHolds()` call that emits its key-up, so on the exact tick `WasKeyReleased()`
becomes true, `HasSyntheticHolds()` has already gone false and the app has stopped listening.
Level-triggered actions (a held direction) work fine. So rotate, hard drop and hold do nothing,
while left and right work — which is the worst possible failure mode, because the system looks
alive.

What it cost: my bot ran 60 pieces and placed one. Everything looked healthy — tools returned,
ticks advanced, no error anywhere — and the game just quietly refused to rotate or drop. I only
found it by noticing that 466 ticks per piece is exactly "gravity alone, no hard drop".

**What the API should have looked like:** `HasSyntheticHolds()` should cover the release tick,
which is the only tick that matters for an edge. That is what I changed (§5.1). The alternative
API — and probably the better one long-term — is for the engine to own this decision rather than
asking every app to re-derive it:

```cpp
//true when this tick's input may drive the simulation: the window has focus, OR the input came
//from a script/replay rather than from the OS.
bool InputController::IsInputLive();
```

Then no app writes the gate by hand and no app gets it wrong.

### 3.2 No hook that runs once per simulated tick — COST: ~20 minutes, and a latent bug in every app

`RunLogic()` is documented as the per-tick gameplay hook, and it is not one. It is called on every
pass of the physics thread's loop (`Application.cpp:301-326`), which keeps spinning while the
simulation is paused so a key can unpause it. The pause and the single-step counter are honoured
one function later, in `Scene::UpdatePhysics` (`Scene.cpp:144-150`).

So gameplay written the obvious way keeps playing while the simulation is paused, and while
single-stepping it advances by however many loop iterations happened rather than by the number of
ticks requested. Pause/step — the engine's best debugging facility — silently stops meaning
anything for that app.

Every app has to re-derive the predicate, and it has to match `Scene::UpdatePhysics` exactly:

```cpp
bool f_tick_will_run = !main_scene->IsPhysicsPaused() || (main_scene->GetPendingPhysicsSteps() > 0);
if (!f_tick_will_run){ return; }
```

I did not patch this, because changing when `RunLogic` is called changes behaviour for all ten
other apps and several of them (camera control, UI polling) clearly rely on running while paused.

**What the API should have looked like:** two hooks instead of one, named for what they are.

```cpp
virtual void RunLogic(void);        //every loop pass, paused or not. Camera, UI polling.
virtual void RunSimulationTick(void); //exactly once per tick that actually runs. The game.
```

`Application::PhysicsThreadFunction` already knows which case it is in; it is the only place that
can know without duplicating the predicate.

### 3.3 No read-side equivalent of `SubmitCommand` — COST: ~30 minutes, and a design decision I had to make blind

An MCP tool handler runs on its own thread holding no lock. Writing is solved —
`Scene::SubmitCommand` and `InputController::SubmitEvent` both exist and are excellent. Reading is
not solved at all. `ApplicationTank` takes the "pause the world and step it" route; that is
unacceptable for a game, because reading the score would stop the game.

I published a snapshot instead: `RunLogic` fills a `TetrisSnapshot` at the end of every tick under
a mutex of my own, and every tool serves from it. It works, it never disturbs the simulation, and
it cost me a struct, a mutex, a copy per tick and a hand-written `PublishSnapshot()` that has to
be kept in step with the game state by hand — which is exactly the kind of code that rots.

It also has a sharp edge I only noticed when the telemetry lied to me: a snapshot is only written
by a tick, and *pausing is precisely the thing that stops ticks*, so `tetris_pause` returned
`"paused": false`. I now read that one field live. Anything else in a snapshot that can change
while paused has the same problem.

**What the API should have looked like** — the mirror of the command queue:

```cpp
//Runs `fn` on the physics thread at a tick boundary and blocks until it has. The function may
//read anything; it must not block. Callable only from a thread that does NOT hold physics_mutex.
void Scene::ReadAtTickBoundary(const std::function<void()>& fn, int timeout_ms = 2000);
```

Every app that grows MCP telemetry will otherwise invent its own snapshot, differently.

### 3.4 No world-space text of any kind — COST: ~15 minutes and a feature I cut

There is no bitmap font, no text mesh, no world-space quad. One 13px ImGui font is the entire text
rendering story in the engine. So "GAME OVER" across the board, a "NEXT" label over the preview
column, a floating "+800" when a tetris lands, a level-up flourish — none of these are possible
without building a font atlas from scratch or spelling words out of cubes. I put everything in an
ImGui panel, which is the only option, and it is the wrong place for it: the HUD is chrome, and a
game's score wants to be part of the picture.

Compounding it: **MCP screenshots do not contain ImGui at all** (§4.4), so the one place text can
go is the one place an agent cannot see.

The cheapest fix that would have helped me: a `Mesh` built from a texture atlas plus a
`DrawWorldText(const char*, vec3, float size)` on `Renderer`. Given `SpriteSheet` already exists
and already loads an atlas, most of the machinery is there; it just cannot reach the world.

### 3.5 No primitive mesh generators — COST: ~5 minutes, but it is a papercut everyone pays

`data/unit_cube.obj` is the only cube in the engine. Every app that wants a box loads that file,
and an app that wants a quad, a sphere at a given tessellation, or a rounded box hand-builds
vertices (`ApplicationShip.cpp:80-125` is the worked example, and it is 45 lines to make a cube).

```cpp
Mesh* MakeBox(const vec3& half_extents);
Mesh* MakeQuad(const vec2& size);
Mesh* MakeSphere(float radius, int segments);
```

Thirty lines each, written once, and they would remove a file dependency from every single app.

### 3.6 `Object` has no user-data pointer — COST: ~10 minutes

I wanted to hang "which board cell is this" and "when does this debris expire" off the objects
themselves. `Object` has no `void* user_data` and no way to subclass cheaply (subclassing means a
new type that `AssetManager::GetObjectFromAsset` will not build for you). I kept parallel arrays
and a `std::vector<TetrisDebris>` instead, which is fine here but means every lookup from an
object back to game state is a search.

Notably, `rp3d::RigidBody` *does* get `setUserData(owning Object*)` — the engine already believes
in the idea, it just does not offer it one level up.

### 3.7 `MoveObjectOverTicks` cannot express a sequence — COST: ~10 minutes

A new motion for an object replaces the one in flight. That is the right default and the comment
says so. But it means a there-and-back (a camera shake, a piece bouncing as it locks, a preview
sliding out and back in) cannot be expressed at all: you get one leg of it. I hand-rolled the
hard-drop camera shake as a decaying square wave on the tick counter, which is eight lines I would
rather not have written.

A `MoveObjectOverTicks(..., bool f_queue)` — or an explicit `QueueObjectMotion` — would cover it.

### 3.8 The gamepad D-pad is not reachable — COST: ~5 minutes, feature dropped

`AddGamePadMap(analog_index, mapped)` maps *analog* indices. XInput delivers the D-pad as buttons
in `wButtons`, and `InputController::PollGamepad` does not surface those as mappable keycodes. So
the one control layout a gamepad Tetris actually wants — the D-pad — cannot be mapped. I mapped
the left stick's X axis and left it there.

### 3.9 No save/load of a running scene — COST: none here, noted for completeness

`BuildSceneFromJSON` restores name/position/rotation of asset-backed objects from `export.json`
and nothing else. There is no save-game and no way to serialise a scene. Tetris does not need one;
anything with progress does.

---

## 4. Bugs found

Everything in this section I confirmed myself, by measurement or by reading the code that is
quoted. Where I am reporting something the brief already listed, I say so.

### 4.1 `HasSyntheticHolds()` is false on the exact tick a scripted release is readable — CONFIRMED, FIXED

`core/InputController.cpp`, `AdvanceSyntheticHolds()`.

A `SyntheticHold` is erased from `synthetic_holds` in the same call that emits its `KEY_UP`:

```cpp
events.push_back(e);
synthetic_holds.erase(synthetic_holds.begin() + i);
```

`HasSyntheticHolds()` was `return !synthetic_holds.empty();`. `WasKeyReleased()` becomes true on
the tick that key-up is applied — which is the tick on which the hold no longer exists.

**Reproduction.** Any app that gates input on `!HasFocus() && !HasSyntheticHolds()` (the pattern
`docs/tetris_agent_brief.md` §6.4 recommends, and the reason `HasSyntheticHolds` exists). With the
window not in the foreground, call `HoldKey(SOME_ACTION, 1)` from an MCP tool and read it with
`WasKeyReleased(SOME_ACTION)` in `RunLogic`. Expected: the action fires once. Actual: it never
fires. `IsKeyDown` on the same action works, so held controls behave and tapped ones do not.

**Measured impact.** Before the fix, my bot placed 1 piece in 1,011 simulation ticks (466 ticks per
piece = free-fall gravity, no hard drop, no rotation, no sideways movement to a chosen column).
After the fix, 45 pieces for 16 lines and 3,908 points, with rotations and wall kicks landing
where the bot asked for them.

Fixed in core — see §5.1.

### 4.2 `Renderer::AddMaterial` returns the wrong index for an existing name — CONFIRMED, FIXED

`core/Renderer.cpp`. Listed in the brief §8; I confirmed it by reading the function. It skipped
the insert when the name already existed but still returned `materials.size() - 1`, i.e. the index
of the *last* material rather than of the matching one. A caller adding a material under a name
already taken silently gets a handle on somebody else's material, and the failure surfaces much
later as an object rendering in the wrong colour.

I did not hit it, because the brief warned me and I used `FindMaterialIndex(name)` everywhere.
Fixed in core anyway — see §5.2 — because the right index is the obvious contract and a wrong one
is indistinguishable from a right one at the call site.

### 4.3 `RRandom` cannot be seeded, is shared between instances, and starts from uninitialised memory — CONFIRMED

`core/RRandom.h`, `core/RRandom.cpp`. The brief says "give your piece bag its own `RRandom`
instance". That cannot work, for four separate reasons, and I read all four:

1. **`state` is uninitialised.** `RRandom.h:24` declares `uint32_t state;` with no initialiser, and
   `RRandom::RRandom()` is empty. `Get_uint8()` reads and advances `state`. A freshly constructed
   `RRandom` therefore starts at an indeterminate offset into the noise buffer — different every
   run, and under `-Og` different between builds.
2. **`SetSeed` has no effect.** `RRandom.cpp:39-43` writes the member `seed`. Nothing anywhere
   reads `seed`. The generator's position is `state`, which `SetSeed` does not touch. So the one
   function whose name promises reproducibility is a no-op.
3. **The stream is process-wide, not per instance.** `rnd_texture` is a `static` member
   (`RRandom.cpp:8`). A second `RRandom` shares the same 262,144 bytes; only the read cursor is
   per instance. So "its own instance" buys an independent cursor into a shared, unseeded buffer —
   not an independent stream.
4. **The buffer is filled from unseeded `rand()`** (`RRandom.cpp:33`), so its *contents* are at
   least stable run to run by accident, but they are not derived from anything a caller chooses.

There is also undefined behaviour in `GetInt(int,int)`: `abs(GetInt())` where `GetInt()` builds an
`int` from four random bytes, so it produces `INT_MIN` about one time in 4 billion, and `abs(INT_MIN)`
is UB.

**What I did instead:** a nine-line xorshift32 in `tetris/Playfield.h` (`TetrisRandom`), owned by
the `Playfield`, drawn from only inside `Tick()`. Verified: `tetris_restart` with seed 42 twice
gives the identical next-queue, and a seeded game replays its piece order exactly.

I did not touch `RRandom`, as instructed. But it is worth saying plainly: the deterministic-sim
direction the engine is being steered toward cannot be reached with this class in it, and the
problem is not just the shared stream the existing TODO describes — it is that the class has never
been seedable at all.

### 4.4 MCP screenshots never contain any ImGui — CONFIRMED

`Renderer::CaptureScreenshotIfRequested()` is called from inside `Renderer::DrawFrame`
(`core/Renderer.cpp:801`), right after the resolve blit. ImGui is drawn afterwards, by
`Application::DrawFrame` (`core/Application.cpp:269-274`), into the window's default framebuffer.
So the screenshot is the 3D scene and nothing else.

**Reproduction:** any app with an ImGui panel. Call the `screenshot` tool. The panel is absent.
Confirmed across every screenshot I took this session — my HUD (score, level, lines, game-over,
paused) appears in none of them.

**Why it matters more than it looks:** ImGui is the engine's *only* text rendering (§3.4). So
everything an app writes in words is invisible to the one client that cannot look at the monitor.
For an agent verifying its own work, "show me the score" is impossible.

The fix is not free — ImGui renders to the default framebuffer, not to `resolve_fbo_id` — but a
second capture path that reads the back buffer after `ImGuiRenderDrawData()` and before
`SwapWindowBuffers()` would give an honest picture of the window.

### 4.5 `SoundSystem::AppendFile` ignores the WAV's channel count — CONFIRMED

`core/SoundSystem.cpp:154-161`:

```cpp
ALenum format;
if (wav.GetNumChannels() == 1){
    format = AL_FORMAT_MONO16;
}else{
    format = AL_FORMAT_STEREO16;
}
//Load into buffer
alBufferData(buffers[buffer_index],AL_FORMAT_STEREO16,wav.wav_data,...);
```

`format` is computed and then never used — `AL_FORMAT_STEREO16` is passed unconditionally. A mono
file is uploaded as interleaved stereo, so it plays at double speed, at half its duration, with
its samples alternating between the two ears.

**Reproduction, with a file already in the repo:** `data/sound/hax.wav` is the only mono file in
`data/sound/` (1 channel, 6000 Hz, 16-bit; the other three are 2-channel 44100 Hz). Register and
play it and it comes out wrong. It is the game-over sound in my app, which is how I came to check.

One-line fix: pass `format`.

### 4.6 `SoundSystem::Play` sets the gain after starting playback — CONFIRMED

`core/SoundSystem.cpp:169-178`:

```cpp
alSourcePlay(sources[handle]);
alSourcef(sources[handle],AL_GAIN,gain);
```

The source starts at whatever gain it was left at by the previous `Play` of that handle, and the
new gain lands a moment later. With short sounds at different volumes on the same handle — a quiet
move tick and a loud line clear, say — the attack of each is played at the wrong volume. Swapping
the two lines fixes it.

### 4.7 `SoundSystem::Play` on an unknown handle plays buffer 0 — CONFIRMED by reading

`core/SoundSystem.cpp:170`, `int handle = map_handles[handle_name];`. `std::map::operator[]`
default-constructs a missing key, so a typo silently inserts `0` and plays whatever sound was
registered first, forever. Listed in the brief; I confirmed the line but did not deliberately
trigger it. Same in `Pause` (181) and `Rewind` (186). `find()` plus a `debug->Err` would turn a
baffling wrong-sound into a log line.

### 4.8 `NUM_AL_BUFFERS` is 16 and one handle is one source — CONFIRMED, worked around

`core/SoundSystem.h:28`. Sixteen distinct sounds per process, and `AppendFile` calls
`debug->Fatal` on the seventeenth. More relevant in practice: `AppendFile` allocates one source
per handle, and a source cannot overlap itself, so a sound playing twice in quick succession cuts
itself off.

**Worked around** by registering the same file under two handles: `click.wav` is both `"move"` and
`"lock"`, `bleep.wav` is both `"rotate"` and `"hold"`, so a move and a lock landing on the same
tick are both audible. That costs two of the sixteen buffers to buy one overlap, which does not
scale — a real game wants a small pool of sources allocated per *playback*, not per sound.

### 4.9 The MCP HTTP server binds IPv4 only, while everything documents `localhost` — CONFIRMED, MEASURED

`docs/mcp_server.md`, every tool description, and the brief all say `http://localhost:8765/mcp`.
On a machine where `localhost` resolves to `::1` before `127.0.0.1` — this one — every request pays
a failed IPv6 connection before falling back.

**Measured**, same tool, same process, back to back:

| URL | Latency |
|---|---|
| `http://127.0.0.1:8765/mcp` | **15 ms** |
| `http://localhost:8765/mcp` | **2,058 ms** |

That is 137x, and it is entirely invisible: everything works, just 2 seconds at a time. My first
bot run took 8 seconds per piece and I assumed the engine's tool dispatch was slow. It is not —
`TCPServer`'s accept loop sleeps 10 ms and the handler is straight-line code.

Two fixes, and both are worth doing: bind a dual-stack socket (or listen on `::` with
`IPV6_V6ONLY` off), and change the docs to say `127.0.0.1`.

### 4.10 `Object::Destroy()` only marks; two apps out of eleven reap — reported in the brief, taken on trust

I did not confirm the leak, because I took the documented precaution: `UpdateDebris()` calls
`renderer->DeleteDestroyedObjects()` once per tick whenever it destroyed anything, from `RunLogic`
where `physics_mutex` is held. Worth restating that this is a trap, not a feature — a `Destroy()`
that leaves a rigid body in the physics world forever will be found by everyone, once each.

### 4.11 An object motion occupies `ticks + 1` ticks — CONFIRMED BY MEASUREMENT, and it cost me a real bug

*Found after the first version of this report, from a bug Dick hit while playing: dropping a piece
into the area where a line had just cleared made some of its blocks vanish a tick after it landed.*

`Scene::MoveObjectOverTicks` is documented as moving an object "over exactly `ticks` physics
ticks", and the extra tick is described in `ObjectMotion` as belonging to physics-driven motions
only — *"The motion lives one tick past ticks_total for that (the velocity set on the last tick
still has to play out)"*. Neither is true of a plain object. `AdvanceObjectMotions` tests
`ticks_done >= ticks_total` at the **top** of the loop, so the completion branch (final
`SetPosition(target)`, then erase) runs for **every** motion on the call after the last
interpolating one. A 10-tick motion takes 11 calls, physics or not — and for a non-physics object
the extra call buys nothing, because `factor` already reached 1.0 on the last interpolating tick.

**How it broke the game.** My line-collapse animation ran `MoveObjectOverTicks(cell, ..., 10)` and
the collapsing phase lasted exactly 10 ticks. So on the tick the phase ended, `SyncBoardView`
restored every cube to its grid position — and then, later in that *same* tick,
`AdvanceObjectMotions` made its 11th call and wrote the collapse target back over it. Since the
normal sync path only set visibility and material and never re-asserted position, that cube stayed
one row low **for the rest of the run**, drawn on top of its neighbour. Drop a piece into that
region and its blocks appear to disappear a tick after landing, which is exactly the symptom
reported.

**Measured.** After 8 line clears, querying every `Cell x,y` object through the `object_get` MCP
tool and comparing against its grid slot:

| | displaced cubes |
|---|---|
| before the fix | **16** (each exactly one row low: `Cell 0,1` at y=0, `Cell 9,4` at y=3, …) |
| after the fix | **0** |

Re-checked over a longer run: 34 line clears, 0 displaced.

**Two things were wrong and I fixed both, in my app:**

1. `SyncBoardView`'s normal path now re-asserts each cube's position from the board array (guarded
   by a compare, so it does not dirty 200 transforms a tick). This is the real fix: the comment at
   the top of that function claimed the view was "rebuilt from scratch every tick", and position
   was the one thing that was not — which is precisely the field that got corrupted. Any
   displacement, from any cause, now self-heals on the next tick.
2. The collapse motion is requested for `TETRIS_COLLAPSE_TICKS - 1`, so it retires *before* the
   phase ends and there is no glitch frame to heal.

**The engine half is `docs/engine_backlog.md` item 32.** The fix there is to retire a non-physics
motion at `ticks_done == ticks_total`, keeping the extra tick only when `f_kinematic` — which is
what the header already promises. It matters more now than it did: with `QueueObjectMotion` (item
12) each queued leg pays the same extra tick, so a three-leg shake of 5 ticks each runs 18 ticks,
not 15, and chained legs drift further apart the longer the chain.

*Fixed in core on 2026-09-11, exactly as described above — see backlog item 32. Measured after the
fix: a plain motion of N takes N ticks, a kinematic one still takes N+1, three queued legs of 5
take 15, and a 45-piece / 16-line-clear bot run leaves 0 of 200 cell cubes displaced. The
`TETRIS_COLLAPSE_TICKS - 1` workaround at `ApplicationTetris.cpp:641` is no longer needed.*

**The lesson I would generalise:** a view that is "derived from the state" has to derive *every*
field it owns, or the one field it does not derive is where the bugs live. I wrote the comment
claiming that property before the code had it.

---

## 5. Changes to `core/` — three, all logged

### 5.1 `core/InputController.{h,cpp}` — `HasSyntheticHolds()` now covers the release tick

**What changed.** A new member `bool f_synthetic_release_tick` (guarded by the existing
`state_mutex`). `AdvanceSyntheticHolds()` clears it at the top and raises it whenever it erases a
hold after emitting that hold's key-up. `HasSyntheticHolds()` returns
`!synthetic_holds.empty() || f_synthetic_release_tick`. Both are commented with why.

**Why.** See §4.1. Without it, the input gate the engine's own design recommends drops every
edge-triggered scripted action. `AdvanceSyntheticHolds` is called exactly once per simulated tick,
so the flag marks exactly one tick — the one on which that release is readable.

**What it would have cost me not to.** An app-side latch ("a hold was live within the last 2
ticks") in `ApplicationTetris::GatherInput` — about six lines. I chose the core change anyway
because this is not an app-specific need: *every* app that wants to be driven by a script, a
replay or a test hits it, and every one of them would have to re-derive the same latch from the
same silent symptom. This is the bug I would most want fixed upstream.

**Behaviour change for the other ten apps — one app is affected, and it is affected for the
better.** `HasSyntheticHolds()` now returns true for one extra tick after the last scripted hold
ends. There is exactly one other caller in the repo: **`ApplicationShip.cpp:1264`**, which gates
the ship's character input on `main_window->f_has_focus || input->HasSyntheticHolds()` — the same
pattern, carrying the same bug today. Everything inside that block reads `IsKeyDown`
(level-triggered) and nothing reads `WasKeyReleased`, so the ship was never actually losing
anything; and on the one extra tick this change lets through, every key involved has already been
released and reads as down=false. So: **no observable change to the ship app**, and the trap is
disarmed if it ever adds a tapped control there. Nothing else in the repo reads the function.

### 5.2 `core/Renderer.cpp` — `AddMaterial` returns the index of the matching material

**What changed.** The duplicate-name scan now returns the matching index directly instead of
falling through to `return materials.size() - 1`. Same behaviour for a new material.

**Why.** See §4.2. The return value's contract is "the index of the material with this name", and
it was only honoured for names that were new.

**What it would have cost me not to.** Nothing — I used `FindMaterialIndex(name)` throughout, as
the brief advised. I fixed it because it is four lines, because the brief flagged it as worth
fixing, and because a wrong index is indistinguishable from a right one at the call site.

**Behaviour change for the other ten apps.** None that I can find. `AddMaterials(list)` discards
the return value. Every other call site in the repo adds a name that does not already exist, and
for those the returned index is identical to before. The only call that changes is one that was
already getting a wrong answer.

### 5.3 Verification that the other apps still build

After both changes, every app was rebuilt from the shared objects:

| App | Result |
|---|---|
| Tetris, Ship, Tank, UI, Tileset, Sim, Animation, IsoAnimation, OCPP, Dozer | build clean |
| **Grid** | **fails — and it already did, for reasons unrelated to this work** |

`APP=Grid` fails with `'class PlayerCharacter' has no member named 'SetNextAnimation'`
(`ApplicationGrid.cpp:133, 174, 194, 481`) and `'Object* Object::parent' is protected within this
context` (`ApplicationGrid.cpp:1103, 1106, 1107, 1187`). Both are consequences of the animation
rewrite and the `Object` encapsulation that landed before I started: every error is in
`ApplicationGrid.cpp`, and nothing in my three-file diff touches `PlayerCharacter`, `Object`, or
either symbol. Flagging it because it is a broken app in the tree, not because I broke it.

### 5.4 No other core files were touched

`Application`, `Scene`, `Object`, `Physics`, `SoundSystem`, `RRandom`, `MCPServer` and the shaders
are untouched. In particular I did **not** patch §3.2 (the `RunLogic` pause predicate), §4.4
(screenshots without ImGui), §4.5-4.8 (the SoundSystem bugs) or §4.9 (the IPv4 bind), even though
several are one-liners — each changes behaviour for apps I was told not to touch, and an
unpatched gap is a clearer signal than a patched one. They are all described above precisely
enough to fix in a few minutes each.

---

## 6. Friction and papercuts

**The MCP HTTP transport logs two `info` lines per request.** `"Client connected from
127.0.0.1:NNNNN"` and `"Calling client connect callback"`, from `TCPServer::AcceptConnections`. A
scripted session makes thousands of calls, so after one bot run my `stderr` log was 5,956 lines of
which essentially all were that. Anything my app logged was unfindable. These want to be `Trace`,
not `Info`.

**`GetObjectFromAsset` copies the asset's material *names* onto the object, and
`f_update_materials` starts true.** So an object whose `material_slot[0]` you assign by index gets
it silently overwritten on the next frame by a name lookup you did not ask for. It only bit me
because the OBJ's material name happened not to exist in the renderer's global list, in which case
`Object::UpdateMaterials` leaves the slot alone — so it worked by luck. Every cube in my app now
does:

```cpp
object->material_names[0].clear();
object->f_update_materials = false;
```

Two idioms for the same thing, one of which quietly wins, is a good way to lose an afternoon. The
slot-assignment path should probably clear `f_update_materials` itself, or `material_slot` should
be behind a setter that does.

**`Renderer::viewport_x` sets only the X offset; the Y offset is hardcoded to 0.**
`glViewport(viewport_x, 0, GetViewportWidth(), GetViewportHeight())` (`Renderer.cpp:412` and
`:689`). Fine for
the tank app's left strip, no good for a HUD band along the bottom. The field name promises more
generality than the call site delivers.

**`Camera::SetupOrthographic(width, height, zoom, ...)` takes a width and height that are
immediately overwritten.** `Renderer::DrawFrame` writes `camera->viewport.width/height` from the
viewport every frame (`Renderer.cpp:617-618`). So the two arguments the signature leads with are
dead on arrival, and I spent a few minutes working out whether I needed to update them on resize.
(I do not — resize is handled, which is good.) They could just go.

**`ApplicationUI.cpp` constructs `Renderer` twice.** The brief warned me; I looked, and it does —
`new Renderer(...)` at line 12 and again at line 18, the first one leaked. Anyone reaching for the
smallest app as a template inherits it. The file is 39 lines — either fix it or put a comment at
the top saying "do not copy this".

**Naming: `Scene::StepPhysics(n)` also steps logic, animation and input.** It runs a whole tick,
which is right and is what everyone wants, but the name says otherwise and I checked the source to
be sure.

**`Object::Show()` / `Hide()` / `SetVisibility(bool)` are three ways to say one thing**, and
`IsVisible()` is on the far side of the class from them.

**The brief's §6.4 example is wrong in a way worth correcting** now that §4.1 is fixed — it was
right in intent and the code did not support it. Worth a note in the brief for the next agent.

**Things I got wrong twice**, for the record, in case they are worth smoothing:
- Assumed `RunLogic` ran once per tick (§3.2). Read `Application.cpp` and found otherwise.
- Assumed a snapshot published per tick would answer "is it paused" (§3.3). It cannot, by
  construction.

---

## 7. What I would build next

1. **Record and replay** — step 7 of the deterministic-sim plan, and this app is now a good test
   case for it. Everything is in place: all mutation arrives as tick-stamped input events or
   `SimCommand`s, the piece bag is seeded from a command payload, every duration is a tick count,
   and the only non-determinism left in my app is the debris physics (which is cosmetic and could
   be excluded). A recorded game should replay to an identical board, and unlike a vehicle a
   Tetris board is *exactly* comparable — 200 cells of `int8_t` either match or they do not. That
   makes it the cheapest possible regression test for the whole determinism effort, and I would
   build `tetris_record` / `tetris_replay` MCP tools and a board-hash assertion before anything
   else.

2. **Fix the read-side gap properly** (§3.3) and move my snapshot onto it. Until that exists,
   every app's telemetry is a hand-maintained copy that will drift.

3. **A second capture path that includes ImGui** (§4.4). It is the difference between an agent
   being able to check its own UI work and not.

4. **World-space text** (§3.4), starting with a single `DrawWorldText`. Half the polish a game
   wants is words in the right place, and right now none of it is reachable.

5. **Make the debris matter, carefully.** The physics is currently pure garnish and the brief was
   right that it should be. But there is a nice half-step: let cleared-row debris knock against the
   *previous* clear's debris piling up in a tray beside the well, so the player accumulates visible
   evidence of a good game without any of it touching the rules. That is the shape physics should
   take in a game whose rules must stay exact.

6. **The skeletal-animation probe** (step 10), which I deliberately did not start. The brief
   budgets an hour and warns it is mid-rewrite with a `debug->Fatal` waiting in
   `ObjectAnimation.cpp:108` for any clip carrying a scale track. With the game and this report
   finished, that is the next hour I would spend — and I would spend it expecting to write a
   finding rather than a feature.

---

## Appendix: running it

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
mingw32-make.exe APP=Tetris -j8
./wind.exe 2>tetris_stderr.log &
```

Controls: arrows move and soft-drop, `Z`/`X` or up rotate, space hard-drops, `C` or shift holds,
`R` restarts, `P` pauses, `F1` toggles the engine's Scene/Inspector/Engine panels (off by default,
so the game screen is the game).

Driving it without a human — note `127.0.0.1`, not `localhost` (§4.9):

```bash
python tools/tetris_bot.py --pieces 60 --seed 11
```

MCP tools: `tetris_state`, `tetris_input`, `tetris_pause`, `tetris_step`, `tetris_restart`, plus
everything core registers. All of them take `include_screenshot`.

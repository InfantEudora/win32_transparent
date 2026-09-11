# Brief: build a classic game (Tetris) on this engine

You are the second agent on this project. The engine's author has spent a long time building
`win32_transparent` as a collection of experiments that share a common core. The core has
matured; the question this task answers is **whether an actual game can be built on it, and what
is missing when you try.**

Read this document top to bottom before writing code. It is the map. Everything in it was
verified against the source at commit `9465ce9`.

---

## 1. The task, and the two deliverables

**Deliverable 1 — a playable Tetris.** A new app, `APP=Tetris`, that builds and runs alongside
the ten existing ones. Real Tetris: 10x20 well, seven tetrominoes, rotation, gravity, soft/hard
drop, line clears, scoring, levels, game over, restart.

**Deliverable 2 — an engineering report.** `docs/tetris_findings.md`. This is the part the author
actually asked for. Everywhere the engine fought you, write it down: a missing feature, a bug, an
API that could not express what you needed, boilerplate that should have been one call,
documentation that was wrong. Be specific and reproducible — file, line, what you expected, what
happened. A finding you worked around still counts and should still be reported.

Both matter. Do not sacrifice the report to finish the game.

### Ground rules

- **You may patch `core/`, but every change must be logged.** Keep a "Changes to core/" section in
  `docs/tetris_findings.md` with one entry per change: file, what you changed, why, and what it
  would have cost you not to. Prefer a workaround in your own app over a core change when the two
  are comparable — an unpatched gap is a clearer signal than a patched one. Never make a change
  that alters behaviour for the other ten apps without saying so loudly in the log.
- **Do not touch the other apps** (`Application{Ship,Tank,Grid,Dozer,Tileset,Sim,Animation,IsoAnimation,OCPP,UI}.cpp`
  or their folders). Read them freely — they are the best documentation in the repo.
- **Scope is deliberately stretched.** The author wants the game to exercise physics, sound and
  animation, not just the render path. See §7 for exactly how far to take that and in what order.
- **The code style is not yours to change.** Match the surrounding code: 4-space indent, `f_`
  prefix on booleans, `//` comments that explain *why*. This codebase comments unusually heavily
  and unusually well; write in that register.

---

## 2. The mental model in sixty seconds

One executable, `wind.exe`. `APP=X` at build time selects which `Application` subclass `main.cpp`
instantiates. There is no scripting layer, no editor, no scene format — a scene is built in C++ in
your app's `Init()`.

Three threads, created in this order:

| Thread | Created by | Runs | Owns |
|---|---|---|---|
| **Main** | `WinMain` | `Application::Start()` message pump | Win32 window messages only. Blocks during title-bar drags. |
| **Render** | `Application::Start()` | `Init()` once, then `DrawFrame()` forever | The OpenGL context. **All GL calls must happen here.** |
| **Physics** | render thread, after `Init()` returns | `UpdateInput` -> `UpdateAnimations` -> `RunLogic` -> `UpdatePhysics`, paced to `physics_tps` | The simulation. |

A fourth thread does Raw Input acquisition, and the MCP server adds one or two more. Neither
touches simulation state except through the queues described below.

**There is one lock: `renderer->physics_mutex`.** It is coarse and it is the whole contract:

- The physics thread holds it for the entire tick (input + animations + logic + physics).
- The render thread holds it for the entire `DrawImGuiUI()` call.
- `Renderer::DrawFrame` takes it around the actual drawing.

So: **your `RunLogic()` may touch anything. Your `DrawImGuiUI()` may read anything.** Anything
else — an MCP tool handler, a worker thread — may touch *nothing* directly and must go through
`Scene::SubmitCommand` (§6.9) or `InputController::SubmitEvent` (§6.4).

**Time is measured in ticks, never in milliseconds.** `Scene::GetPhysicsTick()` is the clock; it
advances only when a tick actually runs, so it stops while paused and steps by exactly N when
single-stepped. `GetTickCount64()` in gameplay logic is a bug: it is ~15.6 ms granular against a
20 ms tick and it keeps running while the sim is paused. Your lock delay, your DAS repeat rate,
your line-clear animation length — all of them are tick counts.

The engine is being steered toward deterministic replay (input stream + command stream => identical
run). You are not asked to implement recording, but honour the model: all mutation from outside
the physics thread goes through a queue, and durations are in ticks.

---

## 3. Build and run

### Toolchain

MSYS2 MinGW-w64, **not** on the default `PATH` of a fresh shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
```

Use `mingw32-make.exe`, **not** `/usr/bin/make.exe` — the latter picks up the wrong shell
semantics here.

### Building

```bash
cd "/c/IDE-E/Mijn Documenten/Projects/code/test/win32_transparent"
export PATH="/c/msys64/mingw64/bin:$PATH"
mingw32-make.exe APP=Tetris -j8
```

Notes that will otherwise cost you an hour each:

- **`APP=` changes which `main.o` is correct.** The makefile tracks the last-built app in
  `.current_app` and rebuilds `main.o` when it changes. Trust it; don't hand-delete objects.
- **Header dependencies are tracked** (`-MMD -MP`, the `.d` files). Editing a header rebuilds its
  dependents. This was not always true — if you ever see inexplicable heap corruption
  (`c0000374`) after switching apps, that is stale objects, and `mingw32-make.exe clean` is the
  cure.
- **`-fno-exceptions`.** No `throw`, no `try`. `nlohmann::json` is built with `JSON_NOEXCEPTION`,
  so a malformed parse returns a discarded value rather than throwing — check `is_discarded()`.
- **Sound is opt-in per app.** Add `USE_SOUND := 1` to `apps/Tetris.mk` or `core/SoundSystem.cpp`
  is dropped from the build entirely and `-lOpenAL32` is not linked. You need this (§6.7).
- Debug flags (`-Og -g`) are on by default. Leave them.

### Running

**`wind.exe` was running the Ship app when this brief was written.** The linker cannot overwrite a
running executable, so a build will fail with a permission error until you stop it:

```bash
tasklist //FI "IMAGENAME eq wind.exe"
taskkill //F //IM wind.exe
```

You have been cleared to do this. Launch your own build the same way any app is launched — just
run `./wind.exe`. It opens a window; `stderr` carries all logging (`Debugger` writes there, never
to `stdout`, because `stdout` is the MCP stdio transport).

```bash
./wind.exe 2>wind_stderr.log &
```

### Verifying without eyes

Every app embeds an MCP server on **HTTP port 8765** (`docs/mcp_server.md` has the full story).
This is how you check your own work:

```bash
curl -s -X POST http://127.0.0.1:8765/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"object_list","arguments":{}}}'
```

Generic tools every app gets for free: `status`, `object_list`, `object_get`,
`object_set_transform`, `object_move`, `screenshot`, `camera_get`, `camera_set`.
`screenshot` returns a PNG of the current frame as an MCP image block — **use it.** It is the
fastest way to see that your well is the right shape and your pieces are the right colour.

Register your own tools in `Init()` (§6.10). A `tetris_state` tool that dumps the board as ASCII,
a `tetris_input` tool that presses a key for N ticks, and `tetris_step` for single-stepping will
pay for themselves within the first hour.

---

## 4. Creating the app

Three files plus a folder. Follow the existing convention exactly.

**`apps/Tetris.mk`**

```make
APP_HEADER := ApplicationTetris.h
APP_CLASS  := ApplicationTetris

SRCS    += ApplicationTetris.cpp
IPATHS  += -Itetris/
DIR_SRC += ./tetris

#Line clears and lock-downs play sounds - see core/SoundSystem.h.
USE_SOUND := 1
```

**`ApplicationTetris.h` / `ApplicationTetris.cpp`** in the repo root (that is where every app
lives), and **`tetris/`** for your gameplay classes (`Playfield.cpp`, `Tetromino.cpp`, ...). This
split is the house style: the `Application` wires things together, the folder holds the game.

The minimum viable `Init()` — this is the distilled version of what `ApplicationTileset::Init()`
and `ApplicationShip::Init()` both do:

```cpp
void ApplicationTetris::Init(void){
    //PIPELINE_DEFERRED gives you the object-id buffer, and so mouse picking.
    //PIPELINE_MSAA is prettier but has no picking. Deferred is the right default.
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to initialise rendering pipeline\n");
    }
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    assetmanager = new AssetManager();
    assetmanager->AddNewAssetFromOBJFile("block","data/unit_cube.obj");

    main_scene = CreateNewScene("Tetris");      //also makes and adds "Main Camera"
    main_scene->physics_world = new PhysicsWorld();
    main_scene->physics_world->SetGravity(vec3(0,-9.81,0));

    {   //Without a light, everything renders black.
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Sun";
        sun->SetPosition(vec3(-10,14,9));
        sun->SetLookAt(vec3(5,10,0));           //aim it at the middle of the well
        sun->color = vec3(1,0.95,0.9);
        sun->brightness = 5.0f;
        sun->viewport.zoom = 20;                //shadow ortho half-extent; must cover the board
        main_scene->AddObject(sun);
    }

    rrand = new RRandom();
    rrand->Generate(512,512);                   //piece bag randomness

    SetPhysicsTPS(60.0f);                       //60 Hz tick; see the note below

    BuildWell();                                //your code
    RegisterCommandHandlers();                  //your SimCommand types, if any
    RegisterMCPTools();                         //your tools

    main_window->Resize(1280,800);
    main_scene->StepPhysics(1);                 //one tick so frame 1 isn't empty
}
```

**Do not use `ApplicationUI.cpp` as your template.** It is the smallest app but it constructs
`Renderer` twice, leaking the first — copying it will leak and confuse you. `ApplicationTileset`
and `ApplicationShip` are the good references.

On `SetPhysicsTPS(60.0f)`: the engine defaults to 50 Hz. Tetris convention is denominated in
frames at 60 Hz (a "1G" drop is one cell per frame). Setting 60 makes your gravity table
translate directly. Either is fine — but pick one, state it, and never compute a duration from
wall-clock time.

Override list for a game like this:

```cpp
void Init(void) override;          //render thread, once
void RunLogic(void) override;      //physics thread, every tick - your game loop lives here
void DrawImGuiUI(void) override;   //render thread, physics_mutex held - HUD only
void PreRender(void) override;     //render thread, before the scene draws - GL work only
vec3* GetCameraTargetPtr() override; //one-liner that lets the camera MCP tools see your pivot
```

`RunLogic()` runs **before** `UpdatePhysics()` in the tick, on the physics thread, with the lock
held. That is where the entire game goes.

---

## 5. Shape of the game on this engine

A recommendation, not a requirement — but it is the shape the engine is built for, and deviating
from it will cost you time you should be spending on findings.

**There is no 2D renderer.** `core/Sprite.h` and `core/SpriteSheet.h` exist but only ever feed
`ImGui::ImageButton` — they cannot draw into the world. Do not try to build a sprite pipeline.

**Render the board in 3D and point an orthographic camera at it.** One `Object` per cell, meshed
from `data/unit_cube.obj` (a true 1x1x1 cube centred on its origin), positioned on integer
coordinates, one material per tetromino colour. This gives you a crisp 2D-looking playfield with
lighting and shadows for free, and every object is inspectable through the Scene/Inspector panels
and the MCP tools.

```cpp
main_scene->camera->SetType(CAMERA_TYPE_ORTHOGRAPHIC);
main_scene->camera->SetupOrthographic(renderer->width,renderer->height,12.0f,0.1f,100.0f);
main_scene->camera->SetPosition(vec3(5,10,20));
main_scene->camera->SetLookAt(vec3(5,10,0));
main_scene->camera->CalculateLookatMatrix();
```

(`zoom` is the vertical half-extent; the horizontal is `zoom * aspect`.)

**Keep the game state in a plain array, not in the physics engine.** `uint8_t board[20][10]`.
Collision, rotation and line detection are array arithmetic — that is what makes Tetris feel
correct, and rp3d cannot give you the exactness Tetris needs. The `Object`s are a *view* of the
array. Physics is for the garnish (§7), not the rules.

---

## 6. The API surface

Everything below was read from the source.

### 6.1 Scene and Object

`Scene` (`core/Scene.h`) is the container. `Scene::AddObject(Object*)` appends to
`renderer->objects` — a flat list of *roots*; children are found by traversal. Lookup:
`FindObject(name)`, `FindObjectByID(id)`, `ForEachObject(fn)` — all depth-first over the whole
tree. **Names are not unique; ids are.**

`Object` (`core/Object.h`) is the universal node — mesh, transform, materials, optional physics
body, optional animations, children. `Camera` and `Light` derive from it, which is why the camera
is itself in the scene.

Transform: `SetPosition` / `GetPosition` are **local** (within parent); `GetWorldPosition` walks
the chain. `SetRotation(quat)`, `RotateAroundAxis(axis,radians)`, `YawBy/PitchBy/RollBy`,
`SetScale(vec3)`. `SetPosition`/`SetRotation` take `f_write_physics = true` by default, which
teleports the rigid body with the object — pass `false` when you are only mirroring a body that
already moved.

Hierarchy: `AttachChild` / `DetachChild` / `FindChild`. Useful for Tetris — parent the four cells
of the active piece to one empty `Object` and rotate the parent.

Lifetime: `object->Destroy()` only **marks** it. `Renderer::DeleteDestroyedObjects()` does the
actual reaping — see the trap in §8.

**To hang your own data on an object, subclass it.** There is no `void* user_data`, and you do not
need one: `GetObjectFromAsset` takes an optional target, so it will load an asset straight into a
type of yours.

```cpp
class MyCell : public virtual Object{ public: int2 coordinate; };

MyCell* cell = new MyCell();
assetmanager->GetObjectFromAsset("block",cell);   //fills in mesh + material names
main_scene->AddObject(cell);
...
MyCell* hit = dynamic_cast<MyCell*>(hovered_object);   //reverse lookup, no parallel array
```

`IsoCell` (which carries exactly this kind of grid coordinate), `ShipCharacter`, `Asteroid`,
`HingedDoor` and `Pickup` are all built this way — copy any of them.

### 6.2 Meshes and assets

`AssetManager` (`core/AssetManager.h`) holds reusable mesh + material-name bundles:

```cpp
assetmanager->AddNewAssetFromOBJFile("block","data/unit_cube.obj");
Object* cell = assetmanager->GetObjectFromAsset("block");   //new Object sharing the mesh
assetmanager->GetObjectFromAsset("block", existing_object); //or fill one you already have
```

Assets are keyed by an FNV-1a hash of the name (`assetid_t`) so a `SimCommand` can carry one.

glTF: `gltfloader.LoadGLTFFile("data/x.glb")` then `GetAllAssetsFromGLTF()` /
`GetAssetsFromGLTF("name1","name2")` / `CreateNewObjectFromGLTF(node,scene)`. **Must be called on
the render thread** (it asserts this) — i.e. from `Init()`.

Procedural meshes: fill `std::vector<vertex>` (`core/type_vertex.h`: pos, normal, tangent, uv,
matid) and call `mesh->SetMeshData(verts.data(),verts.size())`. `ApplicationShip.cpp:80-125` is a
clean worked example (it builds a cube by hand). There are **no primitive generators** — no
`MakeCube()`, no `MakeQuad()`. Use `data/unit_cube.obj`.

### 6.3 Materials and colour

Materials are global to the `Renderer` and referenced by index:

```cpp
Material m;
m.name = "tetris_cyan";
m.glsl_material.color = vec4(0.0f,0.85f,0.9f,1.0f);
m.glsl_material.metallic  = 0.1f;
m.glsl_material.roughness = 0.6f;
m.glsl_material.emissive  = vec4(0.0f,0.3f,0.35f,1.0f);   //xyz factor, w strength - makes it glow
renderer->AddMaterial(m);

//Look the index up by name. Do NOT use AddMaterial's return value - see §8.
cell->material_slot[0] = renderer->FindMaterialIndex("tetris_cyan");
```

`material_slot[0..3]` are per-object; a mesh's vertices carry a `matid` selecting among them. The
alternative idiom is `object->material_names[0] = "tetris_cyan"` plus `f_update_materials = true`,
which resolves by name on the next frame — that is what asset-loaded objects use. Either works;
direct slot assignment is more predictable.

`emissive.w` is what actually makes something glow — `emissive.rgb` alone is clamped to 1 and can
never exceed a fully lit white surface. Good for a ghost piece or a flashing cleared line.

### 6.4 Input

`core/InputController.h`. The model is an **event stream**, not a polled snapshot, because it is
meant to be recordable.

Default mappings already exist: arrows -> `INPUT_MOVE_*`, WASD -> `INPUT_TURN_*`, mouse buttons,
`INPUT_SHIFT`, `VK_PAUSE` -> `INPUT_PAUSE` (which toggles `Scene::PausePhysics`).

Add your own, numbering from `INPUT_LAST`:

```cpp
#define INPUT_ROTATE_CW    INPUT_LAST+1
#define INPUT_ROTATE_CCW   INPUT_LAST+2
#define INPUT_HARD_DROP    INPUT_LAST+3
#define INPUT_HOLD         INPUT_LAST+4
#define INPUT_RESTART      INPUT_LAST+5

main_scene->inputcontroller->AddKeyMap('X',INPUT_ROTATE_CW);
main_scene->inputcontroller->AddKeyMap('Z',INPUT_ROTATE_CCW);
main_scene->inputcontroller->AddKeyMap(VK_SPACE,INPUT_HARD_DROP);
```

Several system keys may map to one action (`AddKeyMap(VK_UP,INPUT_ROTATE_CW)` as well as `'X'`) —
`f_isdown` is a count of held mappings, so the action is down while any of them is.

Read it in `RunLogic()`:

```cpp
InputController* input = main_scene->inputcontroller;
if (input->WasKeyReleased(INPUT_ROTATE_CW)){ RotatePiece(+1); }   //edge: one press, one rotation
if (input->IsKeyDown(INPUT_MOVE_LEFT)){ /* held: run your DAS counter */ }
```

`WasKeyReleased` is the edge trigger and is what you want for rotate, hard drop and hold.
`IsKeyDown` is the level and is what you want for soft drop and for DAS. **Implement DAS as tick
counters** (`das_ticks`, `arr_ticks`), never as milliseconds.

Gamepad is folded in: `AddGamePadMap(analog_index, mapped)` and `GetNormalizedAnalogValue(mapped)`,
XInput, all four slots, with reconnect. Mapping the D-pad would be a nice test of a path only two
apps currently use.

`HoldKey(mapped, duration_ticks)` and `HoldAxis(mapped, value, duration_ticks)` are how a
*scripted* player presses a button — an MCP tool, later a replay. They emit ordinary events, so
the simulation cannot tell them from a human. This is how you write an automated test that plays
your game. They are deliberately not focus-gated.

Input handling in most apps is gated on window focus (`main_window->f_has_focus`). If you do that,
also allow `input->HasSyntheticHolds()` through, or your MCP-driven tests will do nothing whenever
the window is not in front — which is exactly when they run.

### 6.5 The tick, pausing and stepping

```cpp
scene->GetPhysicsTick();        //the clock. Only advances when a tick really runs.
scene->GetPhysicsTimestep();    //seconds per tick, constant for the run
scene->IsPhysicsPaused(); scene->PausePhysics(bool);
scene->StepPhysics(n);          //queue n ticks while paused - deterministic single-stepping
scene->GetPendingPhysicsSteps();
app->physics_time_factor;       //slow-motion: fewer ticks per second, never a smaller timestep
```

Pause + step is the single most useful debugging facility in the engine and it is already wired to
MCP in the tank app (`tank_pause` / `tank_step`). Build the equivalent for yourself early.

### 6.6 Physics

`core/physics/`. A thin wrapper over reactphysics3d 0.10.2 (a **locally patched fork** — see §8).

```cpp
Physics* p = object->AddPhysics(main_scene->physics_world);
if (p){
    p->AddBoxCollider(vec3(0.5,0.5,0.5), vec3(0,0,0), quat().identity(), /*density*/ 1.0f);
    p->SetStatic(false);
    p->SetGravityEnabled(true);
    p->body->rigidbody->setLinearDamping(0.2f);
}
```

`AddPhysics` starts a body **static with gravity off** — you opt in to dynamics. Set position and
rotation *before* `AddPhysics`; that is what seeds the body's transform. Colliders:
`AddBoxCollider`, `AddSphereCollider`, `AddCapsuleCollider`, `AddHeightFieldCollider`. Also
`SetMass`, `SetVelocity`, `AddWorldForceAt`, `SetFrictionCoefficient`, `SetBounciness`,
`SetTrigger`, `SetBodyType` (STATIC/KINEMATIC/DYNAMIC).

Every body created through `AddPhysics` has `rigidbody->setUserData(owning Object*)`, so a
collision callback can get back to your object. Subscribe by implementing
`rp3d::EventListener::onContact` / `onTrigger` on your Application and calling
`main_scene->physics_world->rp_world->setEventListener(this)` **after** the world is populated.

`physics_world->SetDebugRendering(true)` draws every collider as wireframe. Invaluable.

Two hard-won rules from the existing code, both of which will bite you:

- **In `onContact`, contacts are reported BEFORE the solver runs.** Read a velocity on
  `ContactStart` only; by `ContactStay` you are seeing the post-bounce value.
- **Never create or destroy a body from inside a physics callback.** rp3d is mid-iteration over
  its own arrays. Stage it in a vector and act on it at the top of the next `RunLogic()`.
  `ApplicationShip.cpp:995-1041` is the canonical worked example.

### 6.7 Sound

`core/SoundSystem.h`, OpenAL, WAV only. Requires `USE_SOUND := 1` in your `.mk`.

```cpp
soundsystem = new SoundSystem();
soundsystem->Initialise();
soundsystem->AppendFile("data/sound/click.wav","move");
soundsystem->AppendFile("data/sound/bleep.wav","rotate");
soundsystem->AppendFile("data/sound/floop.wav","clear");
soundsystem->AppendFile("data/sound/hax.wav","gameover");
...
soundsystem->Play("clear", /*looping*/ false, /*gain*/ 0.8f);
```

`data/sound/` already has `bleep.wav`, `click.wav`, `floop.wav`, `hax.wav` — enough for a whole
Tetris. `Pause`, `Rewind`, `FinishedPlaying(handle)` round it out. Limits and traps in §8.

### 6.8 Motion over ticks — the animation you actually want

```cpp
vec3 target = vec3(x,y,0);
main_scene->MoveObjectOverTicks(object,&target,NULL,/*ticks*/ 4);
```

Interpolates position (lerp) and/or rotation (slerp) over exactly N **ticks**. An object with a
body is switched to KINEMATIC for the duration and driven by velocity, then snapped and restored —
so it shoves dynamic bodies properly instead of teleporting through them. Either pointer may be
NULL. A new request for the same object replaces the one in flight.

This is your piece-movement animation, your line-collapse slide, your hard-drop slam. It is
deterministic, it is tick-denominated, and it is one call. Use it heavily. Skeletal animation
(§7) is a different and much rougher system.

### 6.9 SimCommand — mutating the sim from another thread

`core/SimCommand.h`. A fixed-size, trivially-copyable POD with a type tag, queued and applied on
the physics thread at the top of a tick. The command is **data** (recordable); the handler is
**code** (registered, never recorded). That split is the whole design.

Core types: `SIM_CMD_OBJECT_SET_TRANSFORM`, `_SET_PHYSICS`, `_SPAWN_ASSET`, `_SPAWN_PRIMITIVE`,
`_DUPLICATE`, `_DESTROY`, `SIM_CMD_WORLD_SET_GRAVITY`. Number your own from `SIM_CMD_LAST`.

```cpp
#define TETRIS_CMD_RESTART   SIM_CMD_LAST+0

main_scene->RegisterCommandHandler(TETRIS_CMD_RESTART,
    [this](const SimCommand& cmd) -> objectid_t {
        NewGame();                       //runs on the physics thread, inside the tick, lock held
        return OBJECTID_INVALID;
    });
```

Submitting:

- From an **MCP tool handler**: `Application::SubmitCommandAndWait(cmd)` — blocks until applied,
  returns the object id the handler produced. Safe there because MCP threads hold no lock.
- From **`DrawImGuiUI`**: `Application::SubmitUICommand(cmd)` — submit and return, never wait.
  **Waiting there deadlocks instantly**, because `DrawImGuiUI` already holds `physics_mutex` and
  the physics thread needs it to drain the queue.
- From **`RunLogic`**: don't. You are already on the physics thread inside the tick — just call
  the function.

A consequence of the simulation's own state (a line clearing because a piece locked) is **not** a
command — it is simulation, and a replay reproduces it by reproducing the cause. Commands are for
intent arriving from outside.

### 6.10 MCP tools

```cpp
MCPServer::Get()->RegisterTool(
    "tetris_state",
    "Current board as ASCII rows, plus score, level, lines and the active piece.",
    json{{"type","object"},{"properties",json::object()}},
    [this](const json& args) -> json {
        json result = BuildStateJson();                     //read-only: safe from this thread?
        return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
    });
```

Register from `Init()`. The server starts only after `Init()` returns, so registration can never
race a client's `tools/list`.

**A tool handler runs on an MCP thread and holds no lock.** Reading scene state from there is a
data race against the physics thread. The safe patterns are: (a) keep a snapshot struct that
`RunLogic` fills each tick behind a small mutex of your own, and serve tools from that; or (b)
pause the sim and step it explicitly, which is what the tank tools do. Pick one deliberately and
say which in your report — this is a genuine sharp edge in the current design.

`MaybeAttachScreenshot(result, bool)` adds a PNG image block to any tool's result in one line.

### 6.11 ImGui HUD

`DrawImGuiUI()` runs on the render thread with `physics_mutex` held, so reading simulation state
is safe. Docking is enabled; the core Scene / Inspector / Engine panels are drawn by
`Application::RenderApplicationUI()` — call it if you want them (you do, while developing), or
skip it for a clean game screen.

```cpp
void ApplicationTetris::DrawImGuiUI(){
    RenderApplicationUI();                 //the engine's own panels - drop this for a clean look
    ImGui::Begin("Tetris");
    ImGui::Text("Score %d", score);
    ImGui::Text("Level %d  Lines %d", level, lines);
    if (ImGui::Button("New game")){
        SimCommand cmd; cmd.type = TETRIS_CMD_RESTART;
        SubmitUICommand(cmd);              //NOT SubmitCommandAndWait - see §6.9
    }
    ImGui::End();
}
```

The only font loaded is `fonts/consola.ttf` at 13px. There is **no world-space text rendering at
all** — no bitmap font, no text mesh. A large "GAME OVER" or a next-piece label must be ImGui, or
must be built out of cubes. Worth a finding either way.

---

## 7. Suggested order of work

Do these in order. Each one is playable-or-visible before the next begins, and each is a natural
place to stop and write findings.

1. **Skeleton app.** `apps/Tetris.mk`, `ApplicationTetris.{h,cpp}`, empty `tetris/`. Build, run,
   see a lit cube. Confirms toolchain, app switching, MCP reachable on 8765.
2. **The well.** Static border objects, orthographic camera framed on the board, one material per
   tetromino colour. Verify with `screenshot`.
3. **Board and pieces, no input.** `board[20][10]`, the seven tetrominoes, a spawn, gravity on a
   tick counter, collision against the array, lock-down, line detection and clear. Render by
   syncing cell objects from the array each tick. This is the game; get it right before anything
   else.
4. **Input.** Keymaps, edge vs level, DAS/ARR in ticks, soft drop, hard drop, rotation with wall
   kicks. Now it is playable.
5. **HUD and game states.** Score, level, lines, next piece, hold piece, game over, restart via
   `SimCommand`.
6. **Your MCP tools.** `tetris_state`, `tetris_input`, `tetris_step`. Then drive a full game
   through them to prove it is deterministic and testable without a human.
7. **Sound.** Move, rotate, lock, line clear, game over. Four files are already in `data/sound/`.
8. **Motion over ticks.** `MoveObjectOverTicks` for the piece slide, the row collapse, the hard
   drop slam. Cheap, deterministic, and it makes the game feel finished.
9. **Physics garnish.** *Now* bring rp3d in, and keep it strictly cosmetic — the board stays an
   array. A cleared row spawns dynamic debris cubes that fall out of the well and are reaped after
   N ticks; a game over topples the stack. Remember: stage every body creation for the top of
   `RunLogic`, never inside a callback.
10. **Skeletal animation — a probe, not a feature.** Optional, last, and only if the game is done.
    Put an animated character beside the board (`data/gwen_anim.glb` or `data/elf.glb` with
    `gltfloader.GetSkeleton(...)` + `LoadAnimation(...)` + `object->AddAnimation(...)` +
    `TransitionToAnimation("name")`) that reacts to line clears. **Expect this to be rough** —
    see §8. The point is to find out *how* rough and write it down; if it costs more than an hour,
    stop and report that as the finding.

If you run short of time, cut from the bottom. Steps 1-6 plus a report is a success. Steps 1-10
with no report is not.

---

## 8. Known shortcomings, bugs and traps

The author asked that these be told to you up front rather than left for you to discover. They are
a head start, not a complete list — find more.

### Confirmed bugs

- **`Renderer::AddMaterial` returns the wrong index for an existing material.**
  `core/Renderer.cpp:1167` — when the name already exists it skips the insert but still returns
  `materials.size() - 1`, i.e. the index of the *last* material rather than the matching one.
  Always use `FindMaterialIndex(name)`. (Small, self-contained, well worth fixing and logging.)

- **`SoundSystem::Play` silently plays the wrong sound for an unknown handle.**
  `core/SoundSystem.cpp:170` uses `map_handles[name]` — `std::map::operator[]` *inserts* 0 for a
  missing key, so a typo plays buffer 0 instead of erroring. Also: `NUM_AL_BUFFERS` is **16**, a
  hard cap on distinct sounds, and there is one source per sound, so a sound cannot overlap with
  itself — rapid line clears will cut each other off.

- **`Object::UpdatePhysicsState` writes a body's WORLD transform into the object's LOCAL
  transform.** `core/Object.cpp:397-408`. Fine for root objects, wrong for a physics body that is
  also somebody's child. If you parent physics bodies, expect this.

- **`ApplicationUI.cpp` constructs `Renderer` twice**, leaking the first. Don't copy it.

### Structural gaps

- **No 2D / sprite rendering.** `Sprite`/`SpriteSheet` only feed ImGui image buttons. No
  world-space quads, no batched 2D. §5 explains the way around it.
- **No world-space text.** ImGui only, one 13px font.
- **No primitive mesh generators.** No `MakeCube`/`MakeQuad`/`MakePlane`. Load
  `data/unit_cube.obj` or hand-build vertices.
- **No scene serialisation to speak of.** `Application::BuildSceneFromJSON()` reads `export.json`
  and only restores name/position/rotation of asset-backed objects. There is no save/load of a
  running scene, so no save-game.
- **`Object::Destroy()` only marks.** The actual reaping is
  `Renderer::DeleteDestroyedObjects()`, which **only two apps call** (Ship, Dozer). Call it
  yourself, once per tick, from `RunLogic` — otherwise destroyed objects stop rendering but their
  rigid bodies stay in the physics world forever. If you spawn debris (step 9), this matters.
- **`Scene::AddObject` during a tick invalidates the iteration.** `Scene::UpdatePhysics`
  range-for's `renderer->objects`; an `AddObject` from inside an `UpdatePhysicsState()` override
  `push_back`s the same vector. Stage additions and perform them in `RunLogic` instead.
- **MCP tool handlers hold no lock** (§6.10). Reading scene state from one is a race. There is no
  sanctioned read-side equivalent of `SubmitCommand` — this is a real gap and a good finding.

### Mid-rewrite / fragile areas

- **The animation system is mid-rewrite.** `ObjectAnimation`/`PlayerCharacter` were recently
  rebuilt around swing-twist root motion with per-clip `extract_horizontal_root_motion` /
  `extract_vertical_root_motion` flags; the old `AnimationGraph` is gone. `docs/animation_root_motion.md`
  and `docs/animation_state_machines.md` describe the intent, and both are partly aspirational.
  `ObjectAnimation.cpp:108` still has `debug->Fatal("TODO: Implement animation scaling")` — a clip
  with a scale track will kill the process. Treat this system as a probe (step 10), not a
  dependency.
- **`RRandom` is not safe for a deterministic sim.** One generator instance is shared by the
  physics thread and by UI/MCP code, and any off-tick draw shifts the stream for everyone. No
  design has been chosen yet; do not try to fix it. **Give your piece bag its own `RRandom`
  instance**, drawn from only inside `RunLogic`, and note in your report that you had to.
- **reactphysics3d is a locally patched fork.** `libs/libreactphysics3d-0.10.2.a` carries three
  local patches plus a merged PR, built from `C:/code/reactphysics3d` (branch `vehicle-constraint`)
  with CMake + Ninja — *not* MSYS make. **Never overwrite it with a stock build.** You should not
  need to touch it at all.
- **A vehicle/tank yaw bug is open** and unrelated to you. Ignore it.

### Small things that will cost you time

- Anything that needs GL — texture upload, shader compile, glTF load — must happen on the render
  thread, meaning in `Init()` or `PreRender()`. `RunLogic` must never touch GL.
- `renderer->viewport_x` / `viewport_width` / `viewport_height` confine the 3D draw to a
  sub-rectangle of the window while ImGui keeps the whole canvas. `ApplicationTank::Init()` uses
  this to reserve a left-hand panel strip. Handy for a HUD-beside-board layout.
- `shaders/` is read from disk at startup, relative to the working directory. Run `wind.exe` from
  the repo root or nothing loads.
- The author is often running the app while you work. Unexplained motion in telemetry is usually
  a human at the controls, not a bug.

---

## 9. What to hand back

`docs/tetris_findings.md`, structured:

1. **Summary** — did you get a playable Tetris, and what is the honest state of it.
2. **What the engine did well.** Genuinely — which APIs were a pleasure, what took one line that
   would have taken fifty elsewhere. This is as useful to the author as the complaints.
3. **Missing features** — ranked by how much they cost you. For each: what you wanted to do, what
   you did instead, and a sketch of what the API should have looked like.
4. **Bugs found** — file, line, reproduction, expected vs actual. Include the ones listed in §8
   *only if you confirmed them yourself*, and say so.
5. **Changes to `core/`** — one entry per change, with the justification. Required.
6. **Friction and papercuts** — boilerplate, wrong docs, confusing names, things you got wrong
   twice.
7. **What you would build next** if you had another day.

Write it as you go, not at the end. The findings you lose are the ones you were sure you would
remember.

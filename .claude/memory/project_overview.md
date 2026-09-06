---
name: project-overview
description: "What win32_transparent is: a hand-rolled C++17/OpenGL game engine for Windows with a one-exe-many-apps build (APP=), and its 3-thread render/physics/input architecture"
metadata:
  node_type: memory
  type: project
  modified: 2026-09-06T00:00:00.000Z
---

`win32_transparent` is the user's own from-scratch C++17 / OpenGL 4.5 game engine and testbed for Windows, built with MinGW/MSYS2 — no engine, no framework, no scripting layer. Started 2024-02-20 from dhpoware's `glLayeredWindows` demo (a layered *transparent* window rendering over the desktop — that demo is where the repo name comes from, and it is no longer what the project is about). ~232 commits. Stated goal in `readme.md`: "Unity-like, just with a load where your coffee is still hot when it's finally done. Without an account. ... Without the scripting."

**One executable, many apps.** The Makefile builds a single `wind.exe` (`PROJECT = wind`). `main.cpp` is ~25 lines of `WinMain` that does `new APP_CLASS()` → `Start()` → `Exit()`; `APP_CLASS`/`APP_HEADER` are `-D` macros. `APP ?= Ship` at Makefile:80 selects `apps/$(APP).mk`, which sets those two macros and appends the app's own `SRCS`/`IPATHS`/`DIR_SRC`. So `mingw32-make APP=Tank` builds a tank game and `APP=OCPP` builds an EV-charger protocol tool from the same tree. A `.current_app` sentinel file is a prerequisite of `main.o` so switching APP rebuilds it (make can't otherwise see that the macros changed).

Apps (each is `ApplicationX.cpp/.h` in the repo root, most with a subdirectory of gameplay classes):
`Ship` (`ship/` — asteroids, ShipCharacter), `Tank` (`tank/` + `crane/` — TankCharacter, BuggyCharacter, Heightmap terrain, CraneCharacter), `IsoAnimation` and `Grid` (`isoterrain/` — isometric rooms/walls/stairs/roads), `Tileset` (`isoterrain/` + `isocity/`), `Dozer` (`dozer/`), `Sim` (`galaxy/` — procedural star systems, Perlin/Worley noise), `Animation`, `UI`, and `OCPP`. **`OCPP` is the odd one out**: not a game at all but an Open Charge Point Protocol (EV charging) client/server built on the engine's HTTP/TCP stack — see `docs/ocpp_*.md`.

**`core/` is the engine** (~15k lines of .cpp). Everything is hand-rolled including the math types (`type_vec3.h`, `type_quat.h`, `type_fmat4.h`, `type_plane.h`, `type_ray.h`, …). Main pieces: `Application` (2000 lines, the orchestrator), `Window`/`Renderer`/`Shader`/`Camera`/`Light`/`Material`/`Texture`/`Mesh`/`CubeMap`/`Sprite`/`Particle`, `Scene` + `Object` (the scene graph), `GLTFLoader` (1200 lines — GLB is the main asset path; `OBJLoader` is legacy), `AssetManager`/`BinaryAsset` (a two-stage `DUMP_BINARYASSETS` → `COMPILE_BINARYASSETS` build can bake every asset into the exe), `SoundSystem`/`WaveFile` (OpenAL, static), networking (`Socket`/`TCPClient`/`TCPServer`/`HTTPServer`/`MCPServer`), `core/physics/` (`Physics`, `PhysicsBody`, `PhysicsWorld` — the reactphysics3d wrapper), `core/skeleton/` (`Bone`, `Skeleton`, `PlayerCharacter`), and `Vehicle`/`Wheel`.

**Threading is the architectural fact that explains most bugs here.** `Application::Start()` spawns two threads on top of the main one:
- **Main thread** — window messages only, forwards input.
- **Frame/render thread** — runs `Init()` (so app setup happens here, not in the constructor), then `DrawFrame()` + `DrawImGuiUI()` each frame.
- **Physics thread** — `UpdateInput()` → `UpdateAnimations()` → `RunLogic()` → `UpdatePhysics()` → `NextInput()` at a fixed rate (`physics_tps = 50`, settable via `SetPhysicsTPS`).

Input is deliberately coupled to the physics rate, not the frame rate. `Object` carries a doubled `ObjectState` with `STATE_ACCESS_PHYSICS` / `STATE_ACCESS_RENDERER` so the two threads never read the same copy; setters are physics-thread-only. `Scene` has `PausePhysics`/`StepPhysics(n)`/`pending_physics_steps` (atomic) for deterministic single-stepping, and `MoveObjectOverTicks` for scripted motion. **Any code touching a body from the render or MCP thread races the physics step** — that's the root of several recorded gotchas; see [[rp3d-vehicle-constraint-plan]] for the queue-it-for-the-physics-thread pattern (`Vehicle::RequestReset`).

**An MCP server is built into the engine** (`core/MCPServer.h`, HTTP on port 8765), which is how Claude drives and inspects the running app. `Application::RegisterCoreMCPTools()` registers generic `object_list`/`object_get`/`object_set_transform`/`object_move` for every app right after `Init()`; individual apps add their own (`tank_drive`, `crane_telemetry`, …). See [[mcp-native-tools-setup]] and `docs/mcp_server.md`.

**Vendored 3rd-party** in `3rdparty/`: imgui (docking branch), reactphysics3d, tinygltf, stb_image, miniz, openal-soft — prebuilt into static `.a` files in `libs/` to keep compile times down. Header-only libraries are deliberately un-header-only'd into .cpp/.h pairs for the same reason. Built **without exceptions** (`-fno-exceptions -DJSON_NOEXCEPTION`), and the default build is the *debug* one (`-DDEBUG -Og -g`).

**How to apply:** Identify which app you're working on first — a change in `core/` affects all ten, and the app you need to rebuild/run is whatever `APP=` names (`.current_app` records the last one built). Before adding anything to `core/`, check whether it belongs in one app's subdirectory instead. When touching object state or physics, work out which thread the code runs on before anything else. `readme.md` is a long-running design journal (materials, skinned meshes, animation import, input, audio, GPU quirks) and is worth grepping before solving a problem from scratch; `docs/` holds the focused writeups.

Related: [[build-toolchain-location]] (how to build), [[rp3d-local-fork-hinge-motor-patch]] (the physics lib is a patched fork), [[rp3d-vehicle-constraint-plan]], [[animation-root-motion-rewrite]], [[tank-suspension-upright-torque-todo]].

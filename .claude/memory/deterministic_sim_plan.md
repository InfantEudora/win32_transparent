---
name: deterministic-sim-plan
description: "Agreed direction (2026-09-06): make the simulation deterministic and tick-driven, with ALL mutation entering through tick-stamped input events - MCP and the UI become 'players'. Ordered plan, known blockers, and the deliberate scope limit on cross-machine floats"
metadata:
  node_type: memory
  type: project
  modified: 2026-09-06T00:00:00.000Z
---

**Direction agreed with the user 2026-09-06.** The engine should become a deterministic lockstep simulation, the way multiplayer games are built: randomness comes from a seed, physics advances in fixed ticks, and **the only thing that mutates the simulation is input, stamped with the tick it applies to**. A session is then reproducible from a small recorded input stream — an hour of gameplay replays to the identical end state. Frame rate becomes irrelevant, and there need not be frames at all (headless runs are possible; nothing on the physics thread needs the GL context, only `Init()` currently does).

**The reframe that matters: `MCPServer` is just another "player", and so is the UI.** Neither may touch simulation state directly; both submit input through `InputController` like a keyboard does. This replaces — rather than patches — the four problems in [[threading-model]]: the MCP race, the frame-rate-vs-tick-rate mutation mismatch, the ad-hoc `Vehicle::RequestReset` / `pending_physics_steps` deferrals, and the wall-clock latches.

**Input must be a serialisable value type, not a closure.** An earlier suggestion of a `std::function` command queue was explicitly rejected for this reason: closures fix the races but cannot be recorded or replayed. POD events can.

**Migration path that avoids rewriting ten apps:** `InputController` today is a *polled state* design (`IsKeyDown`/`WasKeyReleased`/`GetDelta` over `KeyState`), but lockstep needs an *event stream*. Keep both: make the tick-stamped event stream the boundary, and **derive** the polled `KeyState` from that tick's events at the top of each tick. All existing gameplay code keeps calling `IsKeyDown(INPUT_MOVE_LEFT)` unchanged, while replay just feeds a recorded stream the sim cannot distinguish from a live one.

**Sim input vs view input must not mix**, or every camera pan perturbs a replay. Camera control, hover, selection and ImGui panel state are view-local and must never enter the stream — note `InputController` currently holds both kinds (`hovered_object`/`hovered_normal`/`hovered_position` alongside the movement keys). Sharp edge: **object picking depends on the rendered frame** (the ID-buffer readback in `CheckObjectSelection`), so "click at (x,y)" is not replayable — the resolved *object id* is the input. That is also what real multiplayer games do: the client resolves the pick locally and ships the entity id.

**Deliberate scope limit (user, 2026-09-06):** same-binary, same-machine replay is the goal and is where the value is (deterministic MCP experiments — run, change something, compare). Cross-machine/cross-architecture float agreement is explicitly NOT a goal: *"We are FAR away from our floating points behaving the same on a different architecture."* Don't propose fixed-point or strict-FP work. Design so it isn't precluded, don't pay for it.

**Blockers found, with the user's rulings:**
1. **Variable timestep.** `UpdatePhysics(1.0f / physics_tps * physics_time_factor)` scaled `dt` by the global time factor (6 call sites + a "Global Time Factor" UI slider). Ruling: **the timestep is just 50 Hz, no scaling.** Also `TankCharacter`/`BuggyCharacter::UpdatePhysicsState` each hardcode `float timestep = 0.02f`.
2. **`Vehicle` hold-latches on `GetTickCount64()`** (~15.6 ms granularity against a 20 ms tick, and real time rather than sim time, so holds break under pause/`tank_step`). `ApplyHoldLatches()` is called exactly once per tick per vehicle from each subclass's `UpdatePhysicsState`, so a plain tick countdown works.
3. **Unseeded `rand()` in sim logic** (`ApplicationDozer.cpp:214`, `core/type_helpers.cpp:125`; `ApplicationTank.cpp:1331` does `srand(1337)` but that seeds the *global* C RNG shared with any other caller). Ruling: **it should all go through `RRandom`.** Not yet done.
4. **Animation timing is sim state** since the root-motion rewrite (extracted deltas drive the capsule), so `Object::animation_time_delta` (default hardcoded `0.02f`) must be tick-derived. Ruling: *"Animation time delta there means 20ms. But should be set from 1/physics_rate."*

**Ordered plan (agreed):**
1. **DONE 2026-09-06.** Promote the physics-thread-local `physics_ticks` counter to an authoritative atomic tick on `Scene`. Everything else hangs off it. It must advance only when a tick actually runs (not while paused), so it counts sim time, not wall time.
2. **DONE 2026-09-06.** Make `dt` constant; `physics_time_factor` scales the tick *rate* (pacing), never the timestep.
3. Define the `InputEvent` POD + tick-stamped queue; derive `KeyState` from it. Give `InputController` a real lock at the same time, retiring the `InputController.h:103` TODO.
4. **DONE 2026-09-06.** Convert `Vehicle` latches to tick countdowns; keep ms at the MCP boundary (the MCP/network layer speaks milliseconds, the sim speaks ticks).
5. Route MCP through it as a player — deletes `RequestReset` and the `pending_physics_steps` decrement-ordering hack.
6. Route the UI through it.
7. Record/replay harness + state hash. **Keystone: until you can replay and compare, you cannot know whether any of the above achieved determinism.** `tools/vehicle_mcp.py`'s baseline-JSON compare is this in embryo.

Steps 3, 5, 6 are the real refactor and are still open; 7 is the keystone.

**What landed on 2026-09-06 (steps 1, 2, 4 + blocker 4):**
- `Scene::GetPhysicsTick()` — `std::atomic<uint64_t>`, incremented at the very END of a tick that actually ran, for the same reason `pending_physics_steps` is decremented there. Two dead counters (a `Scene::physics_ticks` member and a `PhysicsThreadFunction` local feeding a commented-out log) were consolidated into it.
- `Application::GetPhysicsTimestep()` — `1.0f / physics_tps`, constant, now the single source for all 7 `UpdatePhysics`/`UpdateAnimations` call sites across the apps. `physics_time_factor` moved to the pacing line in `PhysicsThreadFunction` (`us_looptime_desired = physics_us_per_tick / max(factor,0.01f)`, recomputed per loop) and its UI slider is relabelled "Time Factor (tick rate)".
- `Scene::GetPhysicsTimestep()` + `Object::physics_timestep`, pushed to each object before `UpdatePhysicsState()`. This retired the hardcoded `float timestep = 0.02f` in `TankCharacter`/`BuggyCharacter::UpdatePhysicsState` without touching the 13-class `UpdatePhysicsState()` virtual signature.
- `Object::animation_time_delta` is refreshed from the sim timestep every tick by `Scene::UpdateAnimations(float)`, with `f_animation_time_delta_override` (set by the debug slider, cleared by a "Follow tick rate" button) so the drag isn't overwritten. Note the pre-existing, differently-purposed `f_animation_override` next to it.
- `Vehicle` latches are `gas/brake/steer_latch_ticks` countdowns decremented in `ApplyHoldLatches` (called exactly once per tick per vehicle). `GetTickCount64` is gone from `Vehicle`. `ApplicationTank::DurationMsToTicks` / `TicksToRealMs` convert at the MCP boundary (rounding up, so a sub-tick duration still gives one tick).

**Verified:** all 10 apps build except `Grid`, which was ALREADY broken at HEAD by the animation rework (`PlayerCharacter::SetNextAnimation` no longer exists) — unrelated and untouched. `ApplicationUI` and `ApplicationGrid` had long-stale no-arg `main_scene->UpdatePhysics()` calls that could never have compiled; both fixed in passing, so `UI` builds again. `tools/vehicle_mcp.py` baselines for BOTH tank and buggy come back **bit-identical** to `tools/baseline_rp3d_*.json` (82 fields, 0 differing; `coast_distance` matching to 17 significant figures) — the refactor is behaviour-preserving.

**Already in the codebase's favour:** the tick is already fixed at 50 Hz and not frame-coupled; rp3d 0.10 is single-threaded and is a fork the user controls ([[rp3d-local-fork-hinge-motor-patch]]); `Object::GenerateUniqueID()` is a plain `object_ids++` counter (deterministic given deterministic creation order, no addresses or hashing); `RRandom` is already designed around a seed index into a noise texture.

Related: [[threading-model]], [[project-overview]], [[rp3d-vehicle-constraint-plan]].

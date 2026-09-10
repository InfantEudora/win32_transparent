---
name: threading-model
description: "How the render/physics/input threads actually synchronise: one coarse Renderer::physics_mutex, the unused ThreadSafeQueue the readme designed instead, and the four places that arrangement bites (MCP, InputController, frame-rate mutation, wall-clock latches)"
metadata:
  node_type: memory
  type: project
  modified: 2026-09-06T00:00:00.000Z
---

The three threads described in [[project-overview]] synchronise with **one coarse mutex, `Renderer::physics_mutex`** (`core/Renderer.h:62`) — not with the input queue `readme.md` designs. It is taken in exactly four places:

- render thread: around `renderer->DrawFrame()` (`Scene.cpp:145`), around the physics debug-mesh rebuild (`Scene.cpp:86`), and around the **whole** `DrawImGuiUI()` call (`Application.cpp:237`, whose comment reads *"This will access and modify physics, globally... all over the place."*)
- physics thread: around `UpdateAnimations() → RunLogic() → UpdatePhysics()` (`Application.cpp:269-277`)

**Consequence: UI code mutating physics is NOT a data race.** ImGui is immediate-mode, so button/slider interaction is registered while the UI is being built, and the base `Application` UI freely calls `SetPosition`/`SetRotation`/`AddObject`/`Destroy()` from the render thread (`RenderSelectedObjectUI` and friends). That is safe *only* because the physics thread is locked out for the entire UI build. Don't "fix" it as if it were a race.

`core/TQueue.h` contains a complete, correct `ThreadSafeQueue` that **is referenced by nothing in the repo** — the deferred-command mechanism the readme specifies ("these 2 inputs need to be queued as an input, and processed next time the input gathers it's information") was written and never wired up. Ad-hoc replacements grew in its place (`Vehicle::RequestReset`, `Scene::pending_physics_steps`).

**Where the arrangement actually bites:**

1. **The threads alternate rather than overlap.** The lock is held across the entire render pass *and* the entire UI build, so physics cannot tick during either. The physics loop's adaptive sleep (`us_sleep = us_looptime_desired - us_loop`) silently absorbs the stall, so a heavy frame or a slow UI action (loading a GLTF behind a button) becomes physics jitter instead of a visible frame drop.
2. **MCP handlers ignore the lock entirely.** `MCPServer`'s `m_toolsLock` guards only the tool registry; handlers run on the MCP/HTTP thread and touch the simulation with no `physics_mutex`. **Any new MCP tool that writes simulation state must defer to the physics thread** (the `Vehicle::RequestReset` / `pending_physics_steps` pattern), not mutate directly. Note the ordering subtlety already solved in `Scene::UpdatePhysics`: `pending_physics_steps` is decremented *after* the tick, because a caller polling for 0 immediately touches the sim.
3. **`InputController` races — FIXED 2026-09-06.** `UpdateInput()`/`NextInput()` ran outside the lock while three threads touched `InputController` (window thread writing via `HandleMessage`, physics thread polling and clearing edges, render thread reading in the UI), with only hand-picked atomics and an open "How to atomicise this?" TODO. Now: one `state_mutex` guards the cross-thread handoff (pending event queue + `WindowInputState`), `UpdateInput()` moved inside `physics_mutex`, `NextInput()` kept after the sleep but wrapped in its own lock, and the wheel goes through the event queue instead of a raw cross-thread write. Lock order is always `physics_mutex` → `state_mutex`. `KeyState` stays lock-free (physics thread only). See [[deterministic-sim-plan]].
4. **Frame-rate vs tick-rate mutation.** UI is built at frame rate, physics ticks at 50 Hz, so a held slider mutates ~1.5x per tick at 75 FPS with jitter. Fine for a teleport, wrong for anything rate-based (impulses, forces, input latches).

**Wall-clock latches were a live instance of #4 — FIXED 2026-09-06.** `Vehicle`'s hold-latches used to expire against `GetTickCount64()`, which is ~15.6 ms granular against a 20 ms tick and measures **real** time while the sim can be paused or single-stepped, so holds silently meant 4/5/6 ticks between runs and expired before `tank_step` had advanced anything. They are now `*_latch_ticks` countdowns decremented once per tick in `ApplyHoldLatches`, with `ApplicationTank::DurationMsToTicks` converting at the MCP boundary. See [[deterministic-sim-plan]]. **The general rule stands: nothing inside the simulation may measure duration in wall-clock time** — use `Scene::GetPhysicsTick()`. `PerfTimer` wraps `QueryPerformanceCounter` where real elapsed time genuinely is wanted. (`ApplicationOCPP`/`OCPPClient` still use `GetTickCount()` — legitimate there, real-world protocol timeouts, though the 32-bit variant wraps at 49.7 days.)

**RESOLVED 2026-09-10 — `ObjectState` is now single.** The user's 2026-09-06 read was right and the whole triple buffer is gone: `Object` had `state` / `state_physics` / `state_physics_prev` plus three atomics, of which `state_completed` and `state_physics_completed` were written and never read, and the `PhysicsCompleted()` handshake in `Renderer::UpdateState` had both of its acting branches commented out. Since the physics tick and `Renderer::DrawFrame` both hold `physics_mutex`, the mutex already provided everything the second copy was for. Deleted: `Object::UpdateState`, `Object::PhysicsCompleted`, `Renderer::UpdateState`, the `ObjectStateAccessType` enum and its argument on 8 accessors (92 call sites across 13 files). Two consequences worth knowing: the renderer now reads live state instead of a tick-old copy (so `Hide()`/`SetPosition` land the same frame), and `GetWorldPosition` kept only the matrix-chain implementation - the old `STATE_ACCESS_PHYSICS` branch composed parent rotation only and silently **dropped ancestor scale**, so bone chains carrying glTF node scale are the place to look if a world position reads differently than before.

**How to apply:** Before touching object or physics state, work out which thread the code runs on. Render/UI path — already covered by `physics_mutex`, leave it. MCP path — defer it. Anything rate-based — do it per physics tick, not per frame or per millisecond of wall clock. The user's own remark on how the MCP layer got here: it was added by Claude agents without this overview existing first, which is how it ended up bypassing the lock. Prefer wiring `TQueue.h`'s existing `ThreadSafeQueue` into a real deferred-command buffer (drained at the top of `Scene::UpdatePhysics`, before `AdvanceObjectMotions`) over inventing another per-feature deferral.

Related: [[project-overview]], [[rp3d-vehicle-constraint-plan]], [[mcp-native-tools-setup]].

**SUPERSEDED IN PRACTICE (2026-09-10).** The advice above - that UI mutation is safe under the
coarse lock while MCP handlers must defer - is still true about LOCKING, but it is no longer how
the code works. Both now go through the SimCommand queue: every mutation of simulation state from
either thread is submitted and applied on the physics thread at the top of a tick. See
[[deterministic-sim-plan]] steps 5 and 6. The rule to carry forward is the one that bit us: the UI
must SUBMIT and never WAIT (DrawImGuiUI holds physics_mutex, the physics thread needs it to drain),
whereas an MCP handler holds no locks and may wait for its command to land.

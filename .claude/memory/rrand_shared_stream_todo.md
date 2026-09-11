---
name: rrand-shared-stream-todo
description: "TODO: one RRandom is shared by the sim and by the UI/MCP threads, so any off-tick draw both races `state` and shifts every later draw - a determinism hole for the record/replay step"
metadata: 
  node_type: memory
  type: project
  modified: 2026-09-10T13:56:41.641Z
  originSessionId: 8536c1be-600c-4964-8a20-e1b5bd5b6f80
---

**OPEN, no design agreed yet.** Each app has one `RRandom` (`Application::rrand`), and it is a
stateful PRNG - a single `uint32_t state` advanced per draw, plus `spare`/`hasspare` for the normal
distribution. The simulation draws from it on the physics thread (e.g. `Asteroid`'s model pick),
and the debug UI and MCP tools draw from the SAME instance on their own threads.

Two consequences:
1. A draw off the physics thread **shifts the stream** for every later draw, so the sim stops being
   reproducible from inputs + commands alone - which is exactly what step 7 of
   [[deterministic-sim-plan]] needs.
2. From the MCP thread it is also an unsynchronised read/write of `state`. (The debug UI is at
   least serialised, since `DrawImGuiUI` holds `physics_mutex`.)

Live example: ApplicationShip's "Add Asteroid"/"Add Pickup" buttons roll position, scale and
velocity on the render thread. The roll RESULTS travel in the SimCommand, so a replay reproduces
that asteroid rather than re-rolling - but the draw itself still moved the stream. The
`asteroid_spawn` MCP tool dodges it by spacing multiple spawns deterministically instead of
scattering them, which is a workaround, not a fix.

Sketched, not chosen: a separate generator for non-sim callers (tooling randomness never touches
the sim's stream), or deriving draws from the tick number so a draw's position in the stream stops
mattering. User's words on 2026-09-10: "That rrand not consuming is a good one. I'd have to give it
some thought." So do NOT implement either without asking.

Related: [[deterministic-sim-plan]], [[threading-model]], [[project-overview]].

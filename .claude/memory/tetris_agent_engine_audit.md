---
name: tetris-agent-engine-audit
description: "DONE 2026-09-11: APP=Tetris built and playable, findings at docs/tetris_findings.md; 3 core files patched, the important one being the HasSyntheticHolds release-tick fix"
metadata: 
  node_type: memory
  type: project
  originSessionId: c6201c2c-5e1b-437a-9ea1-cbd19ced8142
  modified: 2026-09-11T13:41:32.960Z
---

Agreed 2026-09-11: a separate agent builds a classic game (Tetris) on this engine as `APP=Tetris`,
to find out what a real game needs that the engine lacks. The brief is in-repo at
`docs/tetris_agent_brief.md` — read that first, it documents the whole core API surface.

Ground rules the user chose, which are NOT derivable from the brief's existence:
- The agent MAY patch `core/`, but every change must be logged in a "Changes to core/" section.
- Scope is deliberately stretched to exercise physics, sound and animation, not just rendering.
- The agent may kill a running `wind.exe` to take over the binary and port 8765.
- Known weak spots were disclosed up front rather than left to discover (see brief §8).

**COMPLETED 2026-09-11.** Game is playable (brief steps 1-9; step 10, the skeletal-animation
probe, deliberately not started). Report is at `docs/tetris_findings.md` — that is the
deliverable, not the game. `tools/tetris_bot.py` plays it through MCP; soak run 250 pieces /
99 lines / level 10.

Two things from it worth carrying forward regardless of Tetris:

1. **`InputController::HasSyntheticHolds()` used to go false on the exact tick a scripted release
   was readable**, so the focus gate the brief recommends silently dropped EVERY edge-triggered
   scripted action (rotate, fire, hard drop) while held ones kept working. Patched in core;
   `ApplicationShip.cpp:1264` has the same gate and was one tapped control away from the same
   bug. Remember this one — it defeats any MCP- or replay-driven test.
2. **`RRandom` cannot be seeded at all**, which is worse than the shared-stream problem
   [[rrand_shared_stream_todo]] records: `state` is uninitialised in the constructor, `SetSeed`
   writes a `seed` member nothing ever reads, and `rnd_texture` is a process-wide static. So
   "give it its own instance" does not work; the Tetris bag uses a private xorshift32 instead.

Also patched: `Renderer::AddMaterial` now returns the matching index for an existing name.
`APP=Grid` does not build, and did not before this work (ApplicationGrid.cpp still calls the
removed `SetNextAnimation` — see [[animation_root_motion_rewrite]]).

MCP over HTTP: use `127.0.0.1`, never `localhost` — the server binds IPv4 only, so `localhost`
costs ~2s per call on this machine instead of ~15ms.

Related: [[project_overview]], [[threading_model]], [[deterministic_sim_plan]],
[[animation_root_motion_rewrite]], [[rrand_shared_stream_todo]], [[build_toolchain_location]]

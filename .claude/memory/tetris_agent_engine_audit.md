---
name: tetris-agent-engine-audit
description: "A second agent is building APP=Tetris as a deliberate audit of the engine; brief lives at docs/tetris_agent_brief.md, findings due at docs/tetris_findings.md"
metadata: 
  node_type: memory
  type: project
  originSessionId: c6201c2c-5e1b-437a-9ea1-cbd19ced8142
  modified: 2026-09-11T12:52:46.370Z
---

Agreed 2026-09-11: a separate agent builds a classic game (Tetris) on this engine as `APP=Tetris`,
to find out what a real game needs that the engine lacks. The brief is in-repo at
`docs/tetris_agent_brief.md` — read that first, it documents the whole core API surface.

Ground rules the user chose, which are NOT derivable from the brief's existence:
- The agent MAY patch `core/`, but every change must be logged in a "Changes to core/" section.
- Scope is deliberately stretched to exercise physics, sound and animation, not just rendering.
- The agent may kill a running `wind.exe` to take over the binary and port 8765.
- Known weak spots were disclosed up front rather than left to discover (see brief §8).

The real deliverable is `docs/tetris_findings.md`, not the game. Check it exists before treating
the task as done.

Related: [[project_overview]], [[threading_model]], [[deterministic_sim_plan]],
[[animation_root_motion_rewrite]], [[rrand_shared_stream_todo]], [[build_toolchain_location]]

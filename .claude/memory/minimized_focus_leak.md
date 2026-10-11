---
name: minimized-focus-leak
description: "OPEN 2026-10-11 - a --minimized chasm once reported input_focused true and took keys typed in VS Code (new map, tool, unpause, pan); cause not found"
metadata:
  node_type: memory
  type: project
  originSessionId: 6bcdca7d-dfbb-495a-ba72-ee961d202a1e
  modified: 2026-10-11T09:53:07.200Z
---

2026-10-11: chasm started with `--minimized --mcp-port 8769`, VS Code in front (GetForegroundWindow = Code,
IsIconic = true), yet `sim_pause` reported `input_focused: true` and the user's typing drove the game: unpaused,
H took the winch tool, WASD panned, N generated a new map. A second start behaved (`input_focused: false`).
Core's fix (SyncFocusWithWindow at startup, core/Application.cpp) was in that exe; only WM_ACTIVATE /
WM_SETFOCUS set focus true, so something activated the minimised window once. Not investigated further - reported
to the user as a core bug.

**Why:** CLAUDE.md promises --minimized is safe at a shared desk; this breaks it silently.
**How to apply:** after starting a minimised app, check `sim_pause`'s `input_focused` is false before relying on
its state; if a run shows unexplained camera moves or a new map, suspect this. No key writes saves (F9 only
starts a recording). Related: [[running-app-is-user-driven]], [[chasm-person-picking]].

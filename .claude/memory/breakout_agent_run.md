---
name: breakout_agent_run
description: "Second game-build audit run - Breakout brief written 2026-09-12, agent started, findings doc still pending"
metadata: 
  node_type: memory
  type: project
  originSessionId: 33d61487-8b62-4e74-88b4-d8eb830effc3
  modified: 2026-09-11T22:15:29.375Z
---

The engine is being audited a second time by having an agent build a game on it, following the
Tetris pattern. Brief written 2026-09-12 at `docs/breakout_agent_brief.md`; the building agent was
started the same day. Deliverables: `APP=Breakout`, at least one self-written custom shader effect
through `Renderer::AddCustomShader`, and `docs/breakout_findings.md`.

**Why:** the first run ([[tetris_agent_engine_audit]]) uncovered enough bugs and gaps to fill
`docs/engine_backlog.md`, so it was repeated with a different genre and a fresh perspective.

**How to apply:** the Breakout agent was told NOT to read the Tetris markdown docs (they are
partly out of date and an independent second look is the point), but may read all code. Its
findings doc is the thing to fold into `docs/engine_backlog.md` when it lands - check whether that
has happened before assuming the backlog is current. Core patches by that agent are allowed and
are logged in its own findings under "Changes to core/".

---
name: mcp-errors-are-json
description: "An app's MCP tool refuses with {\"error\": ...} in a normal result, not a failure - sim_step unpaused silently does nothing in a script"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 3d6f80ce-bdc3-46fa-b61d-5a5e4623ce6f
  modified: 2026-10-26T20:38:54.050Z
---

A refused MCP tool call comes back as an ordinary result with an `error` key, never as a failed call. `sim_step`
without `sim_pause {"paused": true}` first returns `{"error":"simulation is not paused ..."}`, so a script that ignores
the result runs the game in real time and only appears to step.

**Why:** On 2026-10-26 this cost a whole test round: the swamp-huts wave "never opened" because no step had run. I
began debugging correct code before noticing the steps had been refused.

**How to apply:** Test helpers raise on an `error` key (`if isinstance(out, dict) and out.get("error"): raise`). Pause
first, then step, and read the tick before trusting a stepped result. Related: [[chasm-test-instance-autosaves]].

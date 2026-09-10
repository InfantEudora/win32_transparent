---
name: running-app-is-user-driven
description: "A wind.exe instance launched during a session is shared with the user - unexplained motion in MCP telemetry is usually them playing, not a bug"
metadata: 
  node_type: memory
  type: feedback
  modified: 2026-09-10T11:09:30.233Z
  originSessionId: 8536c1be-600c-4964-8a20-e1b5bd5b6f80
---

When `wind.exe` is running and being inspected over MCP, the user is often flying/driving it at the
same time - the window is on their screen, not headless. On 2026-09-10 a newly added hinged door
kept getting kicked to its angle limit at random intervals; that was the user ramming it with the
ship, and the "intermittent spontaneous impulse" being chased did not exist.

**Why:** MCP telemetry looks like a controlled measurement but it is a live shared session, so any
object the user can reach is an uncontrolled input.

**How to apply:** Before treating unexplained motion in a running instance as a bug, ask whether the
user is at the controls, or measure something they cannot influence (physics paused via
`sim_command`/`StepPhysics`, or a body far from anything drivable). Say what was observed and ask -
don't spend a long investigation on it first.

Related: [[project-overview]], [[mcp-native-tools-setup]], [[threading-model]].

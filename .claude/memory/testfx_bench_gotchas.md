---
name: testfx-bench-gotchas
description: "Driving apps/testfx over MCP - uniform values leak across effects and reloads, sim_step has a 30 s wall, and the default camera is above the cube"
metadata: 
  node_type: memory
  type: project
  originSessionId: e358216b-2c1e-474a-b9e7-128d949795d0
  modified: 2026-09-13T16:21:45.720Z
---

Three things that cost a round each while tuning `bluecube2.frag` on 2026-09-13:

- **The bench's uniform value map used to outlive the effect** (FIXED 2026-09-13 in
  ApplicationTestFX::BuildEffect - a switch now clears the map, a reload still keeps edits).
  Before that, `fx_set` values and even the previous effect's DEFAULTS carried over to a
  *different* effect declaring a uniform of the same name (`frost`, `exposure`, `glow_color` came
  from bluecube.frag into bluecube2.frag, and a restart did not help because bluecube compiles
  first). If a testfx.exe predating that fix is running, `fx_set` every shared name explicitly.
- **`sim_step` runs at tick rate, not instantly.** 750 ticks at 120 TPS took ~6 s; 3750 ticks
  timed out a 30 s HTTP client but completed in the app. Step in chunks or raise the timeout.
  The argument is `num_ticks`, not `ticks`.
- **The bench default camera is above the cube** (0.48, 0.26, 2.65). The Spiritbox reference
  frames are shot from low; `camera_set position [0.35,-0.42,2.45] look_at [0,-0.12,0]` is
  the view bluecube2 was matched at (also written in that file's header).

**Why:** none of these are in docs/testfx_plan.md as gotchas, and the first one looks exactly
like "my defaults are wrong".

**How to apply:** on a current build, `fx_select` gives the file's defaults; verify with
`fx_state` before judging a screenshot anyway. Scratch MCP driver used:
a 60-line urllib script (`fx.py call/shot/set`) - nothing in tools/ speaks HTTP yet.
See [[shell-heredoc-limit]] for why the PIL sampling ran as a separate python call.

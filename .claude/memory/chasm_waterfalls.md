---
name: chasm-waterfalls
description: "Chasm falls rebuilt 2026-10-12 - brink + welded sheet, flow-time UVs, stage 2 extras; how to look at them (camera, shader reload)"
metadata:
  node_type: memory
  type: project
  originSessionId: d3d22ef3-3b14-43a8-ac97-3d179fa5960f
  modified: 2026-10-12T21:10:05.833Z
---

Chasm's waterfalls were rebuilt 2026-10-12 (agreed with the user, both stages built): `apps/chasm/docs/waterfall_plan.md`.
The river runs out to a straight BRINK past every rock; the sheet's top row is the river's own points/UVs (welded, no
seam); uv.y is the river's along in flow time so the streaks pour over. Stage 2: bow, inner layer, strands, wet rock
(chasm_ground.frag `Wetness`), spray (`SprayBall`). `WaterMeshData::shapes` feeds foam, spray and wet rock.

**Why:** the old sheet started on the layout's rim LINE, not the jagged plot rim, so it never met the river.

**How to apply / gotchas when checking water visuals in chasm:**
- The game camera never goes below the rim: `camera_set` is overridden by chasm's orbit camera, and `chasm_view`'s
  target is clamped to plateau height. Look at a fall's foot from high and steep (pitch 55-80).
- Chasm does not hot-reload shaders - restart the app after editing a .frag/.glsl (no rebuild needed).
- A river's line runs 10 units past its lip; anything keyed on `RiverDip` near a fall can hit lower ground
  (the bank-colour bug fixed in `TerrainMesh.cpp` `Fan`).
- With another agent on chasm, run a COPY of the exe on its own port (`--mcp-port`), claim `#port:N`, delete the copy after.

Related: [[chasm-game-plan]], [[chasm-cliff-kit]], [[lock-claim-before-build]]

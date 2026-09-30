---
name: archer-model-scale
description: "archer's model_scale is ~1.82 (measured 2026-09-30), not the 2.02 an old comment in BuildArcherModel implies; read it off the running app before sizing rules numbers from glb extents"
metadata:
  node_type: memory
  type: project
  originSessionId: 2f8217e8-79fc-46b3-b503-6a851420025d
  modified: 2026-09-30T15:45:48.936Z
---

archer.glb pieces are drawn at `model_scale` = ARCHER_MODEL_HEIGHT / rig height, which measured about 1.82 on 2026-09-30 (bigtree_segment half-width 0.281 -> 0.512). The comment about Toe_End ("0.0254 x 2.02") in BuildArcherModel is stale.

**Why:** I sized the roof bigtree's Stage.h constants with 2.02 and every one came out ~11% too big; the app's load-time drift warning caught it.

**How to apply:** when turning glb extents into rules numbers, take the scale from the running app (a load log line, or measure one known piece), or put a measure-and-warn check in the app the way BuildPlantModels / BuildScenery do. See [[archer-app]], [[plant-mechanics-plan]].

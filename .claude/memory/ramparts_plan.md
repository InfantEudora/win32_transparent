---
name: ramparts-plan
description: Chasm layered walking (ramparts_plan.md) - walk graph nodes are plot+layer; tower decks and a debug castle built 2026-10-10; user judging readability
metadata:
  node_type: memory
  type: project
  originSessionId: 9c1efb04-f5be-4246-803d-f0b9ad53ecb3
  modified: 2026-10-23T21:01:03.749Z
---

apps/chasm/docs/ramparts_plan.md, agreed and built 2026-10-10 (steps 1 and 2). Walls people walk on, roads under
them, Stronghold-style.

- **Walk graph nodes are (plot, layer)**: layer in half storeys (even flat deck, odd a stair's middle). Decks are
  extra A* nodes after the plots (`Walkers::EnsureDecks`, rebuilt per zones version). `WalkNodeLayer` /
  `WalkJoin` in Walkers.h are THE rule - the castle mesh asks the same function, so drawing and walking agree.
- **A worker's height comes from `EconomyWorker::route_layer`** (one per route point, empty = all ground), not
  from the plot under him. Any new place that cuts a route must keep it in step (`StopWhereHeIs`).
- **Who walks**: tower decks anyone's; castle decks only the posted (guards/orders) - the user's call, and the
  hook zombies' own map will use.
- **Debug castle**: `ZoneState::castle`, `ZONE_OP_CASTLE` (refused in play), panel tool "Castle (prototype)",
  `chasm_paint op castle shape storeys`. Test scene: `apps/chasm/saves/ramparts_test.json` (seed 12, wall at
  z -12 across a road at x 244).
- **Arch over a piece** (`castle_over`, user-agreed): headroom >= 2 storeys; routing to such a plot ends on the TOP
  deck (user's call), lower deck still walked through. Up to `WALK_MOST_DECKS` (2) decks a plot. Scene:
  `saves/ramparts_crossing.json` (storey-1 wall under a storey-3 wall at (244, 8)).
- **Stair on an arch**: `castle_over` holds a whole piece; a stair 1 storey over an arch sits on it (covers its
  deck), climbing over the road. Scene: `saves/ramparts_stair_arch.json`.
- **Surface picking**: chasm's terrain + zone chunks are PICKABLE now (`surface_ids`), so the engine's G-buffer
  readback gives position/normal at the mouse; `PickUnderMouse`/`PickSurface` -> plot (inset along normal) + layer
  (`SurfaceLayer`). Orders carry the layer (`post_layer`, CHASM_CMD_ORDER value[1] = layer+1). Test the mapping with
  `chasm_pick surface:[x,y,z] normal:[..]`, or since 2026-10-11 `chasm_select under/press` at a pixel - they pin the engine's pick pixel ([[chasm-person-picking]]).
- Selected person's route shown as dots + ring (`BuildPathMarks`, Select.cpp).
- **Levels, as terraces (2026-10-23, user's ask)**: castle layers count from a per-plot LEVEL in whole storeys
  (`castle_base`, `ZoneCastleBase`); a drag is one level (`ZoneCastleLevelFor`). Joins compare ABSOLUTE heights
  (`WalkNodeHalves`, `WalkJoin` returns the meet in half storeys or `WALK_NO_JOIN`), so a deck level with a terrace's
  top or the hill joins it; heights drawn via `BuildingLayerHeight`/`BuildingMeetHeight`. Saves: castle `[plot, piece, level]`.
- User verdict 2026-10-10: "it reads very well".

**Why:** prototype to judge whether stacked walkable layers READ from the camera before weaving it into play.
**How to apply:** next steps (ramparts on walls, gatehouse, storeys for archer reach) wait on the user's verdict.
Related: [[chasm-defence-plan]], [[chasm-game-plan]].

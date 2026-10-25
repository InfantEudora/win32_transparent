---
name: ramparts-plan
description: Chasm layered walking + debug castle (ramparts_plan.md) - plot+layer walk nodes; real arches and a debug stone guard tower built 2026-10-24; aim is a king's castle
metadata:
  node_type: memory
  type: project
  originSessionId: 9c1efb04-f5be-4246-803d-f0b9ad53ecb3
  modified: 2026-10-25T22:23:28.795Z
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
- **Real arch (2026-10-24, view only)**: round openings on every side, piers at corners, a groin vault, voussoir ring +
  keystone where the opening faces open ground. One rise for all sides (`CASTLE_ARCH_RISE`) so barrels meet. CastleMesh.cpp.
- **Stone guard tower (2026-10-24, user: DEBUG ONLY for now; cost stone + some wood)**: the guard tower with
  `ZoneBuilding::f_stone` (`ZoneTowerStone`, `ZONE_OP_TOWER_STONE` refused in play), placed by the Castle tool's 4th
  choice "Tower"; `BuildStoneTower` (shaft, guard room with windows/doorway onto a level castle deck, red pointed roof);
  guards hidden inside (`InStoneTower`). Test save `saves/castle_towers_test.json`. Goal context: a villager becomes KING
  and lives in a castle (user's long-term aim).
- User verdict 2026-10-10: "it reads very well".
- **Castle task (2026-10-25): `apps/chasm/docs/castle_plan.md`** - wanted: stone tower merged into walls, battlements +
  corner seams, a MINIMAL keep (the castle is for exploring Townscaper tiling, not the stronghold), gatehouse, armoury
  (needed by the ballista); other buildings are open suggestions.
- **Stone tower steps 1-2 BUILT 2026-10-25**: guard tower's rules, castle geometry - whole tile, `ZoneTowerLevel`, 2-4
  storeys, a doorway at every floor a deck meets (`WalkJoin`/`StoneTowerFloors`), drawn by `BuildStoneTowerQuarter` from
  `BuildCastleCell`. User's rules: 3-cell plot = crenellated top, else roofed guard room; ground door kept (move it to the
  INSIDE face later, once "inside the walls" exists); emergent towers from tall blocks noted for later. Castle quarters now
  meet at `BuildingWallCrossing`. Save `saves/castle_tower_heights_test.json`.
- **Step 3 BUILT 2026-10-25**: corbelled battlements, plinth, arrow slits; drawn per CELL by `Outlines`/`OutlineJoin`
  (mitred corner/inside/straight/end) - the corner seams were unmitred per-half-edge parapets. No battlements against a
  wall a sill over the deck (`StandingTop`). Next on the castle: keep, gatehouse, armoury (castle_plan.md).

**Why:** prototype to judge whether stacked walkable layers READ from the camera before weaving it into play.
**How to apply:** next steps (ramparts on walls, gatehouse, storeys for archer reach) wait on the user's verdict.
Related: [[chasm-defence-plan]], [[chasm-game-plan]].

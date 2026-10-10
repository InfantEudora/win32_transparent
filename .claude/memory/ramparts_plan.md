---
name: ramparts-plan
description: Chasm layered walking (ramparts_plan.md) - walk graph nodes are plot+layer; tower decks and a debug castle built 2026-10-10; user judging readability
metadata:
  node_type: memory
  type: project
  originSessionId: 9c1efb04-f5be-4246-803d-f0b9ad53ecb3
  modified: 2026-10-10T19:46:44.945Z
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
- User verdict 2026-10-10: "it reads very well".

**Why:** prototype to judge whether stacked walkable layers READ from the camera before weaving it into play.
**How to apply:** next steps (ramparts on walls, gatehouse, storeys for archer reach) wait on the user's verdict.
Related: [[chasm-defence-plan]], [[chasm-game-plan]].

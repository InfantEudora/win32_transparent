---
name: chasm-terraces-plan
description: apps/chasm/docs/terraces_plan.md - foundations (stage 1) and terraces (stage 2) BUILT 2026-10-22; ZoneGroundAt + ZoneTerraceStep are the single rules
metadata:
  node_type: memory
  type: project
  originSessionId: 69bfd5e2-d3af-47c9-869a-da8e0eaa395a
  modified: 2026-10-22T08:43:53.470Z
---

Terraced hill towns for Chasm, `apps/chasm/docs/terraces_plan.md`.

The user decided (2026-10-21/22):
- build up only, never dig;
- foundations are automatic, with their stone shown;
- at most one storey of rise with a foundation;
- terraces are painted, their top is the highest ground rounded up to a whole storey, and a drag that starts on a terrace extends it;
- roads crossing a retaining wall of up to a storey become stairs;
- everything may stand on a terrace;
- zombies keep off terraces for now.

Stage 1 BUILT 2026-10-22:
- `ZONE_RISE_FOUNDATION`;
- the amber slope grid;
- stone plinths;
- `DoorSteps`.

Stage 2 BUILT 2026-10-22:
- per-plot `ZoneState::terrace` (top in storeys) and `terrace_built`;
- `ZONE_OP_TERRACE`, `_ERASE`, `_RAISE`;
- `TerraceMesh.cpp`;
- the tool (key Y), unlocked with the stonecutter;
- unbuilt terraces are ground sites whose good is stone.

I changed one decision myself and the user may overrule it: terraces are painted **by coarse square** (`ZoneCellPlots`) so fields fit. `saves/terraces_test.json` is the test save.

**Single definitions, use them in new code:**
- `ZoneGroundAt` / `ZoneGroundHeight`: the ground a plot has (a terrace's top, or the terrain). Anything placing something on a plot must ask these, not `Terrain::GroundHeight` or `ground[]`.
- `BuildingFootHalves`: a building's floor.
- `ZoneTerraceStep`: open / wall / stair, asked by the walkers' edges and by straight walks.

**Why:** the user wants Greek-style terraced cities on the hilly chasm side.

**How to apply:**
- Open items for the user: a road over a wall taller than a storey reads as broken; whole storeys mean almost a storey of fill on flat ground.
- Not yet terrace-aware: picking a terrace site's card, `CrateMiddle`, sight lines.
- Stage 3 (an upper-storey door onto the terrace above) is next.

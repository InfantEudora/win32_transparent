---
name: chasm-terraces-plan
description: apps/chasm/docs/terraces_plan.md - foundations, terraces (2026-10-22) and upper doors (stage 3, 2026-10-23) BUILT; ZoneGroundAt + ZoneTerraceStep + ZoneUpperDoor are the single rules
metadata:
  node_type: memory
  type: project
  originSessionId: 69bfd5e2-d3af-47c9-869a-da8e0eaa395a
  modified: 2026-10-25T20:12:38.278Z
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

I changed one decision myself and the user may overrule it: terraces are painted **by coarse square** (`ZoneCellPlots`) so fields fit. `saves/terraces_test.json` is the test save; its three terraces are walled all round (no stair saved), so nothing walks onto them without a road stair or an upper door.

Stage 3 BUILT 2026-10-23 (user: own people walk through, onto open terrace ground, upper doors only - flat roofs and view appeal wait):
- `ZoneUpperDoor` + `ZONE_STEP_DOOR`; one door a level per plot;
- a building is walked inside by a walk to or from it (any of its plots) - a general change in `Walkers::EdgeOpen`;
- upper doors always drawn (`BuildingUpperDoorAt`); figures drawn through them via `RouteDoorLeg` (view only).

Look reworked 2026-10-25 (user asked: n-gon not star, more retaining-wall feel, slant, decoration):
- outline now `BuildingWallCrossing` (house n-gon); road stairs, road ends and `RouteHeight` follow the crossing;
- user CHOSE: batter with the FOOT spreading out onto the lower ground (not the top pulling in); footing, capstones,
  quoins, buttresses, creepers (+ tufts). Not chosen: weep holes / course lines.
- Check: `saves/terraces_test.json` + a top-1 terrace painted at (162.08, 6.25), a 6-cell point.

**Single definitions, use them in new code:**
- `ZoneGroundAt` / `ZoneGroundHeight`: the ground a plot has (a terrace's top, or the terrain). Anything placing something on a plot must ask these, not `Terrain::GroundHeight` or `ground[]`.
- `BuildingFootHalves`: a building's floor - now in Zones.cpp/.h (with `BuildingWallCrossing`), since the walkers ask it.
- `ZoneTerraceStep`: open / wall / stair / door, asked by the walkers' edges and by straight walks.
- `WallDoor` (BuildingMesh.cpp): whether a wall has a drawn door and on which storey. Never across a WALL, into a building, or off a terrace's edge. `BuildingSureDoor` gives every building that isn't shut in at the ground a ground-floor door (2026-10-23, after the user saw a door inside a terrace).

**Why:** the user wants Greek-style terraced cities on the hilly chasm side.

**How to apply:**
- Open items for the user: a road over a wall taller than a storey reads as broken; whole storeys mean almost a storey of fill on flat ground.
- Not yet terrace-aware: picking a terrace site's card, `CrateMiddle`, sight lines.
- Left for later: flat roofs as the terrace above, appeal for a view.

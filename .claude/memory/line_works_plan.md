---
name: line-works-plan
description: "AGREED 2026-10-06: chasm roads + walls as drag-previewed lines built by idle villagers who fell trees in the way; steps 1-4; waiting on window 39's ground-site work"
metadata:
  node_type: memory
  type: project
  originSessionId: 1180787d-9718-488f-bc01-49d094c1aefa
  modified: 2026-10-06T20:55:42.308Z
---

Chasm roads become drag (freehand) -> green/red preview -> release commits one stroke of GROUND_PAINT
commands; right-click cancels. In PLAY a road is a ground SITE (ground_built 0): IDLE villagers walk out,
fell every standing tree in its verge at their STRENGTH pace (woodcutter skill) to PROP_STATE_LOG left
lying, then lay it - LABOUR ONLY - via ZONE_OP_GROUND_RAISE. Walls = new ZONE_GROUND_WALL on a plot chain,
palisade wood per plot, impassable only once standing, a road plot in the chain is the gate. Building
sites also fell their trees first (user said yes). Auto-routing roads: maybe later, not now.

Plan + as-built: apps/chasm/docs/line_works_plan.md. Steps: 1 tool (BUILT 2026-10-06), 2 road sites,
3 walls, 4 building sites clear. ZoneRoadStands(z,v) (Zones.h) was added by window c4 for bridge-as-road
(ground ROAD || ZoneBridgeWalkable); step 2 adds `&& ground_built` to its road half only.
Scripted drags: chasm_tool hover_x/hover_z + button down/up + right_click (a frame between calls);
chasm_pick reports zone_ground. Test scripts were in the session scratchpad (line_drag_test.py, shot.py).

**Why:** the user wants roads/walls built by villagers rather than appearing like a pen tool; the bridge
(c4's) and 39's gardens/lots-as-ground-sites (ground_built, ground_site[plot], ECONOMY_SITE_GROUND|plot)
are the two halves this reuses.

**How to apply:** check `lock_list` first - window 39 (session f74358c8) held ApplicationChasm*, Zones,
Economy, docs/, saves/, tools/, #build on 2026-10-06. Right-click cancel must go through UpdatePlayPick
(ApplicationChasmSelect.cpp) so it doesn't also deselect. See [[chasm-game-plan]], [[coordinator-worker-windows]].

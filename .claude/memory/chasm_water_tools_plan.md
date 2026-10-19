---
name: chasm-water-tools-plan
description: "Chasm test-map tools - ALL 5 steps BUILT (well, rivers, ponds, River/Pond tools 2026-10-18; painted HEIGHT 2026-10-19), A/B clean; apps/chasm/docs/water_tools_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 80068f08-caf2-4476-8759-ffd8a85ffc3a
  modified: 2026-10-18T17:55:30.487Z
---

Plan: `apps/chasm/docs/water_tools_plan.md` (decided with the user 2026-10-18; as-built notes under each step).

- **Well - BUILT 2026-10-18**: ZONE_KIND_WELL 15 (COUNT 16), CHASM_TOOL_WELL on the debug panel only, locked in play;
  Zones::Apply refuses it in a PLAY command. Water = its stock, seeded by EconomyWellWater (seed + plot, 100-200);
  FetchFrom + KeepTowers take from it, SeekSupplyOf needed nothing. Endless only via `set_stock endless:true`
  (EconomyState::endless, CHASM_CMD_STOCK value[3], saved as "endless_wells" only when present). Look = BuildWell,
  offset off the plot point so drinkers stand beside it. wiki/well.md.
- **Placed rivers - step 2 BUILT 2026-10-18**: GridWater.h + ChasmPlaceRivers (ChasmLayout.cpp), called in Grid::Generate
  right after the layout; save `grid.water` only when non-empty; chasm_generate `water`, chasm_grid river_list `placed`.
  A fall onto a BALCONY passes through its deck (not refused yet).
- **Ponds - step 3 BUILT**: GridWater::ponds -> ChasmLayout::ponds via ChasmPlaceWater (renamed from ChasmPlaceRivers);
  ChasmPondFits drops ones off the plateau (ponds_dropped). Bowl in BuildRelief after the river fade; Terrain::pond
  mask widens the swamp's wet + pool rules. Drawn APART (WaterMeshData::ponds, water_ponds, SetWaterPondUniforms):
  the swamp pool shader is murky green, a pond is the rivers' blue at pool stillness.
- **Tools - step 4 BUILT**: CHASM_TOOL_RIVER/POND on the debug panel (locked in play); UpdateWaterDrag samples the
  cursor every 3 units; PlaceWater regenerates on release (refused while recording); preview in the pick view;
  RenderWaterTools lists/removes water (NOT seen on screen - ImGui can't be scrolled by script). The panel's
  next_settings.water follows the map, so Generate/N keep water; New game/title start with none.
- **Height - step 5 BUILT 2026-10-19** (user: smooth raise/lower only, +-20, no cliffs, no flatten): GridSettings::relief
  strokes {points,radius,height} -> ChasmLayout::relief_strokes (ChasmPlaceRelief), hashed; BuildRelief adds them
  with their OWN river fade RELIEF_PAINT_RIVER_FADE 8 (the hills' 24 flattened a small map) and lowering faded near
  the rift. Tools moved to ApplicationChasmMapTools.cpp: UpdateMapDrag / RemakeMap / RenderMapTools; chasm_tool
  takes width/height/radius (the panel's sliders, for scripts).
- **Placed water, the design**: `GridSettings::water` (rivers + ponds) beside `kind`, saved only when
  non-empty, appended into the layout in Grid::Generate, hashed. Rivers = GridRiverLine (EDGE/JOIN/SINK by where
  drawn). Ponds = the swamp's pools generalised (bowl in the relief; the two biome==SWAMP checks widened).
  Placing water REGENERATES the map = a new colony (user OK: water only at a scenario's start). Chasm maps too.

**Why the colony can't be kept across a water edit:** trees are known by their forest index and water changes
which plots get trees, so saved tree state would land on other trees.

**How to apply:** run the test-maps A/B (saves part) after each step. A/B gotcha: pause BOTH instances before the
first chasm_load, or a running one ticks once before the "immediate" save and that file differs by a tick.
Related: [[chasm-test-maps-plan]], [[chasm-stone-plan]], [[chasm-goods-plan]].

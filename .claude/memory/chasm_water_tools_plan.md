---
name: chasm-water-tools-plan
description: "Chasm wells + placed water for test maps - step 1 (debug WELL) BUILT 2026-10-18, A/B clean; steps 2-4 (rivers/ponds in GridSettings) next; apps/chasm/docs/water_tools_plan.md"
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
- **Placed water - steps 2-4, not built**: `GridSettings::water` (rivers + ponds) beside `kind`, saved only when
  non-empty, appended into the layout in Grid::Generate, hashed. Rivers = GridRiverLine (EDGE/JOIN/SINK by where
  drawn). Ponds = the swamp's pools generalised (bowl in the relief; the two biome==SWAMP checks widened).
  Placing water REGENERATES the map = a new colony (user OK: water only at a scenario's start). Chasm maps too.

**Why the colony can't be kept across a water edit:** trees are known by their forest index and water changes
which plots get trees, so saved tree state would land on other trees.

**How to apply:** run the test-maps A/B (saves part) after each step. A/B gotcha: pause BOTH instances before the
first chasm_load, or a running one ticks once before the "immediate" save and that file differs by a tick.
Related: [[chasm-test-maps-plan]], [[chasm-stone-plan]], [[chasm-goods-plan]].

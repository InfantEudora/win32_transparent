---
name: chasm-gardens-plan
description: "apps/chasm/docs/gardens_plan.md - ALL 3 STEPS BUILT 2026-10-26: gardens keep broadleaf trees (pines felled), per-quad planting + cypresses, hanging gardens on terraces; wiki/garden.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 677ea99c-1541-41e5-b18b-320da485d07f
  modified: 2026-10-26T21:04:31.417Z
---

Agreed with the user 2026-10-26, written to `apps/chasm/docs/gardens_plan.md`. All three steps built and checked in game that day.

The user's choices:
- Gardens keep broadleaf trees and garden plants; pines are felled as now.
- Plant density as proposed, plus cypresses. I read "the cypress would be great" as covering big gardens too, not only terrace corners.
- The "hanging garden" look on terraces.

**Rules:**
- `EconomyGardenKeeps` / `EconomyPropSite` / the prop overload of `EconomyPropCovered`, plus a check where the felling finishes.
- `EconomyTreesOn(..., f_garden)` gives the card and price-tag count.
- One deviation from my proposal: kept plants' fruit still goes once the garden stands. Pickers choose plants by fruit alone (`GoPick`), so they would head for walled gardens.

**View:**
- `PropHidden` keeps kept plants in sight.
- `RebuildPropChunk` draws the plants from Gardens.h. Gardens.cpp decides them per quad (`TerrainChunkOfQuad` cuts chunks by quad centre, so cross-quad rules would go stale).
- `MeshCypress` (MeshBuild.h): at most one per coarse cell.
- Hanging garden: lawn on `BuildingWallCrossing`, no garden wall where `ZoneTerraceStep` isn't open, hedge, drapes with flowers, corner cypresses.
- `ForestData::plot_prop` added.

**Why:** the user wants gardens to feel planted and to fit terraced towns.

**How to apply:**
- Test scene: the debug-paint lists in the plan's "As built". terraces_test is in autumn/winter, so flowers don't show.
- Terraced plots keep nothing: buried props are still in the forest data.
- Not run: flowers in spring, and a woodcutter mid-walk when a garden is painted over his tree.

Related: [[chasm-yards]], [[chasm-terraces-plan]], [[chasm-woodcutting]].

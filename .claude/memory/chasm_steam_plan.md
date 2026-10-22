---
name: chasm-steam-plan
description: "Chasm steam + zeppelin plan (apps/chasm/docs/steam_plan.md): steps 1-2 BUILT 2026-10-21 (temperature readout, steam collector); condenser next, then ballista"
metadata:
  node_type: memory
  type: project
  originSessionId: 8a43c07d-a515-47eb-b112-6ad10766c43d
  modified: 2026-10-21T21:00:24.431Z
---

Decided by the user 2026-10-21: a zeppelin floats ONLY over the chasm (its heat), so steam is an ingredient that fills the envelope, not fuel. Steam collector and condenser are TWO buildings, sized by plots like every workplace - one collector may feed two smaller condensers deep down, 1-1 on the rim. Yield rises with EVERY step down (terraces beat the rim); terraces have no river, so condensers are their water. Envelope = hemp canvas (sail loft), replacing leather. All my proposals agreed (warm rim: snow never stops collectors; a floatstone seam guaranteed at home; tuning). Ballista tower is the next plan after this one.

Built 2026-10-21: step 1, `Climate.h` (named so because Economy already has `Heat`/HEAT_WARM for firewood) - ClimateAir + ClimateChasm, ClimateStepsDown (balcony/island steps, painted cliffs excluded) - and DrawHeatReadout, a strip under the calendar with degrees, heat strands and wind. Step 2, ZONE_KIND_STEAM / GOOD_STEAM / WORKER_JOB_STEAM, tool P, SteamIsBuildable, the hood mesh and white plumes (PAL_FOAM - PAL_MIST_LIGHT is grey). Step 3, ZONE_KIND_CONDENSER / WORKER_JOB_CONDENSER, tool L, EconomyCondenserFeeds + FeedCondenser (the ropemaker's TO_INPUT/TO_WORKSHOP errand). The user added brass: palette row 12 PAL_METALS (Box/SlopedBeam take a row now); the readout's hover tip was removed at the user's word. The floatstone-seam guarantee is the user's, separately - not in this plan.

The user said "plateau" for a terrace in the chasm, and "levels" for terrain levels - in code TERRAIN_PLATEAU is the rim-level land, so read their word by context.

**Why:** the game after the first winter is two things: defend against zombies, and reach the chasm for floatstone and a zeppelin.
**How to apply:** next is step 4, hemp canvas and the sail loft; then the dock + zeppelin site. Free tool keys left: I and Y. Test terraces on `saves/casm_edge.json` (seed 58): its winch lands on a wide balcony 2 steps down at x~50..64, z~-3; seed 12's balconies are too thin to camp on. Staffing goes by building order, so add people (chasm_economy add_person, no `house`) before judging a collector unworked. Related: [[chasm-stone-plan]], [[chasm-goods-plan]], [[chasm-zombies-plan]].

---
name: chasm-steam-plan
description: "Chasm steam + zeppelin plan (apps/chasm/docs/steam_plan.md): steps 1-5 BUILT by 2026-10-23 (readout, steam collector, condenser, sail loft, zeppelin dock); flight next, then ballista"
metadata:
  node_type: memory
  type: project
  originSessionId: 8a43c07d-a515-47eb-b112-6ad10766c43d
  modified: 2026-10-21T21:00:24.431Z
---

Decided by the user 2026-10-21: a zeppelin floats ONLY over the chasm (its heat), so steam is an ingredient that fills the envelope, not fuel. Steam collector and condenser are TWO buildings, sized by plots like every workplace - one collector may feed two smaller condensers deep down, 1-1 on the rim. Yield rises with EVERY step down (terraces beat the rim); terraces have no river, so condensers are their water. Envelope = hemp canvas (sail loft), replacing leather. All my proposals agreed (warm rim: snow never stops collectors; a floatstone seam guaranteed at home; tuning). Ballista tower is the next plan after this one.

Built 2026-10-21: step 1, `Climate.h` (named so because Economy already has `Heat`/HEAT_WARM for firewood) - ClimateAir + ClimateChasm, ClimateStepsDown (balcony/island steps, painted cliffs excluded) - and DrawHeatReadout, a strip under the calendar with degrees, heat strands and wind. Step 2, ZONE_KIND_STEAM / GOOD_STEAM / WORKER_JOB_STEAM, tool P, SteamIsBuildable, the hood mesh and white plumes (PAL_FOAM - PAL_MIST_LIGHT is grey). Step 3, ZONE_KIND_CONDENSER / WORKER_JOB_CONDENSER, tool L, EconomyCondenserFeeds + FeedCondenser (the ropemaker's TO_INPUT/TO_WORKSHOP errand). The user added brass: palette row 12 PAL_METALS (Box/SlopedBeam take a row now); the readout's hover tip was removed at the user's word. The floatstone-seam guarantee is the user's, separately - not in this plan. Step 4 (2026-10-23): GOOD_CANVAS 12 (made good, kept like rope), ZONE_KIND_SAIL_LOFT 18, WORKER_JOB_SAILMAKER 12, tool I; the ropewalk's errand is now shared through EconomyHempWork (MakeOfHemp/AtHempWorkshop); sailmakers by plots (my call, for the user to overrule); canvas cells in palette row 9 (PAL_CANVAS); test colony saves/sail_loft_test.json.

The user said "plateau" for a terrace in the chasm, and "levels" for terrain levels - in code TERRAIN_PLATEAU is the rim-level land, so read their word by context.

**Why:** the game after the first winter is two things: defend against zombies, and reach the chasm for floatstone and a zeppelin.
Zeppelin MODELLED 2026-10-23 (the user: dock the size of a field, zeppelin big, visible construction stages): zeppelin_1..5 + zeppelin_scaffold in chasm_props.blend/glb via blender_chasm_props.py --add --merge; stages = wood frame, floatstone hull, rope rigging, slack canvas, filled; one origin (dock ground), nose +Y; 14 x 5 x 7.4, fits 2x4 field cells; notes in steam_plan.md "The zeppelin, modelled". The user's own `zeppelin` object left untouched.

Step 5 BUILT 2026-10-23: ZONE_KIND_DOCK 19 on cells (ZONE_OP_DOCK_PAINT), no key (user), at the chasm's edge (ZONE_DOCK_REACH 13 - 9 refused every 2-wide rim dock); ZoneDockFrame fits the GROUND footprint 10.5x5 (envelope overhangs); the zeppelin = the dock's second site, EconomyState::zeppelin stages saved+hashed, SiteNeed/Build steam last, EVENT_ZEPPELIN_STAGE; ApplicationChasmDock.cpp instance sets; set_stock `site` debug shortcut; saves/zeppelin_dock_test.json. Test scripts in the scratchpad (dock_grid.py paints a lattice-straight block - an early version erased the colony's fields!).

**How to apply:** next is step 6, flight (its own plan, to talk through with the user first). NO letter keys are free now (Y terrace, I sail loft; WASD/U are core's) - a new tool needs a key decision. Test terraces on `saves/casm_edge.json` (seed 58): its winch lands on a wide balcony 2 steps down at x~50..64, z~-3; seed 12's balconies are too thin to camp on. Staffing goes by building order, so add people (chasm_economy add_person, no `house`) before judging a collector unworked. Related: [[chasm-stone-plan]], [[chasm-goods-plan]], [[chasm-zombies-plan]].

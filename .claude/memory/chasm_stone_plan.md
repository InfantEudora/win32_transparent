---
name: chasm-stone-plan
description: "Chasm stone + floatstone - all 5 steps built by 2026-10-16 (rubble crates, PICK UP, stonecutter, deposits, the mine); what stone buys is later; apps/chasm/docs/stone_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 44256e23-5adb-4f9e-8581-5dcae426eac1
  modified: 2026-10-24T11:31:55.566Z
---

Talked through with the user 2026-10-14, written to `apps/chasm/docs/stone_plan.md`; built step by step since (below).

- Plain STONE starts the colony: rubble at the foot of a balcony's wall, modelled as a CRATE (`CRATE_RUBBLE`) so builders move it and sites take from it with no new code. A PICK UP chip (replaces the never-built FETCH FIRST) works on crates and rubble alike.
- Stonecutter's hut = woodcutter for rocks; only big stones (today's rock props, maybe boulders). Stone's first use is building that hut and the mine.
- FLOATSTONE comes from seams in balcony walls, ~50 each (user's figure), dug by a MINE (winch rule turned over: higher ground behind). The mountain's stone never runs out.
- Second round: rubble lies on the rim AND the balconies (rope already gates the winch), picked up when needed with PICK UP to jump the queue; boulders in; coal + the swamp's resource later.
- Floatstone LOOKS like crystal, purple-ish blue (user). The user types "flowstone" but means floatstone.
- Coal is NOT part of the zeppelin. The user liked the softer Valheim idea: only the chasm is a hard gate; mountain = what lasts, swamp worked in winter. Details later.
- Props briefed to worker window win32-transparent-0c 2026-10-14 (rubble, boulder, stone blocks, floatstone, mine portal + cart, quarry crane, outcrop). Hut and mine bodies are BuildingMesh code, not Blender.

- STEP 5 BUILT 2026-10-15 (the mine): ZONE_KIND_MINE 14 (key O), WORKER_JOB_MINER 9, MineIsBuildable (seam foot / mountain foot / outcrop), miner digs a load at the rock and carries it (GoDig, no stock at the mine), EconomyState::dug saved+hashed, DugOut frees the miner only after his last load (MineCarrying), seam band = TerrainSeamBand crystal/rock swap, props in ApplicationChasmMines.cpp. All tested 2026-10-16 on a fresh instance (11,602-tick replays equal; mountain mine on seed 6 digs endless stone; outcrop crane). Mountain portals use a frozen-row copy of mine_portal. The user browses seeds in whichever chasm window is open - keep test instances on a spare port and expect their N/B/U keys.
- 2026-10-24: a PLAY mine on a seam is refused "a crate lies there" until that plot's floatstone pile is PICKED UP (every seam foot plot has one; debug builds over and loses it). User chose to KEEP the rule and show it: the ghost is now YELLOW wherever only a crate stops a placement (all tools, drags per plot), tag "FLOATSTONE LIES THERE: PICK IT UP FIRST"; the mine got a ghost (plot tile - it had none). Built + tested; crates_plan.md "As built: the ghost goes yellow over a crate". The mine's missing slope grid is deliberate (UpdateSlopeGrid).
- DECIDED + BUILT 2026-10-15: PICK UP refused past ECONOMY_PICK_UP_WATER (half a water bar = 2 days) of walking time there + on to the store (WalkSeconds over RouteTo's route); card "Too far to fetch"; verdict cached in Economy::pick_up_looks (not saved). Clouds over unexplored balconies: LEFT AS IS (user: maybe just right; a fix would be a wall of clouds). Step 5: mines against the mountain also on the plateau's gentle foot - a portal into the slope (user likes the asset).
- THIRD ROUND 2026-10-15 (user, after seeing step 4): rubble ONLY at balconies' wall feet, none on the rim (first stone needs a winch); a seam's crystals = floatstone piles (CRATE_RUBBLE holding floatstone, EconomyCrateCrystal, drawn floatstone_b) on every seam foot plot, picked up like rubble; the fixed crystal prop removed; seams never under the mountain (4 of 9 were unreachable). User picked "piles, like rubble" over crystal outcrops or crystals broken like rocks.
- STEP 4 BUILT 2026-10-14: Deposits.h/.cpp, ChasmWorld::deposits (generated, never saved; built before the mesh and forest). Seams: run of 4 foot plots along ONE wall (SEAM_SAME_WALL), band = crystal faces in TerrainMesh WallColumn, floatstone_a crystal (PROP_FLOATSTONE); mountain = deposit 0 (its foot is gentle on the plateau - step 5 must decide); outcrops replace pocket rock clusters (PROP_OUTCROP). NOTICE_SEAM_SEEN via LookForSeams (app's exploration). "taken" + worked-out redraw deferred to the mine. Flagged, not fixed: play camera's ViewGroundHeight settles on UNEXPLORED balconies and sits under the cloud lid.
- STEPS 2 AND 3 BUILT 2026-10-14: PICK UP (crate flag f_pick_up, CHASM_CMD_PICK_UP, builders fetch it before building, from anywhere); ZONE_KIND_STONECUTTER 13 / WORKER_JOB_STONECUTTER 8 (key M), PROP_BOULDER appended to PropKind, rocks STANDING or cleared (PROP_STATE_STUMP value), BreakRock leaves a CRATE_BROKEN rubble crate and lifts via WORKER_LIFTING; boulders block sites via SiteTreesStanding. Next is step 4, deposits.
- STEP 1 BUILT 2026-10-14: GOOD_STONE 9 / GOOD_FLOATSTONE 10; rubble = CRATE_RUBBLE crates laid by Economy::PlaceRubble on the new colony's first tick (FollowZones); drawn with rubble_a (stand-in rock cluster). Seed 12: 114 piles/470 stone, but almost none on the camp's land - flagged as tuning. Next is step 2, PICK UP.
- Worker 0c built 10 props (rubble_a/b, boulder, stone_blocks, floatstone_a/b, mine_portal, mine_cart, quarry_crane, outcrop) and palette row 10 PAL_MINERALS; blender_chasm_props.py --add --merge appends to the GLB without a whole export.

**Why:** the user's answers fixed the shape; the doc's Open list holds what's left (coal, swamp, floatstone's palette colour, island floatstone).

**How to apply:** read the doc's Decided section before building any step; build order is goods+rubble, PICK UP, stonecutter, deposits, mine. Related: [[crates-plan]], [[chasm-goods-plan]], [[chasm-woodcutting]], [[chasm-foraging]].

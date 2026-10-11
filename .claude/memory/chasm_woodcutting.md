---
name: chasm-woodcutting
description: "chasm woodcutting_plan.md - only woodcutters fell (roads, walls, sites wait); camp 1 farmer + 1 woodcutter; log worth 2/4/5; hut room by floor area; all 5 steps built 2026-10-11"
metadata:
  node_type: memory
  type: project
  originSessionId: 29744408-68e4-448f-b96c-c0047ed41572
  modified: 2026-10-11T20:43:51.751Z
---

Agreed with the user and built 2026-10-11 (apps/chasm/docs/woodcutting_plan.md, all five steps checked in game):
- ONLY A WOODCUTTER FELLS. The idle only lay a road once clear. Trees in a road's/wall's way, or on a SITE (building,
  field, garden, lot not yet built), are what woodcutters go for first; a site rises only once its trees are down.
  EconomyPropCovered now means BUILT only (the view's PropHidden asks it). Debug paint still covers at once.
- CAMP: one farmer (its land capped, rest fallow) and one woodcutter of its own (cutter_places), camp residents only.
- LOG WORTH by workplace (log_wood): camp 2, hut 4, hut two storeys on EVERY plot 5.
- HUT ROOM: indoors by floor area (ECONOMY_HUT_ROOM) + its yard's lots by area (ECONOMY_LOT_ROOM); ECONOMY_HUT_PILE gone.
- NEEDS ATTENTION: row 8 no_room, row 12 trees_unreached (click takes up the woodcutter tool).

**Why:** roads painted everywhere had cleared a forest with no woodcutter, and a hut's size bought nothing.
RETUNED same day (user: lots held too much for their cost; indoors only above a minimum size, "a cost 4 woodcutter
would start having space"): ECONOMY_LOT_ROOM 2/area, indoor room only past ECONOMY_HUT_WORKSHOP 1.5 floor
(EconomyHutRoom); the price tag says what a hut keeps. WATER COLLECTOR the same (user, same day): holds a load +
ECONOMY_WATER_ROOM past ECONOMY_WATER_PUMP floor (EconomyCollectorRoom, derived water_room); fill pace left fixed.
STRENGTH (user, same day, "about 25%" each, then "for all jobs, the same rule"): anyone with a load walks by
strength (ECONOMY_CARRY_STRENGTH, in Walk); ECONOMY_SKILL_DISTANCE 4 -> 3 for every job.

**How to apply:** open for the user: start balance (ECONOMY_START_WOOD, camp cap 60) not tuned - they find getting
farms up fast enough the hardest part (the camp's one-farmer cap is a lever). Several design calls in
the plan's "Worked out here" section are mine, for the user to overrule. See [[chasm-game-plan]], [[chasm-yards]].

---
name: chasm-goods-plan
description: "chasm goods plan (docs/goods_plan.md) - four goods categories, hemp crop, ropewalk making rope; AGREED 2026-10-10; steps 1-4 BUILT, step 5 (view by category) next"
metadata:
  node_type: memory
  type: project
  originSessionId: 5bac1302-f1b1-4435-8b32-339b4416d1b9
  modified: 2026-10-10T13:36:43.076Z
---

Agreed with the user 2026-10-10 (apps/chasm/docs/goods_plan.md; wiki ropewalk.md is a `planned` page).
Every proposal in the plan was agreed as written EXCEPT the first rope: the colony starts with NO rope, so
the winch waits for a hemp field + ropewalk ("you have to set up production for it").

- **Hemp** is a crop of its own (wheat-straw rope scrapped), not food, never AUTO's pick, own look on the field.
- **Ropewalk**: works from 2 plots; a 3rd and 4th raise efficiency for the same one ropemaker (precision). The
  user sees "size raises output" as a mechanic also meant for houses+gardens later - the ropewalk is its first use.
- **Rope wear / upkeep: later**, kept as an idea.
- **Categories FOOD, WATER, MATERIALS, INPUTS.** User's rule: a MATERIAL is built with once (rope, leather -
  the zeppelin), an INPUT is used up continuously (coal; hemp). Upkeep (winch maintenance, zeppelin repair)
  makes rope + leather inputs there. Only FOOD is summed; materials are grouped, never added to WOOD.

Step 1 BUILT 2026-10-10: GOOD_HEMP 6, GOOD_ROPE 7, GoodCategory() in Goods.h, GoodIsFood by category,
HudGoodsOrder by category, SiteErrand for "fetching rope for a site".
Step 2 BUILT 2026-10-10: ZONE_CROP_HEMP 3, GoodOfCrop table, GOODS_CROPS, CropIsFood (AUTO skips hemp - also
in EconomyFieldSowNow, where hemp would win as the only crop ripening in time), harvest = food first then hemp,
never home. Look: tall staggered pointed plants (flat-topped boxes read as tiles/hay bales - rejected); colours in
a new palette row PAL_CROPS 9 (no season fade, no snow). The user asked for hemp's own wiki page under goods
(wiki/hemp.md) - each good gets one; rope has wiki/rope.md too.
Step 3 BUILT 2026-10-10: ZONE_KIND_ROPEWALK 12, key K, WORKER_JOB_ROPEMAKER 6, ZoneKindMaxPlots (house 4,
ropewalk 4), MakeRope/AtRopewalk reuse the builders' provisions states - while MAKING rope carry_good=GOOD_ROPE
with carry 0 (ProvisionsLacking skips it); WorkplaceMakes limits FetchFromWorkplace to rope. Build grid has a
WORKSHOPS row (empty cells allowed).
Ropewalk PROPS built 2026-10-10 at the user's ask (BuildRopewalkYard in BuildingMesh.cpp, drawn by
ApplicationChasmRopewalk.cpp as one mesh): walk of strands on T-posts per plot pair, yard (wheel, coils per 2 rope,
hemp sheaves) on the open side most toward the camera (south, +z). Props north of a building hide behind its roof -
always prefer the camera side. PAL_ROPE is palette row 9 col 2. Ropemaker walking the walk backward: user liked it, LEFT OPEN
(they are doing animations in parallel) - don't build unasked.
Step 4 BUILT 2026-10-10: StoreyGoods gives a winch ECONOMY_WINCH_ROPE 4; EconomyRopeChain (hemp, then ropewalk) +
EconomyGoodHeld drive the site card line and NEEDS ATTENTION `site_needs_rope` (GROW HEMP -> field card / field tool,
then BUILD A ROPEWALK). Testing a winch needs a balcony: none near the default seed's camp - order a villager to the
nearest reachable rim (~100 units) to explore it, then paint with play:true (debug paint builds at once). Next: step 5,
the view by category.

**Why:** rope is the winch's and zeppelin's material; the user wants the cards to stay readable as goods grow
toward ~15 (GoodsMask is 16 bits).
**How to apply:** build in the plan's order (goods+categories, hemp, ropewalk, rope cost, view). Other agents
(lifelines, guard tower) edit Economy.cpp a lot - claim, edit, release fast. See [[chasm-game-plan]],
[[chasm-defence-plan]] (site costs per good, provisions as the made-good pattern), [[crates-plan]].

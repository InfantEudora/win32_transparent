---
name: chasm-goods-plan
description: "chasm goods plan (docs/goods_plan.md) - four goods categories, hemp crop, ropewalk making rope; all six steps BUILT and checked 2026-10-10; kept stock + make-more orders for provisions/rope built too"
metadata:
  node_type: memory
  type: project
  originSessionId: 5bac1302-f1b1-4435-8b32-339b4416d1b9
  modified: 2026-10-10T21:36:44.279Z
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
nearest reachable rim (~100 units) to explore it, then paint with play:true (debug paint builds at once).
Step 5 BUILT 2026-10-10: top bar shows extra goods only when held or a site wants them; FOOD hover via
overlay_mouse_x/_y (chasm_select op hover pins it); HudGoodsGroup (materials / food+water / inputs) drives
Card::StockByGroup and the store chips (group chip = OVERLAY_HIT_STORE_ALLOW_GROUP, goods at one indent). 
Step 6 CHECKED 2026-10-10: one run hemp sown -> harvested (12 from a 20-area cell) -> 5 rope -> winch stood;
a 4,500-tick recording of it replayed twice, every tick's hash equal. The plan's build order is DONE. Open: rope
wear/upkeep (later), the ropemaker walking the walk (user doing animations - left open).
KEPT STOCK built + checked 2026-10-10 (goods_plan.md "Kept in stock, and made to order"; first step of the
scouting plan in defence_plan.md). User's answers: BOTH a standing kept number AND a one-off "make N more" order;
one number per good for the COLONY (set from any hut/ropewalk card); rope KEEPS being made to the pile (12) - the
kept number only decides how much goes to stores (default 0 = none); woodcutter later. Counted against
EconomyStoredTotals (top bar's stores+camp). Order = a level max(kept, stored)+N, cleared when stores reach it.
Next in that line: the scout's "gather supplies" chip, bigger pack, messages (defence_plan.md "Scouting").

**Why:** rope is the winch's and zeppelin's material; the user wants the cards to stay readable as goods grow
toward ~15 (GoodsMask is 16 bits).
**How to apply:** build in the plan's order (goods+categories, hemp, ropewalk, rope cost, view). Other agents
(lifelines, guard tower) edit Economy.cpp a lot - claim, edit, release fast. See [[chasm-game-plan]],
[[chasm-defence-plan]] (site costs per good, provisions as the made-good pattern), [[crates-plan]].

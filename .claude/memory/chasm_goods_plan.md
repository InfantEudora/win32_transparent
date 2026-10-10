---
name: chasm-goods-plan
description: "chasm goods plan (docs/goods_plan.md) - four goods categories, hemp crop, ropewalk making rope; AGREED 2026-10-10; step 1 (goods + categories) BUILT"
metadata:
  node_type: memory
  type: project
  originSessionId: 5bac1302-f1b1-4435-8b32-339b4416d1b9
  modified: 2026-10-10T10:54:49.925Z
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
a new palette row PAL_CROPS 9 (no season fade, no snow). Next: step 3, the ropewalk.

**Why:** rope is the winch's and zeppelin's material; the user wants the cards to stay readable as goods grow
toward ~15 (GoodsMask is 16 bits).
**How to apply:** build in the plan's order (goods+categories, hemp, ropewalk, rope cost, view). Other agents
(lifelines, guard tower) edit Economy.cpp a lot - claim, edit, release fast. See [[chasm-game-plan]],
[[chasm-defence-plan]] (site costs per good, provisions as the made-good pattern), [[crates-plan]].

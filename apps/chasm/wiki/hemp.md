---
title: Hemp
aliases: hemp field, hemp fields
kind: goods
status: built
summary: A crop that is not food, grown on a field for rope and canvas. AUTO never sows it, and its harvest never goes home.
plans: goods_plan.md > Decided; goods_plan.md > Hemp; goods_plan.md > Step 2: hemp, the crop; steam_plan.md > Hemp canvas and the sail loft
code: Goods.h: GOOD_HEMP GOODS_CROPS GoodOfCrop; Zones.h: ZONE_CROP_HEMP; Economy.cpp: CropIsFood FetchHarvest PickHarvest; CropMesh.cpp: HEMP_RIPE_HEIGHT
verified: 2026-10-23
---

Hemp is grown on a [[Field|field]] like the other crops, but it is **not a food**: it feeds nobody, and no
[[House|house]] keeps it. It is an **input**, a good that is made into something else: [[Rope|rope]] at
a [[Ropewalk]], and [[Canvas|canvas]] at a [[Sail loft|sail loft]]. Their makers fetch it from the stores, each
as much as its own pile has room for, whoever comes first - so once one pile is full, the other gets the rest.

## Growing it

- It is chosen on a field's card like any crop, as its starting crop or its next one.
- **AUTO never picks it**, not even when it is the only crop that would still ripen before the frost. AUTO
  means the most food, so hemp grows only where the player asks for it, and a hemp field's next crop goes
  back to food unless the player picks hemp again.
- It grows in {crop_days[ZONE_CROP_HEMP]:#days}, like wheat, under the same rules as every crop
  ([[Seasons]], the frost).
- **In the rotation**, what follows it does well ({crop_after[ZONE_CROP_HEMP][ZONE_CROP_WHEAT]}% for wheat
  and greens), and hemp after hemp drops to {crop_after[ZONE_CROP_HEMP][ZONE_CROP_HEMP]}%. After beans it
  does well, as every crop does.
- It stands as rows of **tall plants**, about a person's shoulder high when ripe, instead of the other
  crops' low ridges: dark green as it grows, olive with yellow-green crowns once ripe.

## The harvest

A field's harvest is **food first**: its farmer fetches hemp only once no food lies on the field, so hemp
never keeps a family's food waiting. It **never goes home**. The camp's land puts it in the camp's
supplies; any other land's goes to a [[Store|store]] that takes hemp. A store beside a field takes it, as
it takes the foods, and every store takes it by default; its card has a chip for it, among the inputs.

It shows in the top bar, HEMP after WATER, once the colony holds some in its stores.

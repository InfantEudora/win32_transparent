---
name: chasm-goods-strip
description: "chasm cards show goods as one strip per stock (ChasmHud HudStock); solid=anyone's, hollow=kept, striped=reserved; user's colour/row choices 2026-10-09"
metadata:
  node_type: memory
  type: project
  originSessionId: cd3eb3c1-f675-45da-b343-4593c448dd91
  modified: 2026-10-09T18:06:19.905Z
---

BUILT 2026-10-09 (user approved a mockup first, then chose): every card that holds goods uses the GOODS STRIP
(ChasmHud.h/.cpp: HudGoodColour, HudGoodsOrder, HudStock, DrawHudStock; card line CARD_STOCK). As built:
apps/chasm/docs/selection_plan.md "The goods strip".

- Fill = who may take it: SOLID anyone's, HOLLOW kept (lived-in house, camp pantry, field harvest, builders'
  hut, site), STRIPED reserved (the user's word for goods someone is already walking to take).
- User chose THREE food colours (wheat = top bar's food gold, greens green, beans rose; provisions pale
  hardtack, picked by me). A house = TWO rows, pantry (room) + woodshed (woodshed).
- Kept/reserved come from Economy (EconomyPrivateStock, EconomyReserved, sharing SiteReservation/
  HomeReservation with GoodFree/FetchHome) so the card can't drift from the rules.
- A new good needs a HudGoodColour case or it shows grey.
- Long cards (camp of settlers) move left of the bottom-right stack via build_stack_left.

**Why:** the user wanted to see at a glance what is up for grabs vs. a house's or plot's own supplies.
**How to apply:** any new place that holds goods (crate card, person's pack - still text) gets a HudStock row,
not a text list. Related: [[chasm-game-plan]], [[crates-plan]].

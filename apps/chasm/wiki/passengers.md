---
title: Passengers
aliases: passenger, aboard, board, boarding, go aboard, step off, everyone off, cabin, gangway
kind: people
status: built
summary: People go aboard a zeppelin and off it only by order, with their packs and tents - so an expedition founds a settlement on an island. The cabin is their pantry.
plans: flight_plan.md > Decided (the user, 2026-10-24); flight_plan.md > As built: step 2 (2026-10-24)
code: Economy.h: ZEPPELIN_PASSENGERS ZEPPELIN_BOARD_REACH ZEPPELIN_CABIN_WATER ZEPPELIN_CABIN_PROVISIONS WORKER_ABOARD EconomyZeppelinLanding EconomyDeckPlace; Flight.cpp: Board BoardIfThere StepOff CabinLeft; Economy.cpp: Order KeepTowers Needs; ApplicationChasmFleet.cpp: BuildGangway
verified: 2026-10-25
---

A [[Flight|zeppelin]] takes people only when they are ordered on or off it (the user's).

## Going aboard

- With people selected, **right-click the zeppelin** while it is moored or set down. They are [[Posts|posted]] on
  its landing - the plot it is set down by, or the dock under it - spread round it as a group is, and go aboard once
  they stand there with it - at a [[Mooring mast|mast]], the open plot beside the mast. In the air it is refused.
- At most {ZEPPELIN_PASSENGERS} grown-ups. A child going with his grown-up goes aboard and off with him and takes no
  place; a baby is carried.
- "Pack a Tent" and "Grab Supplies" send them for those first, as anyone posted.
- The zeppelin flown off before they get there, they wait on its landing for it.

## Aboard

- They stand on its deck, two abreast at the rail, wherever it goes - out of every round: no job, no errand, and no
  [[Zombie|zombie]] reaches them.
- **The cabin is their pantry**, as a [[Guard tower|tower]] is its guards': its water and [[Provisions|provisions]]
  first, their packs only for a kind it has none of. Both empty, they go hungry - nobody leaves unordered.
- Moored at a dock or a mast, the builders keep the cabin full, up to {ZEPPELIN_CABIN_WATER} water and
  {ZEPPELIN_CABIN_PROVISIONS} provisions, as they keep a tower: the first of their errands.

## Off

- **Any order** takes someone off while the zeppelin is down: he steps onto its landing, tops up his pack from the
  cabin and goes on to where he was sent. Released, he goes home from there. In the air, every order is refused.
- **Everyone off**, on the zeppelin's card, orders every grown-up aboard onto its landing, spread round it.
- Set down by an island they step off there - and one carrying a tent can pitch it: a new
  [[Settlement|settlement]] ([[Founding|founding]]), supplied by air.

## How it looks

Down, a gangway runs from its deck to the edge or the dock - at a mast, from its bow to the lip beside the mast. The zeppelin's card lists those aboard - a click selects
one - with the places taken, those on their way, and its cabin against full; a person's card says "aboard the
zeppelin".

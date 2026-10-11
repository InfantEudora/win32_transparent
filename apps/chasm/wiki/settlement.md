---
title: Settlement
aliases: settlements, territory, territories, border, borders
kind: world
status: partial
summary: A camp and the land within a walk of it. People only take on work in their own settlement, and nothing new is placed outside every settlement.
plans: settlements_plan.md > Decided; settlements_plan.md > Step 1: territories; settlements_plan.md > As built: step 1
code: Economy.h: ECONOMY_SETTLEMENT_REACH EconomySettlementOf EconomyPersonSettlement EconomyCommandInTerritory; Economy.cpp: Settle Ours; Walkers.cpp: MapFlood; ApplicationChasmBorders.cpp: UploadBorders DrawWalkTip
verified: 2026-10-11
---

A [[Camp|camp]] founds a settlement: the start camp founds the first. Its **territory** is all the land
within {ECONOMY_SETTLEMENT_REACH} units of **walking** from the camp - over the map itself, so a river is
crossed only on a standing [[Bridge|bridge]], a cliff only by a [[Winch|winch]], and the mountain and slopes too
steep to walk are left out. What is built on the land doesn't count, so putting up a house never moves a border.

Where two settlements' territories would overlap, each piece of land goes to the camp that is the shorter walk
away, so the border falls halfway between them, bending round rivers and cliffs.

A camp placed inside a territory is just more tents for that settlement. One placed outside every territory
founds a new one - today only by debug placement; founding by expedition is planned.

## What it limits

Everything belongs to the settlement of the land it stands on - buildings, [[Field|fields]], gardens, lots,
[[Road|roads]], walls, [[Crate|crates]] - and a [[Person|person]] to their home's. People only take on work in
their own settlement:

- carrying to [[Construction|building sites]], and clearing and laying roads;
- a job at a workplace: each settlement's workplaces are filled from its own people, so a settlement with too few
  people leaves its own newest workplaces empty, not another's;
- [[Fetching home|fetching for their home]], and the [[Store|store]] a load is taken to;
- the builders' errands (towers, [[Provisions|provisions]], crates) and the [[Ropewalk|ropemaker's]] hemp.

Families only move into houses in their own settlement, and a field's land only goes to a house or camp in its
settlement. **Not limited**: someone on a [[Posts|post]] and their supply trips, since an order is the player's,
and a [[Thirst|thirsty]] or hungry person going to the nearest supply.

## Placing

In play, nothing new is placed outside every territory: the placement is refused with "outside the settlement",
as one under the clouds is, and its outline shows red. A bridge needs its near end inside. Erasing is never refused.

## How it looks

Its border is marked on the land the way The Settlers marks one: a slim post with a small terracotta flag at
every edge of land where one settlement ends or two meet, the flag turned toward the territory. Not along a
cliff's edge - the drop is border enough.

With a person selected, the cursor says how far it is to walk to the land under it ("37 units to walk"), in
amber when that land is in another settlement or none, and "No way there" when it can't be reached.

## Planned

- [planned] **Founding by taking a tent**: each person on an expedition packs a spare tent at the camp and pitches
  it where they stand, which makes it their home. A camp's own woodcutter, farmer and water carrier keep it going.
- [planned] **Any home keeps a settlement**, a tent or a house: it only vanishes when the last of them does, and
  someone whose home goes crams into a house of their own settlement.
- [planned] **Porters**: a job of its own, maybe with a building, carrying goods between settlements.
- [planned] Kept stock, the top bar and NEEDS ATTENTION per settlement.

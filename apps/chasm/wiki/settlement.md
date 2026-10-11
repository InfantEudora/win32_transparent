---
title: Settlement
aliases: settlements, territory, territories, border, borders
kind: world
status: partial
summary: Homes and the land within a walk of them, further the more beds they have. People only take on work in their own settlement, and nothing new is placed outside every settlement.
plans: settlements_plan.md > Decided; settlements_plan.md > Step 1: territories; settlements_plan.md > As built: step 1; settlements_plan.md > Founding: decided; settlements_plan.md > Founding: as built, step 1
code: Economy.h: ECONOMY_SETTLEMENT_REACH ECONOMY_SETTLEMENT_FIRST ECONOMY_SETTLEMENT_PER_BED EconomySettlementReach EconomySettlementOf EconomyPersonSettlement EconomyCommandInTerritory; Economy.cpp: Settle Ours HouseFamilies; Walkers.cpp: MapFlood; ApplicationChasmBorders.cpp: UploadBorders DrawWalkTip
verified: 2026-10-11
---

A **home** founds a settlement - a [[Camp|camp]] or a [[House|house]], since a tent is just a house - and the
start camp founds the first. Its **territory** is the land within a walk of its oldest home - over the map
itself, so a river is crossed only on a standing [[Bridge|bridge]], a cliff only by a [[Winch|winch]], and the
mountain and slopes too steep to walk are left out. What is built on the land doesn't change the walk.

## How far it reaches

By its **beds**: every tent, and every house's room. One bed - a single tent - reaches
{ECONOMY_SETTLEMENT_FIRST} units, and each bed more adds {ECONOMY_SETTLEMENT_PER_BED}, up to
{ECONOMY_SETTLEMENT_REACH}. The start camp's tents already reach the most. Counting beds rather than tents means
that families leaving their tents for houses, and the empty tents being cleared away, don't shrink it.

Where two settlements' territories would overlap, each piece of land goes to the home that is the shorter walk
away, so the border falls halfway between them, bending round rivers and cliffs.

## How long it lasts

A settlement lasts while **any home in it stands**. Erase its first camp and its oldest house becomes its
centre, so the border shifts a little toward it. Families whose homes are gone squeeze into the homes that are
left ([[House|crammed]]). It is gone only when the last of its homes is: its people then have nowhere to live
and wait where the settlers first camped.

A home placed inside a territory adds beds to that settlement. One outside every territory founds a new one. In
play, that's done by taking a tent out of a camp and pitching it there: see [[Founding|founding]].

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
settlement. **Not limited**: someone on a [[Posts|post]] and their supply trips, since an order is the player's.
A [[Thirst|thirsty]] or hungry person drinks and eats only in their own settlement or on unclaimed land, never
from another village's supplies - posted or not.

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

- [planned] **Porters**: a job of its own, maybe with a building, carrying goods between settlements.
- [planned] Kept stock, the top bar and NEEDS ATTENTION per settlement.

---
title: Floatstone
aliases: floatstones, crystal
kind: goods
status: built
summary: The chasm's own stone, a purple-blue crystal, in seams along the balconies' walls - the first of it lying at a seam's foot, picked up like rubble, the rest dug by a mine; for the zeppelin's hull later.
plans: stone_plan.md > Decided (the user, 2026-10-14); stone_plan.md > 4. Deposits; stone_plan.md > As built: step 4 (2026-10-14); stone_plan.md > As built: the third round (2026-10-15); stone_plan.md > As built: step 5 (2026-10-15)
code: Goods.h: GOOD_FLOATSTONE; Deposits.h: DEPOSIT_SEAM DEPOSIT_SEAM_FLOATSTONE; Economy.h: RUBBLE_FLOATSTONE_MIN RUBBLE_FLOATSTONE_MAX EconomyCrateCrystal; Economy.cpp: PlaceRubble; Palette.h: PAL_MINERALS
verified: 2026-10-18
---

A **material**, like [[Stone|stone]], and the one only the chasm has. It looks like crystal - purple-blue facets
with the odd near-white glint - so a seam, a pile or a load of it is never taken for grey stone. Its colours have a
palette row of their own, which no season fades and no snow covers.

## Where it is

In **seams**, along the foot of some of the balconies' walls - one at most to a balcony, never under the mountain,
holding {DEPOSIT_SEAM_FLOATSTONE} each, and found by exploring ([[Deposit|deposits]]). The way down to one is a
[[Winch|winch]].

**Crystal lies fallen at every seam's foot**: a pile on each plot of its run, {RUBBLE_FLOATSTONE_MIN} to
{RUBBLE_FLOATSTONE_MAX} floatstone each - rubble of floatstone, a [[Crate|crate]] like any pile of [[Stone|rubble]].
The [[Builder|builders]] carry it to a store as their last errand on their own land. With **PICK UP** it goes first,
for builders and anyone without a job alike, from anywhere they can walk to and back without water
([[Crate|PICK UP]]) - a seam far from every store waits for one nearer. Its card says "Floatstone", fallen from a seam in the wall; it is drawn as a heap of broken crystals. Every
[[Store|store]] takes it by default.

**The rest is dug by a [[Mine|mine]]** on the seam's foot, a load at a time, until the seam is dug out.

## Planned

- [planned] The zeppelin's hull: what crossing the chasm needs.
- [idea] Floatstone on the islands - "scarce" or "without end" is still to settle, with the zeppelin.

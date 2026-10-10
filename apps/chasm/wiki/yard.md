---
title: Yard
aliases: yards, lot, lots, garden, gardens
kind: buildings
status: built
summary: Gardens and lots, painted on plots and belonging to a house or a woodcutter's hut. A hut's woodpile is on its lot; a garden does nothing yet.
plans: yards_plan.md > Proposed, and agreed; yards_plan.md > As built; yards_plan.md > Fix: shut in a yard; yards_plan.md > Noted
code: Zones.h: ZoneOwnsGround ZoneKindHasYard ZoneGroundOwner ZoneCanYard ZoneAutoOwner ZoneYardPlots; Zones.cpp: claim_yard ZoneBoundaryBetween; Walkers.cpp: EdgeOpen; ApplicationChasmSelect.cpp: YardLines
verified: 2026-10-10
---

A yard is ground painted on plots that belongs to a building: **gardens**, green and walled, and **lots**,
trodden earth behind a fence of stakes. A [[House|house]] may own both, a [[Woodcutter|woodcutter's hut]]
only lots. No other building owns ground, and roads and walls are never owned. A plot has one owner, or none.

## Whose it is

- **Select the building, then paint.** Its card's **Add lot** and **Add garden** take up the tool with it
  still selected. Each plot must touch the building or ground already in its yard, and be a kind it may
  own; another building's yard stays that building's. The ghost goes red where this refuses, with why.
- **Painted with nothing selected**, a plot goes to a building beside it that can own it (the lowest id
  where several do), else to the owner of yard ground beside it, else to nobody.
- **A building put down beside ground nobody owns takes it**, and all of that ground joined to it. So a lot
  painted first and a hut put beside it after is the hut's.
- **When the owner goes**, its yard is nobody's, until the next building put beside it takes it.

A selected house or hut lists its yard on its card ("Yard: 1 garden plot, 2 lot plots") and outlines it.

## Building it

Placed in play, each plot is a [[Construction|site]] of its own, at {ECONOMY_GROUND_WOOD_PER_AREA}
[[Wood|wood]] per unit of its area, built from a plot beside it. Until its wood is in it has no wall or
fence, and is open ground.

## Walls, fences and gates

A standing yard is walked round. Its wall or fence is crossed only at a **gate**, where a
[[Roads and walls|road]] ends against it; a road is never painted over a garden or lot. Nobody is meant
to go in: a woodpile is reached from its hut. Someone a fence went up around may climb out over it, but
nobody climbs in. Two yard plots side by side have no fence between them, whatever their kind or owner.

## What it does

- **A woodcutter's lot** holds the woodpile, on one standing lot plot beside the hut. A hut with no such
  lot fells nothing, and its card and NEEDS ATTENTION say so ([[Woodcutter]]).
- **A house's gardens and lots** do nothing yet.

## Planned

- [planned] A yard's plots touch its building itself, never only its yard, so the building is the door to
  every plot of it. A plot painted on from the yard and not beside the building is no longer its.
- [planned] A garden or lot still a site can be selected, as a [[Field|field]] site can, with a card like
  a field site's: the wood brought and still wanted, and who is carrying it. And it looks like a site:
  staked out as a field site is, not drawn as finished ground with its wall or fence missing.
- [idea] What a garden does: food for its family, or comfort.
- [idea] Other buildings with a yard: a store's lot as outdoor storage, a builders' hut's timber yard.

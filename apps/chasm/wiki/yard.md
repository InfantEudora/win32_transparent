---
title: Yard
aliases: yards, lot, lots
kind: buildings
status: built
summary: Gardens and lots, painted on plots and belonging to a house or a woodcutter's hut. A hut's woodpile is on its lot; a garden does nothing yet.
plans: yards_plan.md > Proposed, and agreed; yards_plan.md > As built; yards_plan.md > Fix: shut in a yard; yards_plan.md > Noted; woodcutting_plan.md > Decided (the user, 2026-10-11)
code: Zones.h: ZoneOwnsGround ZoneKindHasYard ZoneGroundOwner ZoneCanYard ZoneAutoOwner ZoneYardPlots; Zones.cpp: claim_yard ZoneBoundaryBetween; Walkers.cpp: EdgeOpen; ApplicationChasmSelect.cpp: YardLines GroundPatch GroundSiteCard; ZoneMesh.cpp: GroundSiteCell
verified: 2026-10-26
---

A yard is ground painted on plots that belongs to a building: **[[Garden|gardens]]**, green and walled, and
**lots**, trodden earth behind a fence of stakes. A [[House|house]] may own both, a [[Woodcutter|woodcutter's hut]]
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
[[Wood|wood]] per unit of its area, built from a plot beside it. Until its wood is in it is open ground,
staked out as a [[Field|field]] site is: a cord round the site, unbroken where plots of the same ground
and owner meet. A click on it selects the whole joined patch of that ground and owner, standing plots
too, while any of it is a site. Its card shows the wood brought and still wanted, the plots to build,
whose yard it is and who is carrying for it. Ground cannot be put first.

**Trees on it**: a lot waits for a [[Woodcutter|woodcutter]] to fell them, as any site does. A garden keeps its
broadleaf trees and plants, and waits only for its pines ([[Garden]]).

## Walls, fences and gates

A standing yard is walked round. Its wall or fence is crossed only at a **gate**, where a
[[Roads and walls|road]] ends against it; a road is never painted over a garden or lot. Nobody is meant
to go in: a woodpile is reached from its hut. Someone a fence went up around may climb out over it, but
nobody climbs in. Two yard plots side by side have no fence between them, whatever their kind or owner.

## What it does

- **A woodcutter's lots** add to what his hut keeps indoors: {ECONOMY_LOT_ROOM} [[Wood|wood]] per unit of
  each standing lot plot's area, piled on the lowest lot plot beside the hut ([[Woodcutter]]). A hut too
  small to keep a log indoors needs one to work.
- **A house's gardens and lots** do nothing yet.

## Planned

- [planned] A yard's plots touch its building itself, never only its yard, so the building is the door to
  every plot of it. A plot painted on from the yard and not beside the building is no longer its.
- [idea] What a garden does: food for its family, or comfort ([[Garden]]).
- [idea] Other buildings with a yard: a store's lot as outdoor storage, a builders' hut's timber yard.

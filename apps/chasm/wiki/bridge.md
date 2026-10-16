---
title: Bridge
aliases: bridges
kind: buildings
status: built
summary: A wooden span from one dry bank of a river to the other, placed whole and built like any building. It crosses rivers, never lakes or the chasm.
plans: bridge_plan.md > Decided; bridge_plan.md > As built; bridge_plan.md > Trees across the water (the user, 2026-10-16)
code: Zones.h: ZONE_KIND_BRIDGE ZONE_BRIDGE_SPAN_RIVERS ZoneBridgeChain ZoneBridgeWalkable ZONE_OP_SITE_CANCEL; Economy.cpp: Settle RouteTo WoodNear Derive; Walkers.h: WALK_WHO_ICE
verified: 2026-10-16
---

A river's wet band, the water and the stone bank on either side, can't be walked on or built on, so
until there is a bridge a river splits the land.

## Placing one

A bridge is drawn as one straight drag from a dry plot on one bank to a dry plot on the other, and is
placed whole, never plot by plot. It is allowed only when:

- both ends are dry and every plot between them is wet, all on one ground level;
- nothing is built on any of its plots, and it keeps clear of a waterfall's lip;
- the water it crosses is at most {ZONE_BRIDGE_SPAN_RIVERS} times the river's usual width, so rivers can
  be crossed and lakes can't.

## Building and crossing

It is a [[Construction|building site]] like any other, built from [[Wood|wood]] (less per plot than a
house's storey) by people without a job of the [[Settlement|settlement]] either end lies in, from the banks
inwards. Once a plot of it stands, people walk
across it at road speed ([[Roads and walls]]), and a road leading onto it carries on as road.

## Trees on the far bank

Nothing of it rises while a [[Tree|tree]] stands on any of its plots, and the far end is across the water,
where no [[Woodcutter|woodcutter]] walks. So a bridge with trees there **waits for winter**: once the river
freezes a woodcutter crosses the [[Seasons|ice]] for them, and brings the logs back the same way. Dragging one,
the price tag says "ACROSS THE WATER: WAITS FOR THE ICE"; placed, NEEDS ATTENTION names it ("BRIDGE: TREES IN
THE WAY"), a click selects it, and its card says why. A river south of the snow's reach never freezes: the tag
says "NO WOODCUTTER NEAR" then, as it does with no woodcutter in reach at all.

**Cancel** on its card takes the whole site off while none of it stands, leaving what was brought in a
[[Crate|crate]] on its near bank ([[Construction]]).

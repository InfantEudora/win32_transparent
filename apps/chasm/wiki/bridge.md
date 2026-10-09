---
title: Bridge
aliases: bridges
kind: buildings
status: built
summary: A wooden span from one dry bank of a river to the other, placed whole and built like any building. It crosses rivers, never lakes or the chasm.
plans: bridge_plan.md > Decided; bridge_plan.md > As built
code: Zones.h: ZONE_KIND_BRIDGE ZONE_BRIDGE_SPAN_RIVERS ZoneBridgeChain ZoneBridgeWalkable
verified: 2026-10-09
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
house's storey) by people without a job, from the banks inwards. Once a plot of it stands, people walk
across it at road speed ([[Roads and walls]]), and a road leading onto it carries on as road.

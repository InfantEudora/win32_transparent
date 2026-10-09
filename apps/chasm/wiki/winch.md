---
title: Winch
aliases: winches
kind: buildings
status: built
summary: A one-plot building on the rim right above a balcony. People ride it down to the balcony and back up.
plans: buildings_plan.md > Step 2, the winch
code: Zones.h: ZONE_KIND_WINCH ZoneWinchLanding; Walkers.h: WINCH_RIDE_SECONDS
verified: 2026-10-09
---

A winch is the only way between the plateau and a [[Terrain|balcony]] on the chasm's wall.

- It is **one plot** on the rim, right above a balcony: its plot on the plateau (or on a higher balcony),
  with the balcony's ground among its cells' corners. It is the one building allowed that close to a
  cliff.
- It lowers to its **landing**, the nearest balcony plot, on which nothing may be built.
- People walk through the winch's plot rather than into it. The ride takes {WINCH_RIDE_SECONDS:#seconds}
  either way, however long the drop, and walkers plan their routes over it like any other path.
- One balcony can winch down to another, lower one.
- It is built like any [[Construction|site]], and takes more wood than a house's storey of the same size.

Balconies can hold anything but fields, so whoever lives down there eats what comes down from the rim.

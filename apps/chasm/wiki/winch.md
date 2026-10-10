---
title: Winch
aliases: winches
kind: buildings
status: built
summary: A one-plot building on the rim right above a balcony. People ride it down to the balcony and back up.
plans: buildings_plan.md > Step 2, the winch; goods_plan.md > Step 4: rope as a cost
code: Zones.h: ZONE_KIND_WINCH ZoneWinchLanding; Walkers.h: WINCH_RIDE_SECONDS; Economy.h: ECONOMY_WINCH_ROPE
verified: 2026-10-10
---

A winch is the only way between the plateau and a [[Terrain|balcony]] on the chasm's wall.

- It is **one plot** on the rim, right above a balcony: its plot on the plateau (or on a higher balcony),
  with the balcony's ground among its cells' corners. It is the one building allowed that close to a
  cliff.
- It lowers to its **landing**, the nearest balcony plot, on which nothing may be built.
- People walk through the winch's plot rather than into it. The ride takes {WINCH_RIDE_SECONDS:#seconds}
  either way, however long the drop, and walkers plan their routes over it like any other path.
- One balcony can winch down to another, lower one.
- It is built like any [[Construction|site]], and takes more wood than a house's storey of the same size,
  and **{ECONOMY_WINCH_ROPE} [[Rope|rope]]**. The colony starts with no rope, so the first winch waits for a
  [[Hemp|hemp]] field and a [[Ropewalk|ropewalk]] to make it: its site stands staked out until then, its card
  saying what is missing, and NEEDS ATTENTION lists it (WINCH: NEEDS ROPE) until both are there.

Balconies can hold anything but fields, so whoever lives down there eats what comes down from the rim.

## Planned

- [idea] Its rope wears as it hauls and is replaced now and then - rope as an upkeep input.

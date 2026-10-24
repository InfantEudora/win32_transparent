---
title: Flight
aliases: flying, fly, hover, hovering, set down, setting down, moor, mooring, berth
kind: systems
status: partial
summary: A finished zeppelin flies where it is sent with a right click - to hover over the chasm, set down by an edge, or moor at a dock. It flies only over the chasm, and clears the clouds as it goes.
plans: flight_plan.md > Decided (the user, 2026-10-24); flight_plan.md > As built: step 1 (2026-10-24)
code: Economy.h: ZEPPELIN_SPEED ZEPPELIN_TURN ZEPPELIN_CLIMB ZEPPELIN_CRUISE ZEPPELIN_MAST_CLEAR ZEPPELIN_CLEAR ZEPPELIN_SET_DOWN_REACH ZEPPELIN_SET_DOWN_STEP EconomyFlyable EconomySetDownSpot; Flight.cpp: SendZeppelin TickFleet DockBerthFree ZeppelinFinished; ApplicationChasmFleet.cpp: UploadFleet SendZeppelinTo
verified: 2026-10-24
---

Once its last stage stands, a [[Zeppelin|zeppelin]] is a craft of its own - the colony's, not its
[[Zeppelin dock|dock's]] - moored on the dock it was built on. It needs nobody to fly it and no fuel, and it flies
in winter too.

## Sending it

Click it to select it - by its envelope or its gondola - and **right-click** where it is to go:

- **On a dock** with its berth free, it **moors** there: one standing whole that a zeppelin fits on, whose own
  zeppelin is finished, with no other moored on it or on its way there.
- **On land** - the rim, a [[Terrace|balcony]], an island - it **sets down** beside it: alongside the edge, side-on,
  its deck a step over the ground, at the plot's own height. It comes alongside within {ZEPPELIN_SET_DOWN_REACH}
  of the plot, so a plot away from the edge is refused: "not at an edge".
- **Over the open chasm**, it flies to where the cursor points at its height and **holds still** there.

Anywhere else is refused, saying why, as an [[Posts|order]] is. Sent while flying, it turns for the new place from
where it is.

## How it flies

- **Only over the open chasm**, {ZEPPELIN_CLEAR} inside it, but for the short way off a dock and onto one: from the
  berth sideways to the nearest point over the chasm. Its way is found when it is sent, straight wherever it sees
  over the chasm and round its bends where it must.
- **At {ZEPPELIN_CRUISE} over the rim**, so nothing standing in the chasm is in its way - and at least
  {ZEPPELIN_MAST_CLEAR} over a dock's ground off it and onto it, over the mast.
- In turn: it rises where it is, flies its way at up to {ZEPPELIN_SPEED} units a second - slowing to turn, turning at
  most {ZEPPELIN_TURN} radians a second - turns to how it will lie at the end, and comes down there.
- **It sees from the air**: it clears the [[Exploration|clouds]] round it as a person does from the ground, from its
  deck - high over the chasm, far.

Its card says what it is doing and where, and how to send it. A dock's card says whether its zeppelin is moored
there or away, and whether its berth is free.

## How it looks

Off its mooring it bobs on the air and leans into its turns, and its two propellers turn with its pace - idling
while it holds still, still when moored. The dock draws its mooring lines while one is moored on it.

## Planned

- [planned] People aboard and off by order, with their packs and tents; the cabin their pantry (`flight_plan.md`,
  step 2).
- [planned] Masts on any edge, and goods loaded there and unloaded into a mast's store or as a crate (step 3).
- [planned] A new zeppelin begun on a dock once its own is away (step 4); routes, with porters (step 6).
- [idea] Losing a zeppelin - the winter storm the favourite cause (step 7).

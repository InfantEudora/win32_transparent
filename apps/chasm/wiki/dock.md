---
title: Zeppelin dock
aliases: dock, docks, zeppelin docks, mooring mast, mast
kind: buildings
status: built
summary: Painted over cells as a field is, at the chasm's edge. Once it stands, a zeppelin is built on it, and moored to its mast - and any zeppelin may moor there while its berth is free.
plans: steam_plan.md > The zeppelin dock, and building the zeppelin; steam_plan.md > As built: step 5 (2026-10-23); flight_plan.md > As built: step 1 (2026-10-24)
code: Zones.h: ZONE_KIND_DOCK ZONE_DOCK_MAX_CELLS ZONE_DOCK_REACH ZONE_DOCK_FIT_LENGTH ZONE_DOCK_FIT_WIDTH ZoneCanDock ZoneDockFrame; Economy.h: ECONOMY_DOCK_WOOD_PER_CELL; Economy.cpp: ZeppelinSite; Flight.cpp: DockBerthFree; ApplicationChasmDock.cpp: UploadDocks
verified: 2026-10-24
---

Where a [[Zeppelin|zeppelin]] is built. Its tool is on the build bar with no key (the user's: the keys are all
taken), and comes with the chasm's [[Discoveries|discovery]], with the [[Steam collector|steam works]]. It is dragged
over the large squares as a [[Field|field]] is, at most {ZONE_DOCK_MAX_CELLS} of them - a drag goes on with a new dock
past that - and built as a [[Construction|site]], {ECONOMY_DOCK_WOOD_PER_CELL} [[Wood|wood]] a cell, every cell at once.

## Where it goes

- **At the chasm's edge** (the user's, 2026-10-23): every cell's middle within {ZONE_DOCK_REACH} of it, since a
  zeppelin floats only over the chasm. On the rim, a [[Terrace|balcony]] or an island - unlike a field, which no
  balcony takes.
- A field's rules otherwise: flat, dry, off the mountain, nothing built or laid on it.
- **On one floor**: a cell across a cliff from the rest of the dock is refused.

## Fitting a zeppelin

A zeppelin lies along the dock's long way, its middle on the dock's middle and its nose toward the chasm. It
**fits** when what of it stands on the ground - the scaffold's frames and trestles, the gondola over them, a
{ZONE_DOCK_FIT_LENGTH} by {ZONE_DOCK_FIT_WIDTH} oblong - lies wholly on the dock's cells; the envelope overhangs in
the air. Tried along the long way, then turned a little at a time. About 2 by 4 squares fit; on a crooked part of
the map it may want a different 8.

A dock nothing fits on takes no zeppelin: its card says it is too small, and NEEDS ATTENTION lists it as DOCK:
TOO SMALL FOR A ZEPPELIN.

## Its berth

Once its zeppelin is finished, the dock is a **berth**: its zeppelin leaves from it and comes back to it, and any
other may [[Flight|moor]] there while none is moored on it or on its way to it. Its card says whether its zeppelin
is moored there or away, and whether the berth is free. It builds no second zeppelin for now.

## How it looks

Trodden ground, fenced, as an [[Archery range|archery range's]] cells, and at its nose's end a **mooring mast**: a
timber tower with a deck at its head and a brass cup the zeppelin's nose is moored into. The scaffold and the
zeppelin stand on it as they are built; its mooring lines are drawn while a zeppelin is moored on it.

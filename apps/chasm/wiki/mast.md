---
title: Mooring mast
aliases: mast, masts, mooring masts, cargo, load list, loading, unloading, unload
kind: buildings
status: built
summary: A mast at a drop into the chasm where a zeppelin moors nose-in, to be loaded by the builders from its load list and unloaded into the mast's store. The dock is a mast too.
plans: flight_plan.md > Goods and the mast; flight_plan.md > As built: step 3, the mast and goods (2026-10-25)
code: Zones.h: ZONE_KIND_MAST ZONE_MAST_CUP ZONE_MAST_PAST_LIP ZoneMastAt; Economy.h: ZEPPELIN_CARGO ZEPPELIN_NOSE ZEPPELIN_NOSE_Y ECONOMY_MAST_ROOM ECONOMY_MAST_ROPE CRATE_CARGO EconomyMastBerth EconomyKindStores EconomyLoadShort; Flight.cpp: MastBerthFree SetLoad Unload CabinLeft; Economy.cpp: FindSiteWork Derive; ApplicationChasmDock.cpp: UploadDocks
verified: 2026-10-25
---

Where a [[Flight|zeppelin]] moors away from its [[Zeppelin dock|dock]], to take goods aboard and put them off. Its
tool is on the build bar with no key, as the dock's, and comes with the chasm's [[Discoveries|discovery]].

## Where it goes

**One plot at the edge of a drop** into the chasm - the rim, a [[Terrace|balcony]], an island - where a
[[Steam collector|steam collector]] could stand, by the same rule: placed by a click, not dragged. It is built as a
[[Winch|winch]] is, of [[Wood|wood]] and {ECONOMY_MAST_ROPE} [[Rope|rope]].

It takes a zeppelin only with room for one: nothing under where it would hang, and an open plot beside the mast to
step off onto. Its card says when it has neither.

## Mooring

A zeppelin sent onto a mast - a right click on it with the zeppelin selected - moors **nose-in**: its nose in the
mast's cup, its body out over the drop, as high as it stands on a dock. Its **landing** is the open plot beside the
mast nearest the cup: [[Passengers|passengers]] go aboard and step off there, by a gangway from the bow. One zeppelin to
a mast at a time.

## Its store

A mast holds {ECONOMY_MAST_ROOM} of any good, a zeppelin's whole cargo - and so does a [[Zeppelin dock|dock]], whatever
its size. It holds **what zeppelins unload there**: no carrier delivers to it, but anyone takes from it as from a
[[Store|store]] - to eat, to build, to load the next zeppelin.

## Cargo and the load list

- A zeppelin carries up to {ZEPPELIN_CARGO} goods in its **cargo**, apart from its cabin.
- Its card has a **load list**: a good, and how much of it. While it is moored at a mast or a dock, that settlement's
  **builders** bring what the list still wants - ahead of their construction, as a crate to pick up is - from
  wherever a site's goods come from, the mast's own store nearest. What they bring goes into the cabin while it lacks,
  then the cargo.
- **Unload**, on its card, while it is down: moored, into the mast's or dock's store as far as there is room, the rest
  a [[Crate|crate]] on the landing; set down anywhere else, all of it a crate on the edge plot. In the air nothing is
  unloaded.

## How it looks

The [[Zeppelin dock|dock's]] mooring mast: a timber tower with a deck and rail at its head, a ladder up one face, and
a brass cup facing out over the drop.

## Planned

- [planned] Routes - a load each way, again and again - with porters (`flight_plan.md`, step 6).

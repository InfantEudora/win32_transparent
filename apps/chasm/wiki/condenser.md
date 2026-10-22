---
title: Condenser
aliases: condensers, condenser hand, condenser hands
kind: buildings
status: built
summary: Turns steam into water - straight from a steam collector beside it, or from steam its hands bring from a store. Water where no river is.
plans: steam_plan.md > Decided (the user, 2026-10-21); steam_plan.md > The condenser
code: Zones.h: ZONE_KIND_CONDENSER; Economy.h: WORKER_JOB_CONDENSER ECONOMY_CONDENSER_SECONDS EconomyCondenserFeeds; Economy.cpp: FeedCondenser AtCondenser WorkplaceMakes; BuildingMesh.cpp: CONDENSER_TANK_W
verified: 2026-10-21
---

**[[Water]] where no river is**: on a balcony or an island, or on the far rim. Its tool (**L**) comes with the
chasm's discovery, with the [[Steam collector|steam collector]]'s.

## Where it goes

Dragged, up to {ZONE_BUILDING_MAX_PLOTS:#plots}, wherever a [[House|house]] could stand. **Beside a steam collector** -
a plot of it sharing an edge with one of the collector's - it draws that collector's steam straight from its hold, with
nobody carrying it.

## Condensing

A unit of [[Steam|steam]] into a unit of water every {ECONOMY_CONDENSER_SECONDS:#seconds} **for each hand it has** - one
for every {ECONOMY_PLOTS_PER_WORKER:#plots} standing - up to what it holds, which is what a water collector of its floor
would. **Snow does not stop it.**

- **Its steam comes from a collector beside it first**, the lowest-numbered with any, a unit at a time. A rim
  collector of the same size keeps one condenser busy and no more; one lower down gathers faster than a condenser
  of its size uses, so the rest builds up there and its carrier takes it on as steam - or a second condenser beside
  it takes it. A condenser that outpaces its collector stands waiting on it.
- **With no collector beside it**, its hands fetch steam: once what was brought is used up, one walks to the
  nearest [[Store|store]], camp or [[Crate|crate]] with steam to spare and brings a load ({ECONOMY_LOAD}).

## The hands

**Condenser hands**, by ingenuity. Once a load of water waits and a store has room, one takes it to the nearest store
that takes water - never the steam waiting to be condensed. A store beside it takes steam and water. The thirsty drink
at it as at a [[Water collector|water collector]], waiting there for the next unit while it condenses, and houses
[[Fetching home|fetch]] its water. Its card can stop work.

## Seeing it

A water collector's stone walls under timber, and a brass tank through the roof of every plot. Its card says where its
steam comes from and how much waits at the collector beside it, or that there is none.

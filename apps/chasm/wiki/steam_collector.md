---
title: Steam collector
aliases: steam collectors, cowl, hood, steam carrier, steam carriers
kind: buildings
status: built
summary: A shed at the edge of a drop into the chasm, its hood out over the updraft, filling with steam while it has carriers - more for every step down it stands.
plans: steam_plan.md > Decided (the user, 2026-10-21); steam_plan.md > The steam collector
code: Zones.h: ZONE_KIND_STEAM; Zones.cpp: SteamIsBuildable; Economy.h: WORKER_JOB_STEAM ECONOMY_STEAM_SECONDS ECONOMY_STEAM_PER_STEP EconomySteamYield; Climate.h: ClimateStepsDown; BuildingMesh.cpp: BuildSteamHood SteamHoodTop; ApplicationChasmHearth.cpp: UploadSteamPlumes
verified: 2026-10-21
---

The [[Water collector|water collector]]'s twin over the chasm: it catches [[Steam|steam]] rising out of it. Its tool
(**P**) comes with the chasm's discovery, as the [[Winch|winch]]'s does.

## Where it goes

Dragged, up to {ZONE_BUILDING_MAX_PLOTS:#plots}, along **the edge of a drop into the chasm** - on the rim, on a
balcony's outer edge or on an island's: some corner of its cells lower than its plot, so it leans out over the drop,
and none higher, so it never stands against a wall. The drop may be the floor under the clouds, the rim's crumbled
steps or a terrace below; a cliff painted by hand is not the chasm. With the winch and the [[Mine|mine]] it is the
third building allowed against a cliff. Otherwise a house's rules: dry, off the mountain, no field, gentle, one storey.

## Filling

It fills by one unit every {ECONOMY_STEAM_SECONDS:#seconds} **for each carrier it has** - one for every
{ECONOMY_PLOTS_PER_WORKER:#plots} standing - on the rim, and **{= ECONOMY_STEAM_PER_STEP * 100}% more for every step
down** its plot stands below the rim, the steps the [[Temperature|temperature]] counts too. A balcony two steps down
gathers at 1.7 times the rim's pace, the lowest islands near three times. **Snow does not stop it**: the chasm keeps
its edge warm.

It holds what a water collector of its floor would - a load, and more for the floor past its pump - and fills on
until full.

## The carrier

A **steam carrier**, by ingenuity. Once a load ({ECONOMY_LOAD}) waits and a [[Store|store]] has room for it, he walks
over, picks it up and takes it to the nearest store that takes steam - up a winch from a balcony, as anyone would. A
store beside the collector takes only steam, and a [[Condenser|condenser]] beside it takes its steam straight from its
hold. Its card can stop work. Only people of its own
[[Settlement|settlement]] work it, so one on the far rim or a balcony with no winch waits for a camp of its own.

## Seeing it

A plank shed under slate, a post at the lip with a boom out over the drop, and hanging from its end a brass hood open
below, its neck piped back to the roof. While it gathers - worked, not stopped, not full - a white plume rises from the
hood, so one at work shows from one standing idle. The card says how far down it stands and what that is worth.

## Planned

- [idea] The plume leaning in the wind as chimney smoke does.

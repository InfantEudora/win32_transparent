---
title: Well
aliases: wells, debug well
kind: buildings
status: built
summary: A debug tool for test maps with no river - one plot of water anyone may take, as much as the seed gives it, or endless.
plans: water_tools_plan.md > The well; water_tools_plan.md > As built: step 1
code: Zones.h: ZONE_KIND_WELL ZoneKindOnePlot; Economy.h: ECONOMY_WELL_WATER_MIN ECONOMY_WELL_WATER_MAX ECONOMY_WELL_ENDLESS EconomyWellWater EconomyWellEndless; Economy.cpp: FollowZones SetStock FetchFrom KeepTowers; BuildingMesh.cpp: BuildWell
verified: 2026-10-18
---

A well is **a debug tool**, for test maps, which have no river and so no [[Water collector|collector]]. It is
placed from the debug panel's Paint tools - no key, not on the build bar - and **never in play mode**: the
tool is locked there, and a play command for one is refused.

## Placing it

One plot, placed with a click, wherever a [[House|house]] could stand. It needs no water near, and it stands
at once, as everything painted in debug does.

## Its water

It holds [[Water|water]] and nothing else, **as much as the seed gives it**: between {ECONOMY_WELL_WATER_MIN}
and {ECONOMY_WELL_WATER_MAX}, from the map's seed and the well's plot, so the same well on the same map always
holds the same. A unit is worth {NEEDS_WATER_WORTH:days} of an adult's water.

Anyone may take from it, with nobody working it:

- the **thirsty** drink there as at a [[Store|store]] ([[Thirst]]);
- **houses** fetch water home from it ([[Fetching home]]);
- the **caretakers** fill [[Tower|guard towers]] from it.

Nobody carries water *to* it, and the top bar's water does not count it. Drawn dry, it has nothing to offer
and is passed over; it stands until erased. Snow does not stop it, and a [[Camp|camp]] beside one keeps its own
water carrier.

## Endless

`chasm_economy` op `set_stock` with `endless` true keeps a well at {ECONOMY_WELL_ENDLESS} for good, put back
every tick. Setting its water, or `endless` false, makes it finite again. Saved, and the same in a replay.

## Seeing it

A ring of stone with the water dark in it, two posts with a windlass and its rope, a small red roof and a
bucket on the rim - set off its plot's point, so whoever comes for water stands beside it. Its card shows the
water left, or that it never runs dry; `chasm_economy` op `buildings` shows `water` and `endless`.

## Planned

- [idea] A well as a spot on the release game's map, generated with it as a [[Deposit|deposit]] is and holding
  its amount by the same rule - found, built over, and drawn dry. For the far side, which has no rivers.

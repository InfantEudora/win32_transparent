---
title: Camp
aliases: camps, tent, tents, supplies, camp's supplies
kind: buildings
status: built
summary: Tents and fires where the settlers start. It is home to anyone without a house, and holds the supplies the settlers brought.
plans: people_plan.md > As built; needs_plan.md > As built: step 2; land_plan.md > As built
code: Zones.h: ZoneStartCampPlot ZoneCampTents ZONE_CAMP_FIRE_EVERY; Economy.cpp: HouseFamilies Derive FetchHome Heat
verified: 2026-10-09
---

A new map starts with one camp, just big enough for the settlers. The game puts it on the best spot it
can find along a river: flat ground for houses and fields, with forest at hand but not all around
(`ZoneStartCampPlot`). The player can paint more camps.

## Living there

A camp is built like a house, one storey high. Each plot gets a **tent**, except about one plot in
{ZONE_CAMP_FIRE_EVERY}, which gets a fire. A camp holds **one person to a tent**, from any number of
families. People in a camp are never indoors: they stand in front of their tents, so a crowded camp shows
that there are too few [[House|houses]].

A family leaves the camp as soon as a house has room for all of them.

## The supplies

The settlers arrive with {ECONOMY_START_WOOD} [[Wood|wood]], {ECONOMY_START_WHEAT} wheat,
{ECONOMY_START_BEANS} beans and {ECONOMY_START_WATER} [[Water|water]] in the camp, so the first buildings
can go up before there is a [[Woodcutter]].

- The camp's own people eat and drink from the supplies, but never [[Fetching home|fetch]] more.
  Running short is the reason to build houses.
- **The supplies belong to the colony.** Houses fetch from the camp as they would from a [[Store]], and
  builders take its wood. A camp with people in it keeps its wood for its own fires, though, so no house
  fetches firewood from it.
- While no house is lived in, the [[Field|fields]] belong to the camp, and their harvest goes into its
  supplies.

## Winter

At dawn under the snow, a camp with people in it burns {ECONOMY_FIREWOOD_BURN} wood for every plot, tents
and fires alike. If it doesn't have enough, its people are cold that day ([[Cold]]).

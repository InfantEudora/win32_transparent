---
title: Camp
aliases: camps, tent, tents, supplies, camp's supplies
kind: buildings
status: built
summary: Tents and fires where the settlers start. It is home to anyone without a house, and holds the settlers' supplies for the whole colony beside a pantry of its own.
plans: people_plan.md > As built; needs_plan.md > As built: step 2; land_plan.md > As built; woodcutting_plan.md > Decided (the user, 2026-10-11)
code: Zones.h: ZoneStartCampPlot ZoneCampTents ZONE_CAMP_FIRE_EVERY; Economy.cpp: HouseFamilies Derive FetchHome Heat OpenPantries HomeHeld AssignJobs LandPlaces; Economy.h: pantry
verified: 2026-10-11
---

A new map starts with one camp, just big enough for the settlers. The game puts it on the best spot it
can find along a river: flat ground for houses and fields, with forest at hand but not all around
(`ZoneStartCampPlot`). The player can paint more camps. The start camp founds the first [[Settlement|settlement]]:
the land within a walk of it where its people work; a camp outside every settlement would found another.

## Living there

A camp is built like a house, one storey high. Each plot gets a **tent**, except about one plot in
{ZONE_CAMP_FIRE_EVERY}, which gets a fire. A camp holds **one person to a tent**, from any number of
families. People in a camp are never indoors: they stand in front of their tents, so a crowded camp shows
that there are too few [[House|houses]].

A family leaves the camp as soon as a house has room for all of them.

## Supplies and pantry

A camp holds two stocks.

- **The supplies** are what the settlers arrived with: {ECONOMY_START_WOOD} [[Wood|wood]],
  {ECONOMY_START_WHEAT} wheat, {ECONOMY_START_BEANS} beans and {ECONOMY_START_WATER} [[Water|water]], so
  the first buildings can go up before there is a [[Woodcutter]]. They belong to the colony, whoever
  lives there: houses fetch from them as from a [[Store]], and builders take their wood. Only firewood is
  held back: no house fetches it from a camp people live in.
- **The pantry** is what the camp's own families [[Fetching home|fetch]], as a [[House|house's]] family
  does, with a house's room and woodshed for the camp's floor area. It is theirs alone.

The camp's people eat, drink and burn from the pantry first, then the supplies, and fetch when the two
together run low, never from their own supplies. **When the last of them leaves, the pantry joins the
supplies** at once.

While no house is lived in, the [[Field|fields]] belong to the camp, and their harvest goes into its
supplies.

## Its work

A camp employs **{ECONOMY_CAMP_FARMERS:#farmer}** for its land (more of its fields lie fallow) and
**{ECONOMY_CAMP_WOODCUTTERS:#woodcutter}** of its own, both from those living there. The camp's woodcutter
works with no hut: a log is worth only {ECONOMY_CAMP_LOG_WOOD} wood, and goes into a store that takes wood,
or else the supplies, while they hold less than {ECONOMY_CAMP_WOODPILE}. More of either takes
[[House|houses]] and [[Woodcutter|huts]].

## Winter

At dawn under the snow, a camp with people in it burns {ECONOMY_FIREWOOD_BURN} wood for every plot, tents
and fires alike, from its pantry first. If it doesn't have enough, its people are cold that day
([[Cold]]).

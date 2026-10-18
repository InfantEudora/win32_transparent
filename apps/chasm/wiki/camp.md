---
title: Camp
aliases: camps, tent, tents, supplies, camp's supplies
kind: buildings
status: built
summary: Tents and fires where the settlers start. It is home to anyone without a house, and holds the settlers' supplies for the whole colony beside a pantry of its own.
plans: people_plan.md > As built; needs_plan.md > As built: step 2; land_plan.md > As built; woodcutting_plan.md > Decided (the user, 2026-10-11); settlements_plan.md > Founding: as built, steps 2, 3, 5 and 6
code: Zones.h: ZoneStartCampPlot ZoneCampTents ZONE_CAMP_FIRE_EVERY ZoneCampFireTent; Economy.cpp: HouseFamilies Derive FetchHome Heat OpenPantries HomeHeld AssignJobs LandPlaces EconomyColonyCamp Settle; Economy.h: pantry
verified: 2026-10-18
---

A new map starts with one camp, just big enough for the settlers. The game puts it on the best spot it
can find along a river: flat ground for houses and fields, with forest at hand but not all around
(`ZoneStartCampPlot`). The start camp founds the first [[Settlement|settlement]]: the land within a walk of it where
its people work. **A settlement has one camp**: a tent painted or pitched anywhere in it is that camp's, its tents
needn't touch, and a camp outside every settlement founds another. Every tent counts as a bed toward its reach.

## Living there

A camp is built like a house, one storey high. **Every plot is a tent** - a drag pitches one on each, with no limit - and a camp holds **one person to a
tent**, from any number of families. Its fires are props around the tents, never in a tent's place: about
one in {ZONE_CAMP_FIRE_EVERY} spots of open ground beside a tent at the camp's edge has one, with a log to
sit on. A tent hemmed in by other tents has no room for a fire.

A tent can be **packed and taken away** by someone sent out, and **pitched** where they stand, which makes a new
camp or joins one beside it ([[Founding|founding]]). A pitched tent stands at once; it was packed, not built. People in a camp are never indoors: they stand in front of their tents, so a crowded camp shows
that there are too few [[House|houses]]. A family leaves the camp as soon as a house has room for all of them.

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

A camp employs **{ECONOMY_CAMP_FARMERS:#farmer}** for its land (more of its fields lie fallow), from those living
there, and **{ECONOMY_CAMP_WOODCUTTERS:#woodcutter}**, a water carrier and a picker of its own - the **colony's**:
those living there first, then anyone in the colony **no other workplace needs**, so they go on once everyone is
in [[House|houses]]. The camp's woodcutter works with no hut: a log is worth only {ECONOMY_CAMP_LOG_WOOD} wood, into
a store that takes wood or else the supplies, while they hold less than {ECONOMY_CAMP_WOODPILE}. Once a
[[Woodcutter|hut]] stands in the settlement, the camp has no woodcutter of its own.

A camp with a river's or pool's edge within {ECONOMY_CAMP_WATER_REACH} of walking also employs a **water
carrier**, the most ingenious: he walks to the bank, draws {ECONOMY_CAMP_WATER_LOAD} [[Water|water]] at
{ECONOMY_CAMP_WATER_SECONDS:#seconds} a unit, and carries it into the supplies, while they hold less than
{ECONOMY_CAMP_WATER_HOLD} and no snow lies on the bank. That is enough for a few, not a village, which wants a
[[Water collector|collector]] - and once one stands in the settlement, worked or stopped, the camp has no water
carrier of its own. A **picker** brings in [[Fruit|fruit]] where a plant bears in its land. A painted tent costs {ECONOMY_TENT_ROPE} [[Rope|rope]] with its wood.

When a camp's last tent is packed or erased, its supplies and pantry are left where it stood as a [[Crate|crate]].

## Winter

At dawn under the snow, a camp with people in it burns {ECONOMY_FIREWOOD_BURN} wood for every plot, tents
and fires alike, from its pantry first. If it doesn't have enough, its people are cold that day
([[Cold]]).

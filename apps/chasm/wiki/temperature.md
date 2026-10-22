---
title: Temperature
aliases: temperatures, degrees, the chasm's heat, warmth, readout, wind readout
kind: world
status: partial
summary: A temperature for every place and date - the air, and the chasm's warmth on top, more for every step down into it - shown with the wind under the calendar.
plans: steam_plan.md > Decided (the user, 2026-10-21); steam_plan.md > The chasm's heat, shown
code: Climate.h: ClimateYear ClimateAir ClimateStepsDown ClimateChasm; ApplicationChasmWeather.cpp: UpdateWeather; ApplicationChasmTime.cpp: DrawHeatReadout READOUT_CALM
verified: 2026-10-21
---

Every place has a temperature, in degrees, worked out from the date and the map alone - nothing about it is
saved or stepped, so it is the same in a pause and a replay. **Nothing in the game obeys it yet**: it is how the
player sees that the chasm is warm.

## The air

- **The year**: a smooth curve from {CLIMATE_WINTER} at its coldest, a few days into winter as the snow's front
  stops, to {CLIMATE_SUMMER} half a year later. A new colony's first morning is about freezing.
- **The north** is colder: the map's north edge {CLIMATE_NORTH} below its south, the middle as the year says.
- **The storm** that travels with the snow's front ([[Seasons]]) takes up to {CLIMATE_STORM} off.
- **Under snow** the air is never warmer than {CLIMATE_UNDER_SNOW}.

## The chasm's warmth

On top of the air, wherever the chasm reaches:

- **{CLIMATE_EDGE} at its open edge** and over all of it, fading to nothing {CLIMATE_EDGE_REACH} inland from the rim.
- **{CLIMATE_PER_STEP} more for every step down** a [[Terrain|balcony or island]] stands below the rim. Ground on the
  plateau counts no steps however low it lies, so a river's channel or a painted cliff is not warmed. The same steps
  set a [[Steam collector|steam collector]]'s yield.

The snow does not follow it: a northern balcony can read above freezing under winter's snow.

## The readout

A strip under the calendar, top right, in every build: the temperature where the view is centred, in pale
blue below freezing; rising orange strands while the chasm's warmth is in it, brighter the more; and the
wind - an arrow the way it blows on screen, and CALM (under {READOUT_CALM} a second), BREEZE, WIND or STORM.

`chasm_time` returns it under `weather`'s `heat`: the air, the chasm's share, the total, the steps down and
whether snow lies there.

## Planned

- [proposed] The snow following the heat: none where the chasm keeps the ground above freezing - a green band
  along the rim and on the terraces in winter.
- [idea] The heat drawn in the world: a shimmer over the rim, plumes from the chasm in the cold.

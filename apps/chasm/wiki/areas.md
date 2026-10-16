---
title: Areas
aliases: discovery, discoveries, discovered, waterfall, waterfalls
kind: world
status: built
summary: The parts of the map there are to find - each river, each waterfall, each biome, the open plateau and the forest - found when someone first sees them.
plans: areas_plan.md > Areas; areas_plan.md > The discovered event; areas_plan.md > The water heard
code: Areas.h: AREA_RIVER AREA_WATERFALL AREA_PLATEAU AREA_FOREST AREA_FALL_REACH BuildAreas; ApplicationChasmExplore.cpp: LookForAreas TickExploration; Economy.h: EVENT_DISCOVERED; ApplicationChasmSound.cpp: SignalWater; Forest.h: ForestData
verified: 2026-10-16
---

An **area** is a part of the map there is to discover. They come with the map, so every map of a seed has
the same ones:

- **each river**, branch and stream, along its water;
- **each waterfall**, {AREA_FALL_REACH} round its lip;
- **each biome** as one area: the mountain, a mountain meadow, the swamp, the desert;
- **the starting land**, as two: its open ground (the plateau) and its forest, where the forest's
  dense cores are. The chasm's floor, its balconies and islands are no area yet.

## Found

An area is **found** the moment any of its ground is first [[Exploration|explored]]. Whoever saw it
found it, and it goes into the [[Statistics|journal]]: "Anouk Wever found a river".

- **Only the first of its kind is told** at the bottom of the screen: the first waterfall, not the
  second. The others are found all the same, and kept in the journal.
- **The landing is not news.** What the settlers see on their first look round is home: it counts as
  found, and none of it is told.
- **Nothing extra is saved.** After a load, which areas are found is worked out again from the explored
  ground, and nothing is told twice.

## Heard before seen

The water can be heard from a distance (the sound plan's water loops): the nearest river's stream and
the nearest waterfall's roar, louder the nearer the middle of the view is to it and quieter zoomed out.
**Water under the clouds is heard faintly**: a waterfall in the fog is a reason to go and look. A
waterfall is heard at its full level once it is found; a river wherever the stretch nearest the view is
explored. The water plays on in a pause.

## Seeing them

The debug view's **areas** layer draws each area's edge in its own colour, bright once found and dim
before, with a post at its middle; the panel lists them. `chasm_areas` lists them for agents.

## Planned

- [idea] The chasm itself as an area: its first sight is the music's war horn.
- [idea] A balcony as an area, for the demo's second goal.

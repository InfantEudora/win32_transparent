---
title: Swamp hut
aliases: swamp huts, shack, shacks
kind: world
status: partial
summary: A few solitary, shabby huts on stilts out in the swamp, placed by the seed when a colony starts. Nobody's - where the zombies will come from.
plans: swamp_huts_plan.md > Step 1: the huts (as built, 2026-10-26); swamp_huts_plan.md > Step 2: the first wave (proposed)
code: Zones.h: ZONE_KIND_HUT ZoneKindWild ZONE_SWAMP_HUTS ZONE_HUT_APART ZONE_HUT_CAMP_LEAST ZONE_HUT_EDGE_LEAST ZONE_HUT_DEEP ZONE_HUT_WATER_BEST; Zones.cpp: ZoneSwampHutSpots ZoneSwampHutPlots ZoneHutDoor HutIsBuildable; BuildingMesh.cpp: BuildSwampHut
verified: 2026-10-26
---

A new colony starts with up to {ZONE_SWAMP_HUTS} huts out in the swamp, standing from its first day. They are under
the clouds until someone explores there. Nobody lives in one that the colony knows.

## Where they stand

Each hut stands on one swamp plot whose own point is dry. It is on stilts, so its corners may be in the pools. It
needs a clearing with no tree beside it and a dry plot before its door. The best spots are deep in the swamp, at a
pool's edge, with about {= ZONE_HUT_WATER_BEST * 100}% of the ground round them wet. The huts stand at least
{ZONE_HUT_APART} apart, at least {ZONE_HUT_CAMP_LEAST} from the start camp, and away from the map's edge. Huts whose
door has a walk to the camp, with no bridge, are taken first. On a map where a river cuts the swamp off, the huts still
stand, but nothing could walk from them to the [[Camp|camp]] until the river is bridged.

## Nobody's

A swamp hut is not the colony's.

- No tool builds one, and erasing passes over it.
- No garden or lot goes under it.
- It employs, houses and holds nothing, and it never joins or grows a [[Settlement|settlement]]'s land, however near
  one comes.
- [[Person|People]] and [[Zombie|zombies]] walk round it as round a wall.
- The plot before its door is where anything that comes out of it steps down.

## On screen

One room on stilts with a porch and a plank down from it, all in the swamp's murky colours.

- Planks of three woods, some gone so the dark inside shows through, and a window boarded across.
- The door hangs open on the dark.
- Old reed thatch that sags along the ridge and hangs ragged at the eaves, with a mended strip on some huts, patches
  and moss.
- The rafters cross over each gable end.
- On some: a cold stovepipe, a pole with a rag, a fallen fence.
- Each leans a little, sunk on one side. No two huts are quite alike.

## Planned

- [proposed] **The first wave**: early in year 2's spring, as the swamp thaws, a few zombies come out of each hut one at
  a time and creep on the village unseen under the clouds. A notice tells when they leave and when the first is seen.
- [proposed] Zombies wading rivers slowly, so a river buys time but not safety; huts placed in older saves on load.
- [proposed] Burning a hut down; finding one as a discovery; the swamp's haze before a wave.

---
title: Terrace
aliases: terraces, retaining wall, retaining walls, stair, stairs, stepped street
kind: buildings
status: built
summary: A flat stone platform levelled on a hillside, to build, farm and walk on as on flat ground, with stairs where a road climbs its wall.
plans: terraces_plan.md > Stage 2: terraces as ground
code: Zones.h: ZoneTerraceStands ZoneGroundAt ZoneTerraceStep ZoneCanTerrace; Zones.cpp: ZONE_TERRACE_MOST_FILL ZONE_TERRACE_STEP; Economy.h: ECONOMY_TERRACE_STONE EconomyTerraceStone; TerraceMesh.cpp: BuildTerraceCell; RoadMesh.cpp: RoadStairs
verified: 2026-10-22
---

A terrace levels a hillside: one flat platform over the large squares it is dragged across, with a stone retaining
wall round it. Its tool comes with the [[Stonecutter|stonecutter's hut]], when a boulder is found.

- **Its height** is the highest ground under it, rounded **up** to a whole storey: it is only built up, never dug
  in. A square lower than that by more than {ZONE_TERRACE_MOST_FILL} is refused, so a terrace dragged straight down a
  hill stops at its foot - long terraces follow the hillside. A drag that starts on a terrace extends it at its
  height, so terraces always differ by whole storeys.
- **Painted first**: only on open ground - one ground all round, dry, off the mountain, nothing built or laid there.
- **On it**, its top is the ground: houses, stores, huts, towers, roads, walls, [[Yard|gardens and lots]] and
  [[Field|fields]] go on it as on flat ground, with no foundation. Not a water or steam collector, winch, mine,
  bridge or camp, which each have a place of their own.
- **Built** as a [[Construction|site]]: staked out, its trees felled first, then
  {ECONOMY_TERRACE_STONE} [[Stone|stone]] a unit of area for each unit it fills - shown on the price tag. Nothing goes
  on it until it stands.
- **Walking**: where a terrace stands more than {ZONE_TERRACE_STEP} over the ground across, its edge is a retaining
  wall no one walks; on its uphill side, where the hill meets it at about its height, people walk on. A
  [[Roads and walls|road]] that runs across a wall of up to a storey becomes a **stair** - a stepped street, walked
  slowly. [[Zombie|Zombies]] never step onto a terrace.
- It is erased only with nothing on it.

## Planned

- [planned] A building's upper storey opening onto the terrace above (terraces_plan.md stage 3).

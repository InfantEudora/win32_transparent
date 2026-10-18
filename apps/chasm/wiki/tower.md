---
title: Guard tower
aliases: tower, towers, guard towers, guard, guards, ladder
kind: buildings
status: partial
summary: A two-storey tower of logs in its junction's shape, a guard for each edge past two, built with water and provisions that become its stock - its guards' pantry, entered only by its ladder.
plans: defence_plan.md > The guard tower; defence_plan.md > As built: step 6, the guard tower; defence_plan.md > As built: the tower tuned; defence_plan.md > As built: step 7, the towers kept; defence_plan.md > Guards by hand (step 8); defence_plan.md > Build order; ramparts_plan.md > Step 1: the tower's deck, a real node; zombies_plan.md > Step 3: guards shoot; zombies_plan.md > Step 3b: guards hold their posts
code: Zones.cpp: ZoneCanBuild ZoneTowerGuards ZoneTowerCorners ZoneTowerLadderOk ZoneTowerDefaultLadder ZoneIsLadderPlot; Walkers.cpp: EdgeOpen EnsureDecks WalkDeckLayers; Shooting.cpp: ZombiesInRange DrinkBetweenShots; Economy.cpp: AssignJobs Needs SeekSupplyOf PackUp TowerStoreyCost StoreyKept EconomyPostPoint EconomyOnTower KeepTowers TowerLeft EconomyTowerShort PostGuard SetGuards; Economy.h: ECONOMY_TOWER_WOOD ECONOMY_TOWER_WATER ECONOMY_TOWER_PROVISIONS ECONOMY_TOWER_GUARDS_PLACED WORKER_JOB_GUARD
verified: 2026-10-18
---

Tool **T**, a click each - it is never dragged. One plot, always both storeys: it is painted whole, and taking a storey off takes it all. It
stands by a building's rules, and also **in a wall's line**: painted on a [[Roads and walls|wall]] plot,
planned or standing, it takes that plot over, and the palisade runs up to its sides. Never on a road.
Once it stands it holds land: its [[Settlement|settlement]] grows {ECONOMY_SETTLEMENT_TOWER} units of walking round it.

## Its junction

It takes its **junction's shape**: a triangle, a square, a pentagon or a hexagon, its size its plot's. The
junction decides its **guards**, its edges less two: 1 on a 3-junction, 2 on a 4, 3 on a 5, 4 on a 6. Nearly
every plot is a 4; the ghost's shape shows the rarer ones.

## Building it

So much a guard place: {ECONOMY_TOWER_WOOD} wood, {ECONOMY_TOWER_WATER} water and
{ECONOMY_TOWER_PROVISIONS} [[Provisions|provisions]], half with each storey - so no shape is cheaper for what
it holds. What a storey takes of water and provisions it **keeps** as its stock. Provisions come only from
builders, so no builders' hut, no tower. Placed in debug, it stands full.

## Its ladder is its door

Walkers get on and off a tower only by the plot its ladder stands toward: placed, the open side nearest the
camp - a wall's inside, usually; with none open, refused. **Move ladder** on its card turns it to the next open
side. Nothing is built, and no wall drawn, on the ladder's plot. Once it stands whole, its platform is a place
of its own in the walk - a deck over its plot, up the ladder - so a walk to the tower ends on top of it.

## Its guards

- **How many it takes** is the player's: chips on its card, 0 up to its places, {ECONOMY_TOWER_GUARDS_PLACED} when
  placed. Lowering it sends the extra guards home; at 0 it is unmanned, still kept full, and nobody is missed at it.
- Filled up to that by **archery** first, what the range trained, then [[Skills|precision]]. **Once up, a guard
  keeps his tower** until released or the count is lowered: a shortage of people empties other workplaces first.
- **By hand**: a right click on it, with someone selected, posts him there over the game's pick, leaving his job or
  order - one more with a place to spare, else an automatic guard's place. Refused for a child, the houseless, no way
  there, a site, or every place hand-picked.
- **Released** - his card's chip, or his on the tower's card - he goes home jobless, and the tower takes one fewer.
- A guard lives as someone [[Posts|posted]] does, at a place of his own on the platform. He climbs with
  {NEEDS_PACK_WATER} water in his pack, by the nearest place with some first if he has less - never a tower.
- **The tower is his pantry**: he eats and drinks from its stock as at home, his pack only for a kind it has none
  of. Both empty, a pressing need sends him to the nearest supply and back up - **not while a zombie is in range**.
  A meal waits while he shoots, but a drink he takes between shots, when a whole unit fits.
- The tower pulled down, its guards lose the job and go home. **They shoot** [[Zombie|zombies]] they see from the
  platform, where none can reach them - further than from the ground.

Only posted people draw on it: villagers pass towers by, and no carrier, family or builder takes from one.

## Kept full

The [[Construction|builders]] keep it **full**, the first of their errands: below what it was built with, it is
to them a site wanting water and provisions, fetched from the nearest [[Store|store]], [[Camp|camp]], empty
house, [[Crate|crate]] or builders' hut, never past full - provisions nobody has made for it first. The card
says whether it's full, being filled, or short with **no builders**, as NEEDS ATTENTION does. Never the jobless.

## Planned

- [planned] A **gatehouse**: two towers either side of a palisade gate joined by a walkway between their decks,
  with guard places of its own, reached by the towers' ladders. Towers are never arched over a road
  ([[Roads and walls|arches]]); this is what a tower pair over a gate becomes instead.

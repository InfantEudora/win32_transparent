---
title: House
aliases: home, homes
kind: buildings
status: built
summary: Home to one family, sized by its floor area. It keeps its own food, water and firewood, and the family fetches them.
plans: buildings_plan.md > Decided; people_plan.md > As built; needs_plan.md > As built: step 3
code: Economy.cpp: Derive HouseFamilies HouseRoom PrivateStock; Zones.h: ZONE_HOUSE_MAX_PLOTS ZoneKindMaxStoreys; Economy.h: EconomyShutIn
verified: 2026-10-09
---

The player paints a house over at most {ZONE_HOUSE_MAX_PLOTS:#plots} and gives it storeys
(`ZoneKindMaxStoreys`). It starts as a [[Construction|building site]] and is built a storey at a time,
and a storey does nothing until it stands.

## Who lives there

**One family.** A house holds as many people as its floor area allows, at {ECONOMY_PERSON_AREA} a person,
but never more than {ECONOMY_HOUSE_MOST}.

A family without a house (at the [[Camp]], or homeless) moves into the **smallest empty house that holds
all of them** and that they can walk to; the largest family goes first. If no empty house fits, a smaller
family living in one that would moves to another empty house to make room. A family whose house is pulled
down goes back to the camp.

Every round starts and ends at the house, so where it stands decides who gets which job
([[Person|jobs]]) and how quick their rounds are.

## What it keeps

- **Food and water**, with room for its floor area x {ECONOMY_HOUSE_ROOM} units. The family eats and
  drinks from it at home ([[Health]], [[Thirst]]).
- **Firewood**, kept separately in a woodshed holding {ECONOMY_WOODSHED_DAYS:#days} of what the house
  burns ([[Cold]]).

No carrier ever delivers to a house. Its family [[Fetching home|fetches]] for it, and its farmer brings
part of the harvest in from the house's own [[Field|land]].

**What a house holds is its family's alone while anyone lives there.** Nobody else fetches, eats or builds
from it. Once nobody lives there, its stock is anyone's, like a [[Store|store's]].

## Doors and shut-in houses

A house needs a way in: one of its plots has to be reachable from open ground. A house closed in on every
side by other buildings, walls or water is **shut in**. No family moves into it, and a family already
living there can't get out until the player clears a plot beside it. Its card warns about it.

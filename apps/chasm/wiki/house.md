---
title: House
aliases: home, homes
kind: buildings
status: built
summary: Home to one family, sized by its floor area. It keeps its own food, water and firewood, and the family fetches them.
plans: buildings_plan.md > Decided; people_plan.md > As built; needs_plan.md > As built: step 3; yards_plan.md > Extend; yards_plan.md > Noted; selection_plan.md > Selecting someone indoors; building_size_plan.md > Decided (the user, 2026-10-18)
code: Economy.cpp: Derive HouseFamilies HouseRoom PrivateStock EconomyCrowdedPace EconomyHouseAppeal; Zones.h: ZONE_HOUSE_MAX_PLOTS ZoneKindMaxStoreys ZoneCanExtend; Economy.h: EconomyShutIn
verified: 2026-10-18
---

The player drags out houses - a new one every {ZONE_HOUSE_MAX_PLOTS:#plots}, as the preview shows ([[Construction|placing]]) - and gives them storeys
(`ZoneKindMaxStoreys`): a click on a house is a storey more, a drag never is. It starts as a
[[Construction|building site]] and is built a storey at a time, and a storey does nothing until it stands.

**To make it bigger**, select it and press **Extend** on its card, which takes up the house tool. Every
plot painted then joins that house, and one that doesn't touch it is refused. A drag started on the house
does the same with nothing selected.

## Who lives there

**One family.** A house holds as many people as its floor area allows, at {ECONOMY_PERSON_AREA} a person,
but never more than {ECONOMY_HOUSE_MOST}. **Crowded** - more living there than that, after a birth - its
people work {= LIFE_CROWDED_PACE * 100}% slower for each one over, at worst at {= LIFE_CROWDED_LEAST * 100}%, and a child is less likely.
Storeys and a garden make one likelier ([[Lifelines]]); upper storeys burn half ([[Cold]]).

A family without a house (at the [[Camp]], or homeless) moves into the **smallest empty house that holds
all of them** and that they can walk to; the largest family goes first. If no empty house fits, a smaller
family living in one that would moves to another empty house to make room. A family whose house is pulled
down goes back to a camp with room for them.

**Crammed.** A family left with no home at all, and no camp with room for them, squeezes into a house or
camp of the [[Settlement|settlement]] they are standing in, even past its room: the one with the most room
to spare. Families crammed in together move out, the largest first, as soon as an empty house holds them.
With no home left in the settlement, they wait where the settlers first camped.

Every round starts and ends at the house, so where it stands decides who gets which job
([[Person|jobs]]) and how quick their rounds are. Its card lists the family, dimmed while indoors. **A
click on a name selects that [[Person|person]]**, at home or out, so they can be ordered without waiting
for them to come outside; hovering a name outlines them in light blue if they are out. Someone selected
at home shows on the map by their house's outline; Shift+click adds them to the people already selected.
The [[Camp|camp's]] card lists its settlers the same way.

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

## Planned

- [planned] Firewood on its [[Yard|lot]]: a house with a lot of its own stores wood there as well as in
  its woodshed's {ECONOMY_WOODSHED_DAYS:#days}, so it holds more for the winter ([[Cold]]). Its lot plots
  will all touch the house, which is their door (on the yard's page).

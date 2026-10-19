---
title: Posts
aliases: post, order, orders, pack, posted
kind: people
status: partial
summary: The player can order a grown-up to stand on a plot. It becomes their job while it lasts, and they live off a pack they take along.
plans: defence_plan.md > As built: step 1, orders; defence_plan.md > As built: step 2, the pack; defence_plan.md > As built: step 5, provisions; defence_plan.md > As built: step 6, the guard tower; defence_plan.md > Build order; defence_plan.md > Scouting, provisioned first; selection_plan.md > Several people selected (the user's, agreed 2026-10-11)
code: Economy.cpp: Order TakePost GoToPost SpreadPost RePlace FillPack PackUp Unpack Needs Gather GatherSupplies Notice; Shooting.cpp: ZombiesInRange; Economy.h: EconomyCanOrder EconomyPostRoom ECONOMY_POST_ROOM NEEDS_PACK_WATER NEEDS_SCOUT_WATER NEEDS_SCOUT_FOOD ECONOMY_GATHER_STOPS EconomyEvent
verified: 2026-10-19
---

## Ordering

A [[Guard tower|guard]] is posted by his job instead: his post is his tower, and he lives on it as below,
with the tower as his pantry. An order to him comes first and suspends the job like any other.

With a person selected, a right click on a plot sends them there; their card releases them. An order is refused for a
child - children go with their household ([[Founding]]) - onto a plot with a building, a wall or a shut gate on it (a camp and a standing bridge count as open ground), onto
the water, and onto a plot they can't walk to. In winter they may cross the **ice** ([[Seasons]]): a risk, as the thaw
cuts off whoever is still across. A right click on a [[Guard tower|guard tower]] instead makes them one of its guards, by
hand; on a ripe plant or on water it offers to **pick fruit** or **draw water** there by hand ([[Fruit]]) - not under the clouds, where any refusal is only "no way there" ([[Exploration]]). **Several at once**: drag a box round grown-ups, or Shift+click them on the map or on a card, and one right click
orders them all - the nearest onto the plot clicked while it has room, the rest onto the nearest plots round it they can
reach (one each {ECONOMY_POST_ROOM} of a plot's area). Several on one plot stand apart, each in a place of their own; onto a tower, the nearest take its places.

## On the post

- They drop what they were doing at once. A load they carried is put down as a [[Crate|crate]]; a storey
  they were building stands with what it has.
- They stand on the post: no going home, no errands, no building.
- A job they had is **kept** but stands idle, so a [[Water collector|collector]] doesn't fill and their
  share of a [[Field|field]] doesn't grow. On release they go back to it. Someone jobless gets no job until
  released.
- Their needs work as anyone's: a pressing one sends them to the nearest supply of their settlement or unclaimed
  land, their house counting as one, then back to the post; any food, [[Provisions|provisions]] too. If cut off,
  they wait and try again. **Not with a [[Zombie|zombie]] in range** - one they could shoot or are fighting.

## The pack

They take water ({NEEDS_PACK_WATER} units) and food (worth {NEEDS_PACK_FOOD:days} of
[[Health|health]]) with them, filled from home when they set out and topped up at every supply trip -
provisions first where there are any, since one is the whole pack's food. They eat from it as at home, so a
pressing need only comes once it's empty. Released, they live on it on the way home too - a scout called back
from far out walks home on what he carried. Home, what is left goes into the pantry, but provisions, which stay
packed: no home holds them. Their card shows the pack as a goods strip, with what it is worth in days.

**When it runs out** - the moment it goes from holding water and food to lacking one of them - the player is told,
in amber at the bottom of the screen: "JELLE DE WIT HAS RUN OUT OF WATER" (of food, or of food and water). Only for
someone ordered out, not a [[Guard tower|tower's]] guard. Nothing more happens: they go for it when the need presses.

## Scouting: gathering supplies first

A posted person's card has a **Grab Supplies** chip beside Release - a group's card too. Pressed, they first fill their pack as
full as they can carry - {NEEDS_SCOUT_WATER} water and food worth {NEEDS_SCOUT_FOOD:days} of health, about two weeks
out - and then go on to the post. Whether just ordered, on the way or standing there.

- **From wherever a pressing need would send them**: their home, a [[Store|store]], the [[Camp|camp's]] supplies, a
  [[Water collector|collector]], a [[Field|field's]] harvest or a [[Crate|crate]] - the nearest first, and
  [[Provisions|provisions]] first where there are any. Someone setting out from home fills it there.
- **A second stop** ({ECONOMY_GATHER_STOPS} places in all) for what the first lacked - water at the camp, say, then
  provisions at a store. With something but not everything to be had, they take what there is.
- **With nothing to be had**, they go on to the post as they were, and the player is told: "NO FOOD OR WATER FOR
  KAAT DE WIT TO TAKE" - only when they took nothing at all, not when a second stop finds nothing.
- The chip is lit while they are at it, and their card says "gathering supplies". Pressed again, they go straight
  on - turning for the post at once, unless a pressing need takes them to the supply anyway.

Provisions are in the stores only if the colony keeps some ([[Provisions|provisions]]). A mission is just a post,
usually an [[Archery range|archer's]]. **Pack a Tent**, beside the chip, fetches one to pitch ([[Founding|founding]]).

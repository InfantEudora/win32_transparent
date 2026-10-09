---
title: Fetching home
aliases: fetch, fetching, larder
kind: systems
status: built
summary: A house running low on water, food or firewood sends one of its family for a load from a store, the camp, or its own fields.
plans: needs_plan.md > As built: step 2; needs_plan.md > As built: step 3; land_plan.md > As built
code: Economy.cpp: HomeShort FetchHome TakeHome FirewoodNow FirewoodFair FairShare
verified: 2026-10-09
---

Nobody delivers to a [[House]]. Each family keeps its own house stocked, and that is what saves people
from going out for every meal and drink ([[Health]], [[Thirst]]).

## When a house is short

- **Water or food**: it holds less than {ECONOMY_HOME_DAYS:#days} of what the family drinks or eats.
- **Firewood**: it holds less than {ECONOMY_HOME_DAYS:#days} of its burning, or less than its fair share
  (below) if that is smaller. Firewood is only fetched in its season: from autumn's first day, through
  winter, and in spring while snow still lies on the house. Only houses that the full winter's snow
  reaches fetch firewood at all ([[Cold]]).

The scarcest kind, counted in days, is fetched first. Water wins a tie.

## Who goes

**One errand per house at a time.** A jobless member of the family goes first. Someone with a job goes
only between rounds, and only when nobody jobless is home. Fetching comes before carrying wood to
building sites and before the next round of work.

People living in the [[Camp]] never fetch: the camp's supplies are what the settlers brought.

## Where from, and how much

- The nearest [[Store]] or [[Camp]] with some to spare, after counting the loads that others are already
  on their way to take.
- For food, also the house's own [[Field|fields]], while the house isn't ahead of the colony (below).
- A load is at most {ECONOMY_LOAD}, and never more than brings the house to a day past
  {ECONOMY_HOME_DAYS:#days}, so water doesn't crowd out the room that food needs.

Someone who went to a store to eat or drink on a pressing need brings a load home as well.

## Fair shares

A family may take food from its own fields only while it is less than {ECONOMY_FAIR_DAYS:#days} a head
ahead of the colony's average at home. A house that is further ahead fetches from storage like everyone
else. Firewood is shared the same way: a house fetches no more than its burning's part of all the firewood
at homes, plus {ECONOMY_FAIR_DAYS:#days} of burning. While wood is short, the houses fill up together, a
day at a time.

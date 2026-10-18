---
title: Fetching home
aliases: fetch, fetching, pantry
kind: systems
status: built
summary: A home running low on water, food or firewood sends one of its family for a load from a store, the camp's supplies, or its own fields.
plans: needs_plan.md > As built: step 2; needs_plan.md > As built: step 3; land_plan.md > As built; lifeline_plan.md > Baby: the mother's year; lifeline_plan.md > As built: shares
code: Economy.cpp: HomeShort FetchHome FetchFrom TakeHome FirewoodNow FirewoodFair FairShare Mouths EconomyShare; Economy.h: LIFE_SHARE_BABY LIFE_SHARE_CHILD WORKER_JOB_CARING
verified: 2026-10-18
---

Nobody delivers to a [[House]]. Each family keeps its own house stocked, and that is what saves people
from going out for every meal and drink ([[Health]], [[Thirst]]). It is all that feeds a baby, which never
goes out for anything itself.

## When a house is short

- **Water or food**: it holds less than {ECONOMY_HOME_DAYS:#days} of what the family drinks or eats.
- **Firewood**: it holds less than {ECONOMY_HOME_DAYS:#days} of its burning, or less than its fair share
  (below) if that is smaller. Firewood is only fetched in its season: from autumn's first day, through
  winter, and in spring while snow still lies on the house. Only houses that the full winter's snow
  reaches fetch firewood at all ([[Cold]]).

What a family eats and drinks is counted by its mouths, not its heads: a [[Lifelines|baby]] takes
{LIFE_SHARE_BABY} quarters of an adult's, a child {LIFE_SHARE_CHILD}. The scarcest kind, counted in days, is
fetched first. Water wins a tie.

## Who goes, and when

Someone at home looks for this once they have eaten their fill, before any work: before carrying to a
[[Construction|building site]] and before the next round of a job. **One errand per house at a time.**

1. A member with no job goes first, and so does a mother caring for her baby in its first year, who is
   home anyway. While one of them is home, nobody with a job goes.
2. Otherwise someone with a job, between rounds: the first of them home while the house is short.
3. Never a child, a baby, a [[Guard tower|guard]] or anyone on a [[Posts|post]].

So in a house of two working adults one of them fetches between rounds. With a baby, the mother runs every
errand for its first year and its father none.

Families living in the [[Camp]] fetch the same way, into the camp's pantry. They count its supplies as
part of what they hold, but never fetch from them.

## Where from, and how much

- The nearest [[Store]], [[Camp|camp's supplies]], empty [[House]] or [[Crate|crate]] with some to spare,
  after counting the loads that others are already on their way to take - for water a
  [[Water collector|collector]] too, for firewood a [[Woodcutter|woodpile]]. Never a house somebody lives in.
- For food, also the house's own [[Field|fields]], while the house isn't ahead of the colony (below).
- A load is one kind, at most {ECONOMY_LOAD}, and never more than brings the house to a day past
  {ECONOMY_HOME_DAYS:#days}, so water doesn't crowd out the room that food needs.

Food also comes home with the family's own **farmer**, who carries its land's harvest home first while the
house is short and within its fair share; with someone who went to a store to eat or drink on a pressing
need; and in what is left of a pack when someone comes back from a post.

## Fair shares

A family may take food from its own fields only while it is less than {ECONOMY_FAIR_DAYS:#days} a head
ahead of the colony's average at home. A house that is further ahead fetches from storage like everyone
else. Firewood is shared the same way: a house fetches no more than its burning's part of all the firewood
at homes, plus {ECONOMY_FAIR_DAYS:#days} of burning. While wood is short, the houses fill up together, a
day at a time.

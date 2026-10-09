---
title: Woodcutter
aliases: woodcutter's hut, woodcutters' huts, hut, woodpile
kind: people
status: built
summary: Fetches logs and fells trees around the hut, and carries the wood to a store, or onto the hut's woodpile.
plans: economy_plan.md > The woodcutter: logs first; economy_plan.md > As built: house storage and the woodpile
code: Economy.cpp: TickWorker FindTree WoodNear FetchFromWorkplace FindStore; Economy.h: WORKER_JOB_WOODCUTTER
tuning: ECONOMY_PICK_SECONDS
verified: 2026-10-09
---

A job at a **woodcutter's hut**, which the player paints. The hut needs a fenced lot beside it for its
**woodpile**; with no lot standing there, the woodcutter fells nothing. The job goes to whoever scores
best on strength against the walk from home (see [[Person|jobs]]).

## A round

Every round starts and ends at the woodcutter's [[House|house]], so a house close to the hut and its
store makes a quick woodcutter.

1. **The pile first.** If the woodpile holds wood and a [[Store]] will take it, the woodcutter fetches a
   tree's worth ({ECONOMY_WOOD_PER_TREE} wood) from the pile and delivers that.
2. **Otherwise the nearest log lying, then the nearest standing [[Tree]]**, within
   {ECONOMY_WOODCUTTER_REACH} world units of the hut. Logs are left lying by people clearing
   [[Road|roads]] and [[Wall|walls]]. Felling takes {ECONOMY_FELL_SECONDS:#seconds} at an average pace,
   and less with more strength ([[Skills]]).
3. **A second log**, if one lies within {ECONOMY_LOG_PAIR_REACH} of the first and there is room for both
   where they are going: at most {ECONOMY_WOODCUTTER_LOGS:#logs} a trip, {ECONOMY_WOOD_PER_TREE} wood each.
4. **Deliver** to the nearest store that takes wood and has room for the whole load, or else onto the
   woodpile, which holds {ECONOMY_HUT_PILE}. If neither has room, the woodcutter waits, holding the wood,
   until one does.
5. **Home**, then the next round.

A woodcutter only goes for a tree while the pile has room for it. So with no store taking wood, the
woodcutter works until the pile is full, then waits at home.

## Before the round

At home, before any of this, the woodcutter sees to a pressing [[Thirst|thirst]] or [[Health|hunger]]
that the house can't meet. They may also be the one sent to [[Fetching home|fetch]] for the house, but
only if no jobless family member is home to go instead.

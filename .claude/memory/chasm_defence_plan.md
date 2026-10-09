---
name: chasm-defence-plan
description: "AGREED 2026-10-09 apps/chasm/docs/defence_plan.md - posts/orders, pack, provisions, guard tower, gate doors; 8-step build order, steps 1-4 (orders, pack, gate doors, site costs per good) BUILT 2026-10-09"
metadata:
  node_type: memory
  type: project
  originSessionId: 81514510-853a-4457-94ca-3d840e8ed249
  modified: 2026-10-09T15:43:10.878Z
---

Chasm's defence groundwork was talked through and agreed with the user on 2026-10-09, written up as
`apps/chasm/docs/defence_plan.md`. Steps 1-4 BUILT and tested 2026-10-09 (see the plan's "As built"); step 5,
provisions, is next. An ordered load is dropped as a crate (crates plan, another window). Build order: 1 orders
(right-click a selected person to post him; a post is his job; ordered people are out of `AssignJobs`), 2 the
pack, 3 gate doors (shut for everyone), 4 site costs per good (`StoreyGoods`/`EconomyCellCost` in Economy.cpp
are where a kind declares a non-wood cost - all 0 so far; window 38's archery range will add wheat per cell),
5 provisions (new good, own bit OUTSIDE `GOODS_FOOD`; goods masks go to 16 bits), 6 guard tower, 7 caretakers
resupply towers, 8 right-click on a tower.

Window 38 built the archery range (range_plan.md): ZONE_KIND_RANGE 10, WORKER_JOB_ARCHER 4, WORKER_TO_RANGE 25 /
WORKER_TRAINING 26, ZONE_OP_RANGE_PAINT after GATE_SHUT, ZoneKindOnCells(); EconomyWorker::archery exists - so
step 6 may staff guards by archery then precision (plan says precision; raise with the user then).

Open from step 4: sites take food/water from the same stocks people live on, nothing kept back (a test hut
drained the camp's water) - raised with the user, not decided.

**Why:** the user wants defence (P6, zombies) built on people who can be sent somewhere and stay, without
Knights-and-Merchants-style feeding chores - supply is automatic, placement is the player's part.

**How to apply:** read the plan before touching orders, the tower, provisions or gates in chasm. Numbers
in it are tuning ([[tuning-numbers-not-rules]]); balance stocks at 3 water : 2 provisions (water runs out
first and kills). Comparing builds: an old chasm build's FIRST map after startup can differ; compare on a
regenerated map. Related: [[chasm-game-plan]], [[line-works-plan]].

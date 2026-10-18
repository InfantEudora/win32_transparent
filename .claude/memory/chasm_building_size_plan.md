---
name: chasm-building-size-plan
description: "chasm width vs height, decided + BUILT 2026-10-18: footings, stone for storeys 3+, workers by plots, heat rises, appeal + crowding in births and work; apps/chasm/docs/building_size_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 26190601-4a53-4a7c-a11d-31341147a809
  modified: 2026-10-18T13:00:33.073Z
---

The user, 2026-10-18: bigger/taller buildings were under-used because width, height and plot size cost and gave the
same (everything by floor area). Wanted: another plot dearer than a bigger plot, another storey dearer again, a benefit
for each, leaning players to layouts. From my suggestion list the user CHOSE A (footing wood on each plot's ground
storey, body kinds only), B as STONE (storeys 3+), D (woodcutter/collector/stonecutter take a worker per 2 plots) and
F (house fire: ground storey 1, upper storeys half, odd half on alternate dawns); and ADDED appeal (storeys +5 each over
the first, gardens 8 then halving) and crowding (lower birth chance, people work 10% slower per person over, min 50%).
NOT taken: rising wood per storey, shared walls, builders by area / ropewalk by length, two families in a tall house,
height seeing farther.

All built and checked the same day (plan's "As built"). Key code: EconomyKindStoreyCost(..., level) and
EconomyPlotLeftCost (a storey's cost now depends on its level - never multiply one storey's cost by a count again);
Economy::Crowding/CrowdPace (member crowded_by, cleared in Reset - replay checked); EconomyBirthChance shared by the
roll and the house card. Numbers are tuning ([[tuning-numbers-not-rules]]).

**Why:** the user wants building shape to be a real choice. **How to apply:** a new storey-cost caller uses the level
API; a new timed work task multiplies by CrowdPace; a new workplace kind decides whether it takes workers by plots.
Related: [[chasm-game-plan]], [[chasm-goods-strip]].

---
name: chasm-defence-plan
description: "AGREED 2026-10-09 apps/chasm/docs/defence_plan.md - posts/orders, pack, provisions, guard tower, gate doors; 8-step build order, steps 1-6 BUILT (tower 2026-10-10); step 7 caretakers' resupply next"
metadata:
  node_type: memory
  type: project
  originSessionId: 81514510-853a-4457-94ca-3d840e8ed249
  modified: 2026-10-10T11:40:41.268Z
---

Chasm's defence groundwork was talked through and agreed with the user on 2026-10-09, written up as
`apps/chasm/docs/defence_plan.md`. Steps 1-5 BUILT 2026-10-09, step 6 (guard tower) BUILT and tested 2026-10-10
(see the plan's "As built"). Build order: 1 orders, 2 pack, 3 gate doors, 4 site costs per good, 5 provisions,
6 guard tower, 7 caretakers resupply towers (ahead of building), 8 right-click on a tower. Each step only on
the user's go-ahead.

Step 6 as built: ZONE_KIND_TOWER 11 (tool T), WORKER_JOB_GUARD 5, painted whole (ZoneKindPaintStoreys), may take
a wall plot. A guard is "posted" by his job: EconomyPosted / EconomyPostPlot / EconomyPostPoint / EconomyOnTower
(EconomyState::door); the tower is his pantry in Needs (pack only for a kind the tower lacks). Water and
provisions kept as stock (StoreyKept); debug placement stands full. Staffed archery first (range_plan.md had
already agreed that with the user). Roof: none for now.
TUNED 2026-10-10 (the user's): shape = its junction's polygon (ZoneTowerCorners), guards = junction - 2 (1-4, a
6-junction 4), cost per guard place (ECONOMY_TOWER_WOOD/WATER/PROVISIONS, TowerStoreyCost), LADDER IS ITS DOOR
(ZoneBuilding::ladder; Walkers::EdgeOpen + EconomyStraightClear), default open side nearest the camp, moved by the
card chip / ZONE_OP_TOWER_LADDER, its plot kept clear of buildings and walls. 3-junction plots are often small, so
one-guard towers are slim (ZONE_TOWER_INSET is the knob) - told the user.

ARCHES (window 24's arches_plan.md, agreed 2026-10-10): towers are never arched (for now), nothing is arched over a
gate; arch storeys kept apart from z.storeys and arch plots out of plots_of (my tower code reads plots_of.front(),
and Restore re-checks a ladder with z.storeys[to] == 0). The GATEHOUSE is WANTED (defence_plan Open): a walkway
between two towers' decks over a palisade gate, own guard places, reached by the ladders, paid per guard place -
not in the build order yet.

For step 7: a tower below full is to builders "a site that wants water and provisions"; it must also join
ProvisionsLacking, and window 28's SiteReservation / HomeReservation helpers (GoodFree, card stripes) need the
new reservation kind. Towers take no deliveries yet (accepts 0, keeps water+provisions, room = full).

Open from step 4: sites take food/water from the same stocks people live on, nothing kept back - raised with
the user, not decided.

Testing traps: a test colony with no fields runs out of wheat and beans in about 8 days (give the camp food);
chasm_paint's ground kind is `ground`, not `kind`; its `ran` is true even when refused - read `refusal`. Run a
long test from a copy of the exe so other windows can relink. Compare builds on a regenerated map (an old
build's FIRST map after startup can differ).

**Why:** the user wants defence (P6, zombies) built on people who can be sent somewhere and stay, without
Knights-and-Merchants-style feeding chores - supply is automatic, placement is the player's part.

**How to apply:** read the plan before touching orders, the tower, provisions or gates in chasm. Numbers
in it are tuning ([[tuning-numbers-not-rules]]); balance stocks at 3 water : 2 provisions. Related:
[[chasm-game-plan]], [[line-works-plan]], [[chasm-range]].

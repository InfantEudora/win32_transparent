---
name: chasm-settlements
description: "chasm settlements plan (docs/settlements_plan.md) - territories by walking 50 units from a camp, people work only in their own; step 1 BUILT 2026-10-11; founding by expedition and porters still to think through"
metadata:
  node_type: memory
  type: project
  originSessionId: 5bac1302-f1b1-4435-8b32-339b4416d1b9
  modified: 2026-10-11T10:11:11.995Z
---

Talked through with the user 2026-10-11 after a far tent was built by a villager from the camp (no task limit).
The game heads for several settlements: rim/balcony (floatstone), mountain (stone, coal), far side (desert, pyramid).

Decided: distance over the MAP (walking), not straight lines; borders drawn like The Settlers (posts), Voronoi
where two meet; ~50 units (ECONOMY_SETTLEMENT_REACH); a walk-distance label with a person selected.
LATER, to think through before building (the user's): founding a camp = packing up a tent, an expedition of 2-3
incl. a woodcutter who works without a house, only once the base runs - it will shape the game's start too;
PORTERS a job of their own, maybe with a building managing what goes where and how much.

Step 1 BUILT 2026-10-11: Walkers::MapFlood (terrain-only, ignores buildings), Economy::Settle at the end of Derive
(standing camps in id order found if outside earlier territories), EconomyState::settlement/settlement_of, the
limit via Economy::Ours in every errand + AssignJobs/HouseFamilies/SettleLand per settlement; play placement
refused "outside the settlement" (EconomyCommandInTerritory, ghost, road lines); ApplicationChasmBorders.cpp draws
posts + DrawWalkTip. Needs and posts are NOT limited. A second camp only by debug placement until founding exists.

**Why:** the colony must grow as separate bases supplied deliberately, not one map-wide labour pool.
**How to apply:** any new errand or "nearest X" search for a person must filter by Ours(k,id); kept stock,
provisions and the top bar are still colony-wide (open). Related: [[chasm-defence-plan]] (scout pack for
expeditions), [[chasm-goods-plan]] (kept stock -> porters), [[chasm-game-plan]].

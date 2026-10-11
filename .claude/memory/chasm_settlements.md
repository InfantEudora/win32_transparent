---
name: chasm-settlements
description: "chasm settlements plan (docs/settlements_plan.md) - territories by walking 50 units from a camp, people work only in their own; step 1 BUILT 2026-10-11; founding by expedition and porters still to think through"
metadata:
  node_type: memory
  type: project
  originSessionId: 5bac1302-f1b1-4435-8b32-339b4416d1b9
  modified: 2026-10-11T20:31:15.958Z
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

FOUNDING decided 2026-10-11 (plan "Founding: taking a tent", not built): each expedition member carries their own
tent (load) + pack, packed at the camp via a card toggle like gather, PITCH on the card = their home. A tent is just
a house: ANY home keeps a settlement, it vanishes only with the last; the homeless cram into a house of their own
settlement. Pitching in another settlement = home there, house auto-assigned. Camp's 3 places: woodcutter, farmer,
water (drawn standing beside the wet bank) - the start camp too. Tents cost >= 1 rope. Foraging: just slow, its own
item later. House place AGREED: kept until the tent is pitched; pitching gives it up, they become a household of
their own (as a new couple). Open: a partner left behind. Water place + rope after the woodcutting plan's camp
steps (another agent).
Founding STEP 1 BUILT 2026-10-11: every camp plot a tent (fires = props in open edge cells, ZoneCampFireTent, drawn
by the cell); Settle founds from ANY home (camp/house) in id order, oldest = centre; reach by beds
(EconomySettlementReach: 20 + 5/bed to 50, agreed); HouseFamilies crams the homeless into a home of the settlement
they stand in, crammed families move out to an empty house.
Founding STEP 4 BUILT 2026-10-11 (take a tent + pitch): EconomyWorker::tent (ECONOMY_TENT_*), TakeTent/GoForTent/
TentToPack (packs via TentsPacked -> ZONE_OP_BUILD_ERASE), PitchSpot + Zones::PitchTent + Pitched (FollowZones split
out of Tick so the new camp is derived at once), on-the-road families not housed; cards' chips; LOAD_TENT drawn
(WORKER_LOADS moved to ApplicationChasm.h - the arrays there were hard-coded 4 and crashed). Wiki page founding.md.
Founding steps 2/3/5/6 BUILT 2026-10-11: camp water carrier (drawer_places/draw_plot, WORKER_JOB_WATER at a camp,
ECONOMY_CAMP_WATER_*); tent costs ECONOMY_TENT_ROPE via StoreyGoods; SETTLE HERE (the user's: posted onto another
settlement's ground, inside its borders, no tent -> SettleHere, needs a free bed: CampSpare or empty house - the
agent's default); a gone camp's goods -> crate CRATE_CAMP (Derive keeps was_camp: zones clear a gone building's kind).
Still open: a partner left behind; foraging, porters, per-settlement stock.

**Why:** the colony must grow as separate bases supplied deliberately, not one map-wide labour pool.
**How to apply:** any new errand or "nearest X" search for a person must filter by Ours(k,id); kept stock,
provisions and the top bar are still colony-wide (open). Related: [[chasm-defence-plan]] (scout pack for
expeditions), [[chasm-goods-plan]] (kept stock -> porters), [[chasm-game-plan]].

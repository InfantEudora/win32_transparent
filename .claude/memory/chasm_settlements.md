---
name: chasm-settlements
description: "chasm settlements: part one (territories, founding by tent, one camp to a colony) FINISHED, moved to docs/done/settlements_plan.md 2026-10-21; docs/settlements_plan.md is part two - porters and per-settlement stock, to think through"
metadata:
  node_type: memory
  type: project
  originSessionId: 5bac1302-f1b1-4435-8b32-339b4416d1b9
  modified: 2026-10-23T10:11:33.573Z
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
Then the user's: SETTLE never refused for beds - squeezes into the home with most room (crammed, belongs there);
PRESSING NEEDS only from own settlement or unclaimed land (SeekSupplyOf in_reach), posted and gathering too.
Then (the user's, testing saves/camp_3.json): partners REJOIN - pitching/settling where the partner lives joins
their household + home (PartnerLivingIn); COUPLES also pair into a house one lives in alone (holds 2); a
woodcutter's hut in the settlement TAKES OVER the camp's woodcutter place (after Settle). All built + replay-checked.
LAND GROWS FROM EVERY BUILDING (the user's, built 2026-10-12, plan "Growing from every building"): heart = founding
home by beds; every STANDING building +15 walk (ECONOMY_SETTLEMENT_BUILDING), guard tower +30, built wall/gate plot
+10; chains outward; not sites/fields/ranges/roads/yards; Settle grows in waves, narrows hearts in rounds.
2026-10-12 BUILT (the user's): children GO WITH the household when every grown-up leaves (tent or other settlement's
ground) - internally posted beside mother/father/eldest (GoesWith/WithTheGrown, EconomyWorker::with, saved+hashed);
a BABY carried by its mother whenever she is posted, until home, fed from her pack; ORPHANS squeeze in with a
grown-up's home of their settlement (HouseFamilies) - overrules lifeline_plan's no-orphan rule. Box select = grown-ups.
2026-10-12 also: ONE CAMP TO A COLONY (the user's): tents anywhere in a colony join its camp (EconomyColonyCamp;
zones let a camp's tents not touch, never split it); a camp never joins land with a camp in Settle (founds its own);
camp door in its largest tent group; ECONOMY_PITCH_RINGS 4. COLONY N panel top-left (SelectionColony).
Also open: foraging, porters, per-settlement stock. camp_3's home village dies of thirst (camp carrier only).
2026-10-21 (the user's): part one moved whole to docs/done/settlements_plan.md (FINISHED note on top; code comments
naming settlements_plan.md + a section mean that file - left unedited, as demo_plan's were). Foraging was built as
foraging_plan.md. docs/settlements_plan.md is now "part two: goods between them" - porters, and kept stock /
provisions / top bar / NEEDS ATTENTION per settlement (all colony-wide today: KeptShort, ProvisionsLacking,
EconomyStoredTotals). Wiki plans: lines point at done/.
DECIDED 2026-10-21: per colony FIRST (Statistics shows totals AND per colony); porters work like a zeppelin's
route (the user to think through); a DEPOT building, only porters carry to/from it (reserving); top bar = colony
under the camera, else under the cursor. Step 1 proposed in the plan (4 sub-steps): a LASTING colony number
carried by homes (today's id = oldest home's, changes), kept by colony, top bar + NEEDS ATTENTION, stats rows.
Defaults AGREED 2026-10-22 (top bar keeps last shown over open land; NEEDS ATTENTION follows it, dangers from all;
"nobody's" share in stats). Sub-step 1 BUILT 2026-10-22: EconomyState::colony (by building id) + next_colony, set at
the end of Settle, saved (economy.colonies/next_colony), hashed only once next_colony > 2; EconomyColonyNumber;
SelectionColony + stats NOW use it. Test save saves/colony_numbers_test.json (colonies 2 and 3). Next: step 2, kept
stock by colony. Built on the steam agent's UNCOMMITTED tree (the user's choice to wait for it, not for a commit).
Step 2 BUILT 2026-10-22: kept/ordered are vectors by colony number (EconomyKept/EconomyOrdered/EconomyKeptLevel take
the colony); KeptShort/ProvisionsLacking/EconomyStoredTotals take a SETTLEMENT id (ECONOMY_EVERY_SETTLEMENT = all);
CHASM_CMD_KEEP value[2] = colony (0 = oldest standing); save "kept": {"1": {...}}, old flat form -> colony 1.
Test save saves/kept_by_colony_test.json. Testing an outpost: put people BESIDE the house (its plot refuses), give
it water (a debug well, endless) or they die of thirst in ~4 days; a store holds 22, so supply by crate.
Step 3 BUILT 2026-10-22 (view only): ApplicationChasm::ViewColony (camera point weather_view.at, else GetHoverPick,
else view_colony_number, else oldest; EVERY with one colony), ChasmPersonSettlement (in ApplicationChasmIssues.cpp -
ApplicationChasmSelect.cpp's anonymous namespace runs to ~line 2724, a definition there clashes with the header),
ChasmIssues/ChasmDaysHeld/ChasmInCamp/ChasmBarGoods take a settlement, ViewIssues adds other colonies' dangers.
Testing: the play camera EASES to a chasm_view target (wait ~3 s); pin the cursor with chasm_tool hover_x/hover_z
(chasm_select op hover does not move the ground pick). MOST IN NEED stays all colonies (the user's, 2026-10-22).
Step 4 BUILT 2026-10-22: EconomyDay::colonies (rows by colony number, 0 nobody's; EconomyColonyRow), filled in
EconomyDayLevels + Event (EconomyEvent::colony; EconomyPersonLand = whose a person is, rules side); saved after zombies;
hashed only with a colony > 1; ChasmColonyHistory/ChasmStatsColonies/NowOf(colony); check: colonies add up; page chips
OVERLAY_HIT_STATS_COLONY (chasm_select op press now works on the stats page); chasm_stats `colony`. Test save
saves/stats_by_colony_test.json. ALL of step 1 (per colony) is BUILT. Next: porters/depot - the user thinks routes over.
2026-10-23 (the user's): the JOBS overview (bottom left) follows the colony in view too, titled "JOBS, COLONY N" /
"JOBS"; EconomyJobCounts takes a settlement; chasm_economy `jobs_shown` = the panel's, `jobs` stays all (the Void).
MOST IN NEED is the only bottom-left panel still every colony's (on purpose).

**Why:** the colony must grow as separate bases supplied deliberately, not one map-wide labour pool.
**How to apply:** any new errand or "nearest X" search for a person must filter by Ours(k,id); kept stock,
provisions and the top bar are still colony-wide (open). Related: [[chasm-defence-plan]] (scout pack for
expeditions), [[chasm-goods-plan]] (kept stock -> porters), [[chasm-game-plan]].

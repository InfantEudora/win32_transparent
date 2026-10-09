---
name: crates-plan
description: "chasm crates (apps/chasm/docs/crates_plan.md) - AGREED 2026-10-09; step 1 BUILT same day (crate list, posted person's drop, debug spawn, drawn); steps 2-5 next"
metadata:
  node_type: memory
  type: project
  originSessionId: 47e6cba0-a787-423c-b97b-1ca2ffa6b50e
  modified: 2026-10-09T18:41:34.652Z
---

Chasm crates, agreed with the user 2026-10-09 in `apps/chasm/docs/crates_plan.md`. The user's decisions: a
crate is a STORE VILLAGERS ONLY TAKE FROM, destroyed when empty; moving crates to a storehouse is the
CARETAKERS' (builders') errand, LAST on their list, not the jobless's; building over a crate is refused with a
message; A FEW finds are generated from the seed, and debug can spawn more; holding (WORKER_HOLDING) becomes
dropping for EVERYONE; one crate look for everything. FETCH FIRST and the crate badge: left open (the user).

STEP 1 BUILT 2026-10-09 ("As built: step 1" in the plan): `EconomyCrate` list in EconomyState, saved/hashed;
`Economy::DropLoad` from `Order` (into a store he stands at, else his plot, else nearest open plot within
ECONOMY_CRATE_RINGS stepping across open ground, camp and bridges); `CHASM_CMD_CRATE` debug spawn (tool op
`crate`, panel on the selected plot); `ApplicationChasmCrates.cpp` draws `crate_a` (exported to
chasm_props.glb). Wiki page `apps/chasm/wiki/crate.md` holds the planned bullets.

STEP 2 BUILT 2026-10-09: a worker's `store` names a crate as ECONOMY_STORE_CRATE|id; read stocks via
Economy::SourceStock, never state.stock[k.store]. Crates are sources in FindSiteWork/SeekSupply/FetchHome/TakeHome;
builders move them last (MoveCrate after FindSiteWork+FindRoadWork; states 27 TO_CRATE, 28 LIFTING; c8's defence
step 5 takes 29+ and hooks provisions before FindSiteWork); EconomyCrateInTheWay refuses play paint over a crate
and turns the ghost red.

STEP 3 BUILT 2026-10-09 (the card): PLAY_PICK_CRATE picked like a person (CRATE_PICK_PX of its middle), CrateCard in
MakeCard, outline-only copies of the crate mesh for selected/hovered (CrateYaw shared). FETCH FIRST and the badge
LEFT OPEN by the user. Next: step 4 (finds from the seed under the clouds), step 5 (holding -> dropping).
Testing tip: in play mode chasm_view's target glides, so find a crate's pixel by scanning chasm_select op under.

**Why:** the user's fix for a posted carrier's load, and the small start of They Are Billions-style finds.
**How to apply:** build in the plan's order, asking before each step; after any economy change run a replay
pair (replay_trace). Watch for other windows building chasm mid-edit - a stale object mix cost a rebuild
here (touch the changed headers). Related: [[chasm-defence-plan]], [[chasm-game-plan]], [[chasm-wiki]].

---
name: chasm-statistics
description: "Chasm statistics v1 BUILT 2026-10-16, choices CONFIRMED by the user - EconomyEvent record shared with sound, saved journal + day rows, tail-hashed \"history\" trace part, Tab page, chasm_stats; events listed in wiki/events.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 626f6d08-e1e4-40c4-aa34-4b3f03e32cc2
  modified: 2026-10-16T20:33:37.709Z
---

2026-10-16: statistics_plan.md version 1 built (its "As built" and "Chosen for version 1"). The user asked for a
review of statistics_plan.md + sound_plan.md and then a first page; the choices below were made by the agent and the
user CONFIRMED them the same day ("good choices"). Also the user's: NEEDS ATTENTION's ChasmDaysHeld leaves camp
pantries out ON PURPOSE (only that family may eat from them) - not a bug; the demo's end screens just show this page
for now. wiki/events.md lists every event kind, what makes it and what keeps it - update it with each new EVENT_*.
History stays colony-wide (the user, 2026-10-16): split per settlement only once settlements grow larger, not now.
Discoveries belong to the audio work (areas_plan.md, EVENT_DISCOVERED, another session) - not the statistics agent's.

Both written into statistics_plan.md "Answered" (no open questions left) and wiki/statistics.md ([parked]) 2026-10-16.
Crates of origin CRATE_SITE (a cancelled site's goods, bridge_plan.md "Trees across the water") count as held too.

- One record for statistics and sound: `EconomyEvent` / `EVENT_*` / `Economy::Event` (ring of 64, `events_by_kind`).
  EVENT_* numbers are SAVED in the journal - append only, never renumber.
- `EconomyHistory` in EconomyState, saved as economy.history: journal (born, died, harvest, building finished first
  time, settled incl. landing, seam seen), a row a day (people by stage, goods the colony HAS - not rubble/finds -
  felled/broken/storeys), closed on the first tick of the next day so people rows follow births/deaths exactly.
- Hash: trace part "history", TAIL only (append-only; full hash per replay tick would be too slow).
- Page: Tab or click the top goods bar; pauses like the pop-up; replaces the HUD while up. chasm_stats op status/open/close.
- `ChasmStatsCheck`: journal vs dead/born, felled vs forest, row-to-row people. Empty in all tests.

**Why:** user's requirement - a load shows the same statistics, sums agree with what they sum.
**How to apply:** a new event = new EVENT_* at the end + EconomyEventName + ChasmJournalText (+ EconomyJournaled/
EconomyCounted). Re-run the three checks (save/load diff, replay twice, check empty). Pause menu (demo step 1) should
call OpenStats. See [[chasm-sound-plan]], [[chasm-game-plan]], [[void-plan]] (VoidBrief reads chasm_economy `notices`,
kept in its old shape).

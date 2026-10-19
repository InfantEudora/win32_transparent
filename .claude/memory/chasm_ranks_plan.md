---
name: chasm-ranks-plan
description: "Chasm kills + ranks (apps/chasm/docs/ranks_plan.md): BOTH STEPS BUILT 2026-10-19 - kills by bow/hand, ranks Blooded/Veteran/Champion at a kill each for now (rank_kills table), fight pace + arrow one-shot; perks the user's to think over"
metadata:
  node_type: memory
  type: project
  originSessionId: 0c59b567-ea5b-416c-9eec-44f20876729f
  modified: 2026-10-19T19:43:46.746Z
---

The user's decisions, 2026-10-19 (first "RPG elements"):
- Kills counted per person, by bow AND by hand, both shown; credit = the hit that fells (no assists).
- RANK from kills alone, ALL kills together; NEVER lost (worked out from kills each time, never stored).
- Ranks Blooded, Veteran, Champion. A KILL A RANK FOR NOW (1/2/3) to test faster - but each rank keeps its own
  entry in `rank_kills` (Economy.cpp) so any can get a different requirement later.
- A rank gives only: slightly quicker FIGHT pace (shot turns + the INTERVAL of hand blows, not their harm, not
  walking) and a chance a HIT ARROW kills outright (arrows only). Perks (target choice, nerve, steadying, a choice at
  a rank) the user "will think about" - kept under the plan's "Later".
- "Old veteran teaches at the range" is in lifeline_plan.md ("Old" + build step 8), after old age.
- The ELDER is NOT part of this plan: the user named it only as an example of a status/property on a character.

**As built** (plan "As built: step 1" and "step 2"): `EconomyWorker/EconomyDeath::kills` [KILL_BOW, KILL_HAND];
`Economy::ZombieKilled` (Zombies.cpp) credits (hand: the LAST striker of the tick) + EVENT_ZOMBIE_FELLED (25, counted,
silent) + EVENT_RANKED (26, journaled, notice, cue row `ranked`). Tables rank_kills/rank_pace/rank_one_shot;
EconomyRank & co. `EconomyArrow::f_fells` decided at the loose (ShotHash(now,id,~m.id)), `EconomyArrowHarm` in landing
and `coming`. Debug kills via add_person `kills` / People panel slider, packed in CHASM_CMD_PERSON bits 19-23,
counted in `EconomyHistory::kills_from` so ChasmStatsCheck holds. Everything new saved/hashed only when nonzero (old
saves round-trip identical). Journal death line has the rank BEFORE the name only (column cut longer lines).
Test: `python apps/chasm/tools/chasm_kills_test.py --port N` (10 cases; reads only the check's zombies line - test
maps always fail its people line). Wiki page kills.md (zombie/person/events pages sit at the 45-line cap).

**Why:** small colony, no immigration - a rank marks someone worth keeping alive, not a stat ladder.
**How to apply:** retune ranks in the three tables only; update the test's RANK_KILLS/RANK_PACE mirrors with them.
Related: [[chasm-range]], [[chasm-zombies-plan]], [[chasm-statistics]], [[chasm-game-plan]].

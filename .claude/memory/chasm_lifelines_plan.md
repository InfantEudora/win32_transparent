---
name: chasm-lifelines-plan
description: "Chasm lifelines (docs/lifeline_plan.md): steps 1-4 + crowding BUILT (old age 2026-10-21); 4b widows pairing NEXT; test save saves/old_age_test.json + tools/chasm_old_age_test.py"
metadata:
  node_type: memory
  type: project
  originSessionId: f7d5c5b8-011d-4ee5-940d-86c24ab2d1b0
  modified: 2026-10-20T22:47:12.618Z
---

apps/chasm/docs/lifeline_plan.md - a whole life per person. Status 2026-10-21:
- BUILT: 1 families/ageing, 2 births, 3 couples, shares (2026-10-09/10); 7 crowding (2026-10-18, via
  building_size_plan.md); 4 OLD AGE (2026-10-21).
- NEXT: 4b widows pairing again - the user's rule (2026-10-21): only "as long as the woman can have another child"
  (under LIFE_CHILDREN_MOST, not old). Open sub-question: do the widowed parent's growing children go along?
- Then 5 skills passed on, 6 skills growing, 8 the old Expert teaching at the range (ranks_plan.md).

Old age as built: LIFE_OLD stage at 36, WORKER_JOB_OLD (retired in AssignJobs' first loop), errands + GoPlay walks,
EconomyCanOrder excludes the old (no orders/fights/shooting, "too old"), dies at home via EconomyLifespan (hash slot
id*8+7) in Needs, NEEDS_DIED_OLD_AGE, EVENT_GREW_OLD. Wiki page wiki/old_age.md.

**The user accepted old age as a LONG-GAME feature** (settlers 24-32, first old after 2-6 h). Their idea: check it with
a stored save, `saves/old_age_test.json` (village_year5 + two old people, Jelle turns 36 in 1.5 days, Wessel dies
day 3). `python apps/chasm/tools/chasm_old_age_test.py --port N` runs 8 cases on it (all PASS 2026-10-21).

Open question raised, not answered: the old can't be ordered, so a household moving settlements leaves its
grandparent behind - should the old go along like children (WITH THE GROWN)?

**Why:** the plan doc is the record; this is the pointer plus the user's decisions that are easy to lose.
**How to apply:** read lifeline_plan.md's Status line first; build 4b next unless the user picks another step.
Related: [[chasm-game-plan]], [[chasm-ranks-plan]], [[chasm-settlements]].

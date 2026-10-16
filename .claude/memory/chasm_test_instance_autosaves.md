---
name: chasm-test-instance-autosaves
description: "A Chasm test instance's chasm_load (or new game) autosaves the colony it leaves into apps/chasm/saves/autosave_N.json - overwriting the user's untracked autosave slots"
metadata:
  node_type: memory
  type: feedback
  originSessionId: e532a19b-410c-4f18-b4c7-cc0b53ada9b0
  modified: 2026-10-16T12:34:11.969Z
---

Leaving a colony autosaves it (demo_plan, [[chasm-demo-menus]]), and `chasm_load` counts as leaving. A test
instance started on another port shares apps/chasm/saves/ with the user's game, so each load in it writes the
next of the rotating autosave_1..3 slots. They are untracked by git: what was there is gone.

Happened 2026-10-16 (sound work): two loads of village_year5 in a test instance overwrote slots 1 and 3; told
the user.

**Why:** the user's autosaves are their own play's safety net; an agent's test run should not spend them.
**How to apply:** before loading a save or starting a new game in a test Chasm, copy saves/autosave_*.json to
the scratchpad, and put them back after the instance is stopped (check mtimes). Say so in the report.

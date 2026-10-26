---
name: chasm-swamp-huts-plan
description: apps/chasm/docs/swamp_huts_plan.md - huts (step 1) and the first wave gated on an archer (step 2) BUILT 2026-10-26; later waves open
metadata:
  node_type: memory
  type: project
  originSessionId: 3d6f80ce-bdc3-46fa-b61d-5a5e4623ce6f
  modified: 2026-10-26T20:38:55.057Z
---

Swamp huts plan, `apps/chasm/docs/swamp_huts_plan.md`:
- **Step 1, BUILT:** three generated huts per new map.
- **Step 2, BUILT 2026-10-26:** the way to the first wave.

**The user's decisions (2026-10-26):**
- The wave does NOT come on a date. Year 2 was far too soon to have an archer, and starting the colony with one would be too easy.
- New goals appear on the way, each journaled as EVENT_GOAL_SET:
  - **Train an archer** (20% archery), from year 2's spring;
  - **Don't venture into the swamp unescorted**: appears once the swamp is found; failed by anyone there with no trained archer within 10 units; met when the wave is beaten;
  - **Survive the first wave**.
- An archer trained calls the wave for the second dawn after (spring's first if that falls in winter): one zombie per hut.
- Zombies march as they come out, and wade rivers (WALKER_SPEED_WADE).
- Saves from before the huts are left as they are: only a new game gets a wave.

**Why:** the demo's threat had to be paced to the player, who must have a defender first.

**How to apply:** the rules are in TickGoals (ApplicationChasmGoals.cpp) and Economy::TickWave (Zombies.cpp). `chasm_zombie` status shows `wave`. Open items are later waves, crows over the huts, and click-to-view on the notice. Related: [[chasm-zombies-plan]], [[chasm-modes-plan]], [[chasm-range]].

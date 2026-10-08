---
name: tuning-numbers-not-rules
description: "Gameplay figures (drains, bar sizes, starting supplies) are TUNING - docs and comments must not state them as fixed facts"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 1b15a32f-eb2e-4cf6-a5f2-67893e27d3be
  modified: 2026-10-08T08:10:22.775Z
---

Gameplay numbers - how long a need bar lasts, drain rates, what a unit is worth, thresholds, starting
supplies - are tuning, not decisions. Docs describe the MECHANICS as decided and present figures as the
current tuning, kept in one table (e.g. `apps/chasm/docs/needs_plan.md` "Current tuning") that names the
constant; code comments say what a constant IS, not its value.

**Why:** the user (2026-10-08): they will change a lot with testing and new features, and a difficulty
setting may later pick different starting values and drains. The chasm needs constants had already drifted
(5 -> 7 days etc. in commit 2b40394) while docs and comments still stated the old numbers as fact.

**How to apply:** when writing plans or comments for balance values, point at the constant and give the
number as "now X (first Y)"; when a constant changes, update that one table. Rough sums ("first death about
day N") are labelled as arithmetic from the current tuning. Related: [[chasm-game-plan]].

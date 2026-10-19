---
name: chasm-modes-plan
description: "Chasm modes plan (apps/chasm/docs/modes_plan.md) - Tutorial/Demo/Free play, goals as rules, a one-at-a-time tell-only track; step 0 built 2026-10-19"
metadata:
  node_type: memory
  type: project
  originSessionId: b9b4e427-c7da-4e28-884b-9742729f0722
  modified: 2026-10-19T19:50:05.635Z
---

apps/chasm/docs/modes_plan.md, talked through 2026-10-19. The demo's hard-coded goals generalised into three
modes (Tutorial, Demo, Free play - the user's "perhaps"), chosen on New game and saved with the colony.

Decided by the user: tutorial steps and story moments come ONE AT A TIME, and they ONLY TELL - the player's actions
are what change the colony. The goals panel is titled GOALS and a goal switched off is not drawn (step 0, BUILT
2026-10-19 in DrawGoals; compiled, not yet seen in the game).

Proposed, not agreed: goals stay rules (journaled, hashed); the track is the VIEWER's (UpdateView, its step saved,
never hashed) so a step can wait on viewer actions; steps TELL/TASK/NOTE from assets/story/<mode>.json with a small
condition vocabulary, `issue` leaning on Needs attention rows; one text per event.

**Why:** the switches only silenced notices and left the panel up; three places said "what next" separately.
**How to apply:** read the plan's Open list before building steps 1-5. Related: [[chasm-issues-panel]],
[[chasm-statistics]], [[chasm-demo-menus]].

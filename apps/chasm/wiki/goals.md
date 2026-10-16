---
title: The demo's goals
aliases: goals, goal, demo, demo complete, the end, colony lost, death pop-up
kind: systems
status: built
summary: Two goals - the first winter through with nobody dead, and the chasm reached - a pop-up for every death, and an end, the demo complete or the colony lost, on the statistics page.
plans: demo_plan.md > The demo's goals; demo_plan.md > As built: the goals and the end (2026-10-16)
code: Economy.h: CHASM_GOAL_WINTER CHASM_GOAL_CHASM EVENT_GOAL_MET EVENT_GOAL_FAILED EVENT_DEMO_COMPLETE EVENT_COLONY_LOST; ApplicationChasmGoals.cpp: ChasmGoalState TickGoals UpdateEnd DrawGoals; ApplicationChasmStats.cpp: OpenEnd UpdateEndPage; Calendar.h: CALENDAR_YEAR_TICKS
verified: 2026-10-16
---

The demo has **two goals**, shown at the top left in play mode, each in progress, met or failed:

1. **Through the winter, nobody lost.** Met at the first dawn of the second spring, {CALENDAR_YEAR_TICKS:days}
   after the landing, with nobody dead. Failed by the first death before then. Until then the panel
   counts the days to go.
2. **Reach the chasm.** Met once a [[Winch|winch]] is finished and someone has been lowered by it to a
   balcony ([[Discoveries]]). It can't fail. Until then the panel says what's next: find the chasm, build
   a winch, lower someone down.

Each is a rule: worked out on the tick from what is saved, the dead and the calendar and the discoveries,
and told once as an [[Events|event]] the [[Statistics|journal]] keeps. So a replay ends the same way, and
a load tells nothing again. A goal met or failed is told on the notice line.

## A death

Every death stops the game behind a pop-up: who died and of what, several on one tick together. If it
fails the first goal, the same pop-up says so. **Continue** plays on; **Main menu** goes to the title,
which loses nothing.

## The end

The end is the statistics page with its own heading and, at its foot, the goals and the way on:

- **Demo complete**, once both goals are decided, met or failed: **Keep playing** or **Main menu**.
- **The colony is lost**, when the last person dies: **Load**, **New game** or **Main menu**. With nothing
  left to lose, Load and New game don't ask, and a lost colony is never autosaved.

The pop-ups and the end's page are play mode's only. Each pauses the game as the pause menu does, which
changes nothing in the colony.

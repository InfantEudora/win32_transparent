---
title: The demo's goals
aliases: goals, goal, demo, demo complete, the end, colony lost, death pop-up
kind: systems
status: built
summary: Five goals, three of them appearing on the way - the first winter through, the chasm reached, an archer trained, the swamp kept out of without one, the first wave survived - a pop-up for every death but of old age, and an end on the statistics page.
plans: done/demo_plan.md > The demo's goals; done/demo_plan.md > As built: the goals and the end (2026-10-16); swamp_huts_plan.md > Step 2: the way to the first wave (as built, 2026-10-26)
code: Economy.h: CHASM_GOAL_WINTER CHASM_GOAL_CHASM CHASM_GOAL_ARCHER CHASM_GOAL_SWAMP CHASM_GOAL_WAVE ARCHER_TRAINED ESCORT_REACH EVENT_GOAL_SET EVENT_GOAL_MET EVENT_GOAL_FAILED EVENT_DEMO_COMPLETE EVENT_COLONY_LOST NEEDS_DIED_OLD_AGE; ApplicationChasmGoals.cpp: ChasmGoalState TickGoals SwampUnescorted UpdateEnd DrawGoals; ApplicationChasm.h: ChasmGoalShown; ApplicationChasmStats.cpp: OpenEnd UpdateEndPage; Calendar.h: CALENDAR_YEAR_TICKS
verified: 2026-10-26
---

The demo's **goals** are shown at the top left in play mode, each in progress, met or failed. The first two are there
from the landing; the others **appear** on the way, each told on the notice line ("New goal: ...").

1. **Through the winter, nobody lost.** Met at the first dawn of the second spring, {CALENDAR_YEAR_TICKS:days}
   after the landing. Failed by a death before then - but not of [[Old age|old age]], which is no villager lost.
2. **Reach the chasm.** Met once a [[Winch|winch]] is finished and someone has been lowered by it to a
   balcony ([[Discoveries]]). It can't fail. Until then: find the chasm, build a winch, lower someone down.
3. **Train an archer.** Appears at that first dawn of the second spring. Met by anyone alive with
   {= ARCHER_TRAINED * 100 / ARCHERY_FULL}% archery ([[Archery range]]): build a builders' hut, a range, then train.
   Meeting it calls the first wave out of the [[Swamp hut|swamp huts]].
4. **Don't venture into the swamp unescorted.** Appears once the swamp is found, on a map with huts. Failed by any
   grown-up out in the swamp with no trained archer within {ESCORT_REACH} units - an archer escorts himself - which ends
   nothing. Met when the first wave is beaten.
5. **Survive the first wave.** Appears as the huts' doors open; met when the last of its zombies has fallen.

Each is a rule: worked out on the tick from what is saved, the dead and the calendar and the discoveries,
and told once as an [[Events|event]] the [[Statistics|journal]] keeps. So a replay ends the same way, and
a load tells nothing again. A goal met or failed is told on the notice line.

## A death

Every death stops the game behind a pop-up: who died and of what, several on one tick together. If it
fails the first goal, the same pop-up says so. **Continue** plays on; **Main menu** goes to the title,
which loses nothing.

A death of **old age** stops nothing: it is no villager lost. It is told as every death is - on the red line
over the build bar, in the journal and in the [[Statistics|statistics]] - but with no pop-up.

## The end

The end is the statistics page with its own heading and, at its foot, the goals and the way on:

- **Demo complete**, once every goal in play is decided, met or failed - the first three, the wave's on a map with
  huts, the swamp's once it has appeared: **Keep playing** or **Main menu**.
- **The colony is lost**, when the last person dies: **Load**, **New game** or **Main menu**. With nothing
  left to lose, Load and New game don't ask, and a lost colony is never autosaved.

The pop-ups and the end's page are play mode's only. Each pauses the game as the pause menu does, which
changes nothing in the colony.

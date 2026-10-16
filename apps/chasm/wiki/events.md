---
title: Events
aliases: event, notice, notices
kind: systems
status: partial
summary: The moments the rules tell of - a birth, a harvest, a waterfall found - made once, and read by the notice line, the statistics and the sound.
plans: sound_plan.md > The events; sound_plan.md > Game events: one record, many readers; statistics_plan.md > As built: version 1 (2026-10-16); areas_plan.md > The discovered event
code: Economy.h: EVENT_NOTHING_TO_TAKE EVENT_RAN_OUT EVENT_NO_TENT EVENT_SEAM_SEEN EVENT_BORN EVENT_DIED EVENT_TREE_FELLED EVENT_ROCK_BROKEN EVENT_STOREY_RAISED EVENT_BUILDING_FINISHED EVENT_HARVEST EVENT_SETTLED EVENT_DISCOVERED ECONOMY_EVENTS_KEPT; Economy.cpp: Event Notice EconomyJournaled EconomyCounted; ApplicationChasmEconomy.cpp: TickEconomy; ApplicationChasmExplore.cpp: LookForSeams LookForAreas; ApplicationChasmSound.cpp: SignalEvents
verified: 2026-10-16
---

An **event** is a moment in the colony, made once by the rules as it happens, so a replay makes the same ones on
the same ticks. Everything that tells of it reads that one record: the notice line at the bottom of the screen,
the [[Statistics|statistics]] and the sound. Every event has a sound for now - the notification chime: the
colony's own (a birth, a death, a settlement, a notice) heard anywhere, one at a time; what happens at a place (a
tree down, a storey up, a harvest) only near the view.

## The events today

| Event | When | Kept as |
|---|---|---|
| Born | a child is born, at its mother's side | journal |
| Died | someone's health runs out - hunger, thirst, [[Cold|cold]] - or a [[Zombie|zombie]]'s bite takes the last of it | journal |
| Harvest | a [[Field|field]] is reaped: its crop and how much it gave | journal |
| Building finished | something the builders put up stands whole for the first time - every storey, a field's every cell | journal |
| Settled | a [[Founding|tent]] pitched outside every [[Settlement|settlement]] founds one; a new colony's settlers landing | journal |
| Floatstone seen | someone is the first to see a floatstone seam in a balcony's wall ([[Deposit|deposits]]) | journal, notice |
| Discovered | someone is the first to see an [[Areas|area]] - a river, a waterfall, a biome | journal; notice for the first of its kind |
| Tree felled | a [[Woodcutter|woodcutter]] fells a [[Tree|tree]] | counted |
| Rock broken | a [[Stonecutter|stonecutter]] breaks a rock or a boulder | counted |
| Storey raised | the builders finish a storey | counted |
| Nothing to take | someone told to gather finds no food or water anywhere they can get to ([[Posts|posts]]) | notice |
| Ran out | someone sent out runs out of water, food or both in their pack | notice |
| No tent | someone told to take a tent finds none to spare | notice |

- **Journal**: kept one by one in the statistics' journal, and saved.
- **Counted**: too many a day to keep one by one; each day's row in the statistics counts them.
- **Notice**: told in amber at the bottom of the screen. A death has a red line of its own, read from the colony's
  dead rather than from the event.

Events are never saved: the last {ECONOMY_EVENTS_KEPT} are kept for telling, so after a load nothing old is told
again. A seam already seen before the load is not seen a second time. What has to last is in the journal.

## Not events (yet)

- **Reaching the chasm.** Nothing marks it today: the demo's goal - a [[Winch|winch]] built and someone lowered by
  it - and the first sight of the chasm are planned, below.
- **The work itself** - chopping, building, sowing, walking. Only its end is an event; the work is the background.
- A refused placement or order, an arch taken down by a command, and NEEDS ATTENTION: the view tells those itself.

## Planned

- [planned] DISCOVERIES (demo_plan.md step 6): a winch built, the first person lowered to a balcony, floatstone
  seen, later the first of each building kind - each an event and a saved flag, for the goals and the build tools.
- [planned] The demo's goals: the first winter through with nobody dead, a goal met or failed, the demo complete.
- [planned] The chasm first seen, with the music's reveal (sound_plan.md).
- [proposed] A couple housed, someone grown up, a crop lost to frost or chosen by AUTO, a road or wall laid, a stock
  reached, a deposit dug out, a gate shut or opened, an arch down, the season and the snow (sound_plan.md).
- [proposed] Zombies: one coming, a bite, a blow, one down, an arrow loosed or hitting.

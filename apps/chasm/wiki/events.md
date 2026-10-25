---
title: Events
aliases: event, notice, notices
kind: systems
status: partial
summary: The moments the rules tell of - a birth, a harvest, a waterfall found - made once, and read by the notice line, the statistics and the sound.
plans: sound_plan.md > The events; sound_plan.md > Game events: one record, many readers; statistics_plan.md > As built: version 1 (2026-10-16); areas_plan.md > The discovered event; ranks_plan.md > As built: step 1, kills (2026-10-19)
code: Economy.h: EVENT_ZOMBIE_FELLED EVENT_RANKED EVENT_NOTHING_TO_TAKE EVENT_RAN_OUT EVENT_NO_TENT EVENT_SEAM_SEEN EVENT_BORN EVENT_DIED EVENT_TREE_FELLED EVENT_ROCK_BROKEN EVENT_STOREY_RAISED EVENT_BUILDING_FINISHED EVENT_HARVEST EVENT_SETTLED EVENT_DISCOVERED EVENT_FROST_TOOK EVENT_GREW_UP EVENT_GREW_OLD EVENT_EXPECTING EVENT_BOULDER_FOUND EVENT_BALCONY_REACHED EVENT_GOAL_MET EVENT_GOAL_FAILED EVENT_DEMO_COMPLETE EVENT_COLONY_LOST EVENT_WALL_BROKEN EVENT_GATE_BROKEN EVENT_BREACHED EVENT_PUT_DOWN ECONOMY_EVENTS_KEPT; Economy.cpp: Event Notice EconomyJournaled EconomyCounted Lifelines ChancesOfChildren AddPerson; ChasmStats.cpp: ChasmStatsCheck; ApplicationChasmEconomy.cpp: TickEconomy; ApplicationChasmExplore.cpp: LookForSeams LookForAreas LookForFirsts; ApplicationChasmSound.cpp: SignalEvents
verified: 2026-10-25
---

An **event** is a moment in the colony, made once by the rules as it happens, so a replay makes the same ones on
the same ticks. Everything that tells of it reads that one record: the notice line at the bottom of the screen,
the [[Statistics|statistics]] and the sound. Nearly every event has a sound for now - the notification chime: the
colony's own (a birth, a death, a settlement, a notice) heard anywhere, one at a time; what happens at a place (a
tree down, a storey up, a harvest) only near the view.

## The events today

| Event | When | Kept as |
|---|---|---|
| Born, put down | a child is born, at its mother's side; the debug tools put someone down - the People panel, X, `add_person` - which the [[Statistics|statistics]]' check counts in as it does a birth | journal; put down: no sound |
| Died | someone's health runs out - hunger, thirst, [[Cold|cold]] - or a [[Zombie|zombie]]'s bite takes the last of it, or [[Old age|old age]] at home | journal |
| Harvest | a [[Field|field]] is reaped: its crop and how much it gave | journal |
| Building finished | something the builders put up stands whole for the first time - every storey, a field's every cell | journal |
| Settled | a [[Founding|tent]] pitched outside every [[Settlement|settlement]] founds one; a new colony's settlers landing | journal |
| Floatstone seen | someone is the first to see a floatstone seam in a balcony's wall ([[Deposit|deposits]]) | journal, notice |
| Discovered | someone is the first to see an [[Areas|area]] - a river, a waterfall, a biome | journal; notice for the first of its kind |
| Boulder found | a boulder stands on the colony's own land, on explored ground - the first only ([[Discoveries]]) | journal, notice |
| Balcony reached | someone stands on a balcony, lowered by a [[Winch|winch]] - the first only ([[Discoveries]]) | journal, notice |
| Grew up, grew old | a child becomes a young adult and goes to work ([[Lifelines]]); someone turns old and works no more ([[Old age]]) | journal, notice |
| Expecting | a couple's chance of a child comes up at dawn ([[Lifelines]]) | journal, notice |
| Frost took | winter's first day takes a [[Field|field's]] crop still growing | journal; one notice for all of them |
| Goal met, goal failed | one of [[The demo's goals|the demo's goals]] decided | journal, notice |
| Demo complete | both goals decided | journal; the end's page |
| Wall broken, gate broken, breached | [[Zombie|zombies]] broke a palisade plot, a gate's door, or into a building | journal, notice |
| Colony lost | the last person dies | journal; the end's page |
| Tree felled | a [[Woodcutter|woodcutter]] fells a [[Tree|tree]] | counted |
| Rock broken | a [[Stonecutter|stonecutter]] breaks a rock or a boulder | counted |
| Storey raised | the builders finish a storey | counted |
| Zombie killed, rank risen | someone's hit takes the last of a [[Zombie|zombie's]] health, by bow or by hand: their [[Kills|kill]] - and with it, at a rank's count, their next [[Kills|rank]] | killed: counted, no sound; risen: journal, notice |
| Nothing to take | someone told to gather finds no food or water anywhere they can get to ([[Posts|posts]]) | notice |
| Ran out | someone sent out runs out of water, food or both in their pack | notice |
| No tent | someone told to take a tent finds none to spare | notice |

- **Journal**: kept one by one in the statistics' journal, and saved.
- **Counted**: too many a day to keep one by one; each day's row in the statistics counts them.
- **Notice**: told in amber at the bottom of the screen. A death has a red line of its own, read from the colony's
  dead rather than from the event.

Events are never saved: the last {ECONOMY_EVENTS_KEPT} are kept for telling, so after a load nothing old is told
again. A seam already seen before the load is not seen a second time. What has to last is in the journal.

A [[Discoveries|discovery]] told says what it brings: "... has found a river - new tools: WATER, BRIDGE". The
first builders' hut and the first winch finished are told too, for that.

## Not events (yet)

- **The work itself** - chopping, building, sowing, walking. Only its end is an event; the work is the background.
- A refused placement or order, an arch taken down by a command, and NEEDS ATTENTION: the view tells those itself.

## Planned

- [proposed] A couple housed, a crop chosen by AUTO, a road or wall laid, a stock reached, a deposit dug out, a gate
  shut or opened, an arch down, the season and the snow (sound_plan.md); a zombie coming, a bite, a blow, an arrow loosed or hitting.

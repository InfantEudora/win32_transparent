# Statistics Plan: what the colony did, kept so a load shows the same

Split out of `done/demo_plan.md` on 2026-10-15 as a project of its own. Version 1 built 2026-10-16 ("As built"). What it
chose where the brief left the choice open is under "Chosen for version 1", confirmed by the user the same day ("good
choices"); what is still open is under "Open questions".

## What it is for

- **A Statistics page** in the demo's pause menu (`done/demo_plan.md`, "The screens").
- **The summary on the demo's end screens**: "Demo complete" and "The colony is lost"
  (`done/demo_plan.md`, "The demo's goals").
- **A `chasm_stats` MCP tool** for agents, and for the Void (`apps/llm/docs/void_plan.md`), which
  asked for a journal of what happened: births, harvests with their yield, sites finished.

## The user's requirement (2026-10-15)

> On loading a save, the statistics should be the same. If the statistics sum up events and numbers,
> they'd need to be consistent.

What follows from it:
- **Sampling the published state on the view side is ruled out** for anything over time. It starts
  empty after every load.
- **The history is kept by the rules.** It is made in the tick, so a replay makes the same one.
- **The history is saved** with the rest of the economy (`EconomySaved`, `ChasmSave`).
- **Sums can't disagree with what they sum.** Either a total is worked out from the events or rows
  when it is shown, or it is kept beside them and the app's checks assert that the two agree.

## What there was before (2026-10-15)

- **The present only.** `EconomyState` held:
  - the people: `workers`, each with `born`, `mother`, `father`, job, health and skills;
  - the dead: `dead`, each an `EconomyDeath` with the tick, the cause, `born` and the parents;
  - stocks, fields, crates;
  - every tree's state (`prop_state`), so the number felled can be counted;
  - `notices`: the last 8, neither saved nor hashed, so a load forgot them.
- **Births could be worked out.** Anyone alive or dead with a mother was born in the colony,
  at `born`. The original settlers have no parents in the colony.
- **Deaths were kept,** with day and cause.
- **Harvests left no record**, nor did finished buildings: a harvest only grew the field's stock, and a
  site that completed only went up in `ApplicationChasm::TickEconomy` (`economy.Raises()`, `ZONE_OP_BUILD_RAISE`).
- **The state hash** is `HashSimState` / `HashEconomy`, split into the trace's parts. Chasm has no pinned
  replay baselines since its replay test was retired (`README.md`): replays are compared with replays.
- **Saves are not migrated** (`README.md`). A save from before the history loads with none.

## The page, as first drafted in demo_plan.md

- **Now:** people by stage and by job, homes and beds; the dead by cause; buildings by kind; goods
  held, and days of food, water and firewood; the colony's age.
- **Over time:**
  - line charts by day: people, food and water days, and stocks;
  - bars by season: births, deaths, harvests and trees felled.
- **A journal:** births, deaths, harvests with their yield, buildings finished, colonies founded,
  the first winter survived, and DISCOVERIES. Discoveries are the demo's own saved flags, each with
  the tick it was made on (`done/demo_plan.md`, "Discoveries"). The journal lists them; it doesn't own them.
- **Drawn** on UIOverlay: `AddLine`, `AddRect` and `AddText` are enough.

## Chosen for version 1 (2026-10-16, confirmed by the user)

- **One record with the sound plan.** The journal is made of `sound_plan.md`'s event record, not a second
  struct: `Economy::Event` makes an event once, the ring of 64 tells it, and the history keeps the kinds it
  needs as they are made. That is `sound_plan.md` step 1's record, built here with the events the statistics need.
- **In the hash**, as a trace part of its own, `history`. Only its tail is hashed: the history is only ever added
  to and a replay is traced every tick, so an entry that parts two replays does so on the tick it is added.
  Hashing all of it byte by byte, every tick, would slow a long replay more the longer the game.
- **Nothing folded away.** A year is 40 rows, 30 minutes of play at 1x; fifty years is 2,000 rows of a dozen
  numbers. Folding old years into seasons can come if a save ever shows the need.
- **The journal keeps:** born; died, with the cause; a harvest, with its crop and yield; a building finished, the
  first time it stands whole; a settlement founded, the landing too; a floatstone seam first seen. Not trees
  felled, rocks broken or storeys raised: tens a day, so each day's row counts them.
- **A day's row:** the people by stage and every good the colony has at the day's end, and that day's trees felled,
  rocks broken and storeys raised. *Has* means gathered - stocks, pantries, packs, loads carried and crates its
  people put down - not rubble or finds lying out there.
- **A day closes on the first tick of the next**, before anything of that tick happens. So a row's people are the
  row before's, plus that day's births, less its deaths, exactly - which is what the check holds it to. Closing when
  the day changes, rather than on dawn's tick, also closes the right day when the date is set by hand.
- **A save from before the history** begins it at its load, with the levels and the forest's count then. The page
  says "kept since" instead of "since the landing".
- **Totals are summed when shown**, from the journal and the rows: never kept, so never out of step.
- **Where the page opens**, until the pause menu exists: Tab, or a click on the top bar's figures, which carry a small
  chart mark at their right end. It pauses the game as the pop-up does; Tab or Escape closes it.

## As built: version 1 (2026-10-16)

- **The record** (`Economy.h`, AN EVENT): `EconomyEvent` - tick, kind, person, building, plot, pos, what, amount -
  with `EVENT_*` kinds that are never renumbered, since the journal saves them. The four notices are its first four
  kinds, and the notice line picks them out of the ring. `Economy::Event` makes them:
  - in the rules: born (`Lifelines`), died (`Needs`), tree felled, rock broken (`BreakRock`), harvest (the fields'
    loop in `Economy::Tick`), and a new colony's landing (`FollowZones`);
  - in the app, stamped with the tick they belong to: storey raised and building finished (`TickEconomy`, after
    each raise), and a settlement founded (the PITCH command, when the tent makes a camp of its own).
- **`events_by_kind`** counts every kind. Felled trees push a scout's notice out of 64 in half a minute at 4x, so a
  reader that looks seldom still knows what it missed. `chasm_economy` keeps its `notices` as before (the Void's
  `VoidBrief` reads them) and adds `events`.
- **The history** (`Economy.h`, THE HISTORY): `EconomyHistory` in `EconomyState` - `from`, `start`, `felled_from`,
  `broken_from`, `journal`, `days`, `today`. Saved as `economy.history` (`ChasmSave.cpp`), restored by
  `Economy::Restore`, today's row too, so a load mid-day counts on from where it was.
- **The figures** (`ChasmStats.h`): `ChasmStatsNowOf`, `ChasmStatsSum`, `ChasmStatsSeasons`, `ChasmStatsDays`,
  `ChasmStatsCheck` and `ChasmJournalText`, from a published state - view only.
- **The page** (`ApplicationChasmStats.cpp`), in place of the HUD while it is up:
  - NOW: people by stage, food and water in days, homes, goods held, buildings by kind, settlements, the dead;
  - OVER TIME: the totals as tiles; by day, a line chart each for people, food and water in days, wood, and stone;
    by season, bars of born, died, harvested and felled, the last two years;
  - the JOURNAL, newest first, scrolled by the wheel.
  - Charts: one measure to an axis; wood and stone each a chart of their own, as their colours are too alike to
    share one; seasons as a strip under their bars, since spring's and summer's colours are too alike to tell a bar
    by (checked with the dataviz validator). The day's values under the mouse on the line charts.
- **`chasm_stats`**: the same figures, the rows and the journal, and the check; op open and close.
- **The check**: the journal's deaths are the dead since it began, and its births everyone born in the colony since;
  the trees felled and rocks broken it counted are the forest's, less what was down when it began; each row's
  people follow from the row before. `chasm_stats` reports it in every build; a debug build logs it once a day and
  shows it at the foot of the page.
- *Checked* (port 8770, release; the debug objects compiled, and the ship build built):
  - `village_year5`, a save from before the history: it began at the load, with 280 trees down. After 3 days, 3 rows
    and 5 lines - two houses finished, three harvests with their yields. Saved and loaded: `chasm_stats` the same in
    every figure.
  - A 9,000-tick recording from that save, replayed twice: the traces the same at every tick in all seven parts,
    `history` among them, and the statistics the same as each other and as the run recorded.
  - A new colony: "12 settlers made camp", a birth on spring 9, the rows' people following it. A tent pitched 65
    units west: "Dirk De Wit founded a settlement", two settlements.
  - The check empty throughout.
- *Not checked live*: a field finished (`FieldRaises`), a rock broken, a seam seen reaching the journal.
- **Noticed while testing**, not changed: a new colony left alone loses people to thirst by early summer (three of
  twelve by summer 8). The rules are untouched by this work - nothing in them reads the events or the history.

## Answered (the user, 2026-10-16)

- **The choices for version 1**: confirmed - "good choices".
- **Two food figures, on purpose.** NEEDS ATTENTION's days (`ChasmDaysHeld`) leave a camp's pantry out because only
  the family living there may eat from it, not everyone; that is good enough for now. The page's days are what the
  colony has, pantries in.
- **The end screens** show this page, for now (`done/demo_plan.md` step 7).
- **The events** are listed on a wiki page of their own, `wiki/events.md`: what makes each, and what keeps it.
- **Per settlement: not now.** "History as a whole is good enough for now. When we expand the game to have more
  settlements that grow larger, we might break it up." The page and the history stay the colony's; the settlements
  appear only in NOW, with their people and beds. (Rows per settlement would need a settlement id that lasts: today's
  is its oldest home's, which changes when that home goes.)
- **Discoveries** belong to the audio work under way (`areas_plan.md`, `EVENT_DISCOVERED`), not to this plan.

## Open questions

None.

## Working here

- Others work in `Economy.cpp` and `Economy.h` too (zombies, stone). Claim through lockd, as CLAUDE.md says.
- A new event: an `EVENT_*` number after the last, its name in `EconomyEventName`, its words in `ChasmJournalText`,
  and `EconomyJournaled` or `EconomyCounted` if the history is to keep it. Never renumber.
- The wiki page is `wiki/statistics.md`.
- **Check it three ways**, as above: save and load and compare `chasm_stats`; replay a recording twice and compare
  every tick (`replay_trace`); and the check, empty.

## Steps

1. ~~Confirm the shape with the user~~ - chosen for version 1, above, for the user to confirm.
2. *Built*: the ledger and the journal in the rules and the save, with the check.
3. *Built*: `chasm_stats`, and the three checks.
4. *Built* as a page of its own, opened by Tab and the top bar. The pause menu (`done/demo_plan.md` step 1) is to open
   it once that is built.
5. The end screens: the Statistics page itself, for now (the user's), with `done/demo_plan.md` step 7.

---
title: Statistics
aliases: statistics page, stats, journal, ledger, history
kind: systems
status: partial
summary: What the colony did, kept by the rules and saved: a journal of what happened and a row for every day, on a page of their own.
plans: statistics_plan.md > Chosen for version 1 (2026-10-16, confirmed by the user); statistics_plan.md > As built: version 1 (2026-10-16); sound_plan.md > Working with the statistics plan
code: Economy.h: ECONOMY_EVENTS_KEPT ECONOMY_STAGES EVENT_BORN EVENT_SETTLED; Economy.cpp: Event CloseDay BeginHistory EconomyDayLevels EconomyJournaled; ChasmStats.cpp: ChasmStatsCheck ChasmStatsNowOf; ApplicationChasmStats.cpp: DrawStats UpdateStats
verified: 2026-10-16
---

Press **Tab**, click the figures in the top bar, or choose Statistics in the pause menu, for the **Statistics page**.
The game pauses under it; Tab or Escape closes it, back to the pause menu if it was opened from there. It is
also [[The demo's goals|the demo's end]], with its own heading and buttons.

## What is kept

Everything that happens is made once by the rules as an [[Events|event]], and the colony's history is kept from those, as
they happen - so a replay keeps the same history, and a load shows what was saved.

- **The journal** remembers these one by one: a birth, a death and its cause, a harvest with what it gave, a
  building standing whole for the first time, a [[Settlement|settlement]] founded ([[Founding|a tent]] pitched
  outside every one, and the settlers' landing), a floatstone seam first seen ([[Deposit|deposits]]), an area
  found, someone grown up, a couple expecting, a crop the frost took, and the firsts the [[Discoveries|discoveries]]
  read: a boulder found, someone on a balcony.
- **A row a day** holds the [[Person|people]] by stage and every good the colony has at the day's end, and how many
  [[Tree|trees]] were felled, rocks broken and storeys raised that day. *Has* means gathered: stores, homes, packs and
  loads, and [[Crate|crates]] people put down - not rubble or finds still lying out there.

Totals are added up from these whenever they are shown. Nothing is thrown away: a year is
{CALENDAR_SEASONS} seasons of {CALENDAR_DAYS_PER_SEASON:#days}, a row each.

A save from before the history starts it at its load; the page then says "kept since" that day.

## The page

- **Now**: the people by stage, food and water in days for everyone at their shares, homes and beds, the goods
  held, buildings by kind, each settlement's people and beds, and the dead by cause.
- **Over time**: the totals; a chart by day of people, food and water, wood and stone, the day's values under the
  mouse; and bars by season of births, deaths, the harvest and trees felled, for the last two years.
- **The journal**, newest first. The wheel scrolls it.

## Checked as it goes

The history must agree with the colony it was kept beside: the journal's deaths are the dead since it began and
its births everyone born since; the trees felled it counted are the forest's; and each day's people are the day
before's, plus its births, less its deaths. `chasm_stats` reports any disagreement, and a debug build logs it.

## Planned

- [proposed] History per settlement, not only the colony's.

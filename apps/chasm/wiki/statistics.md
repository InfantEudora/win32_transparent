---
title: Statistics
aliases: statistics page, stats, journal, ledger, history
kind: systems
status: partial
summary: What the colony did, kept by the rules and saved: a journal of what happened and a row for every day, on a page of their own.
plans: statistics_plan.md > Chosen for version 1 (2026-10-16, confirmed by the user); statistics_plan.md > As built: version 1 (2026-10-16); sound_plan.md > Working with the statistics plan; ranks_plan.md > As built: step 1, kills (2026-10-19); settlements_plan.md > As built: statistics by colony
code: Economy.h: ECONOMY_EVENTS_KEPT ECONOMY_STAGES EVENT_BORN EVENT_SETTLED EVENT_ZOMBIE_FELLED; Economy.cpp: Event CloseDay BeginHistory EconomyDayLevels EconomyJournaled EconomyColonyRow; ChasmStats.cpp: ChasmStatsCheck ChasmStatsNowOf ChasmColonyHistory ChasmStatsColonies; ApplicationChasmStats.cpp: DrawStats UpdateStats
verified: 2026-10-24
---

Press **Tab**, click the figures in the top bar, or choose Statistics in the pause menu, for the **Statistics page**.
The game pauses under it; Tab or Escape closes it, back to the pause menu if it was opened from there. It is
also [[The demo's goals|the demo's end]], with its own heading and buttons.

## What is kept

Everything that happens is made once by the rules as an [[Events|event]], and the colony's history is kept from those, as
they happen - so a replay keeps the same history, and a load shows what was saved.

- **The journal** remembers these one by one: a birth, a death with its cause and the age, a harvest with what it gave, a
  building standing whole for the first time, a [[Settlement|settlement]] founded ([[Founding|a tent]] pitched
  outside every one, and the settlers' landing), a floatstone seam first seen ([[Deposit|deposits]]), an area
  found, someone grown up, a couple expecting, a crop the frost took, someone risen a [[Kills|rank]], and the firsts the [[Discoveries|discoveries]]
  read: a boulder found, someone on a balcony.
- **A row a day** holds the [[Person|people]] by stage and every good the colony has at the day's end, and how many
  [[Tree|trees]] were felled, rocks broken, storeys raised and [[Zombie|zombies]] killed that day. *Has* means gathered: stores, homes, packs and
  loads, and [[Crate|crates]] people put down or a camp or site left - not rubble or finds still lying out there.
- **By colony**, each row holds the same again for every [[Settlement|colony]]: its people by their homes, its goods
  by where they lie, its counts by where they happened - and *nobody's*, the homeless off every colony's land and
  what lies there, so they all add up to the whole. Each journal line knows its colony too.

Totals are added up from these whenever they are shown. Nothing is thrown away: a year is
{CALENDAR_SEASONS} seasons of {CALENDAR_DAYS_PER_SEASON:#days}, a row each.

A save from before the history starts it at its load; the page then says "kept since" that day.

## The page

- **Now**: the people by stage, food and water in days for everyone at their shares, homes and beds, the goods
  held, buildings by kind, each settlement's people and beds, and the dead by cause.
- **Over time**: the totals - zombies killed among them once there has been one; a chart by day of people, food and water, wood and stone, the day's values under the
  mouse; and bars by season of births, deaths, the harvest and trees felled, for the last two years.
- **The journal**, newest first. The wheel scrolls it.
- **All, or one colony**: with more than one, chips beside the title choose. One colony's page is its own now, totals,
  charts and journal, "founded" on the day its rows begin.

## Checked as it goes

The history must agree with the colony it was kept beside: the journal's deaths are the dead since it began and
its births everyone born since; the trees felled it counted are the forest's; the zombies killed are everyone's
kills, the living's and the dead's; each day's people are the day
before's, plus its births, less its deaths; and each row's colonies add up to it. `chasm_stats` reports any
disagreement, and a debug build logs it.

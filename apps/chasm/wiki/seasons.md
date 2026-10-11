---
title: Seasons
aliases: calendar, season, winter, spring, summer, autumn, snow, year, day, days
kind: world
status: built
summary: A year of four seasons of ten days each. Winter's snow sweeps down from the north, stopping fields and water collectors and calling for firewood.
plans: gameplay_plan.md > P1, the calendar; seasons_plan.md > Decided; seasons_plan.md > Crossing the ice (the user's, 2026-10-12)
code: Calendar.h: CalendarDateOf CalendarSnowCover CalendarSnowAt; Seasons.h: ForestSeasonal; Walkers.h: SetSeason Frozen; Economy.cpp: IceHome EconomyIceDays
verified: 2026-10-12
---

The calendar is one count of ticks since the colony's first dawn. A day lasts
{CALENDAR_DAY_TICKS:seconds} at normal speed, a season {CALENDAR_DAYS_PER_SEASON:#days}, a year
{CALENDAR_SEASONS:#seasons}. A colony starts on spring 1 of year 1. The game speed changes how fast the
days go by, never what happens in them. There is no day and night.

## The snow

A front runs east to west, and north of it everything is under snow. It edges in over autumn's last two
days, sweeps south through winter's first four, holds there, and melts back north over spring's first
five. At its furthest it covers {CALENDAR_SNOW_REACH} of the map's depth from the north edge, so the swamp
and the desert in the south never see it. The front wanders north and south along its length rather than
running straight. **The first spring has no snow**: there was no winter before the colony.

## What winter changes

| | |
|---|---|
| [[Field|Fields]] | nothing sown in winter or under snow, nothing grows, and winter's first day takes whatever still grows |
| [[Water collector|Water collectors]] | stop filling while snow lies on them |
| Rivers and pools | frozen where the snow lies: the [[Posts|posted]] may walk the ice, nobody else. The walk tip says "over the ice, it thaws in N days", and the card of anyone whose only way home is over it counts down to the thaw - still across then, they are cut off until a bridge or the next winter, and their card says so |
| Homes | burn firewood at dawn while under snow ([[Cold]]) |
| [[Tree|Trees]] | broadleaves turn one by one through autumn and stand bare in winter (a look only) |
| Ground | grass fades smoothly through the year; flowers only in spring and summer (a look only) |

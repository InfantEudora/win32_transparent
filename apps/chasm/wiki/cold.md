---
title: Cold
aliases: firewood, heat, warm, woodshed, chimney
kind: needs
status: built
summary: Under winter's snow a home must burn firewood each dawn. A home that can't is cold that day, and its people lose health faster.
plans: needs_plan.md > As built: step 3
code: Economy.cpp: Heat FirewoodNow FirewoodFair; Economy.h: EconomyCold HEAT_COLD; ApplicationChasmHearth.cpp
verified: 2026-10-09
---

## At dawn

Every **inhabited** home under the [[Seasons|snow]] burns firewood at dawn: {ECONOMY_FIREWOOD_BURN} wood
for each plot-storey of a [[House|house]], or each plot of a [[Camp|camp]] (tents and fires alike). With
enough, the home is **warm** that day. With too little it burns nothing, and its family is **cold** until
the next dawn.

- If nobody is home, nothing burns. The south, which the snow never reaches, never needs a fire.
- Someone with no home at all is cold under the snow wherever they wait.

## Being cold

While cold, [[Health|health]] drains {NEEDS_COLD_DRAIN} more each tick, but food still counts, so cold
shows first as hunger. Someone who dies while cold (and not dry) dies **of cold**. A couple only has a
chance of a child in a home that was warm that day ([[Lifelines]]).

## Firewood

- A house keeps its firewood in a **woodshed**, apart from its food and water, holding
  {ECONOMY_WOODSHED_DAYS:#days} of its burning. A camp has one too, in its pantry.
- The family [[Fetching home|fetches]] firewood when it holds less than {ECONOMY_HOME_DAYS:#days} of
  burning. That happens only from autumn's first day, through winter, and in spring while snow lies on the
  house, and only for houses that the full winter's snow reaches.
- **Fair shares**: no house fetches more than its part of all the firewood at homes, so while
  [[Wood|wood]] is short the houses fill up together.
- Nothing is kept back from [[Construction|building sites]]: by autumn, the building is meant to be done.

## On screen

A warm home smokes, from a house's chimney or a camp's fires, and a warm house's windows glow.

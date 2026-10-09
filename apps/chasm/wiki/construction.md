---
title: Construction
aliases: building site, building sites, site, sites, builder, builders, builders' hut, builders' huts, build first
kind: systems
status: built
summary: What the player places in play is a site, built a storey at a time from wood that people without a job, and builders, carry to it.
plans: construction_plan.md > As built; construction_plan.md > Which site goes first; builders_plan.md > As built
code: Economy.cpp: FindSiteWork Build SiteNeed AssignJobs EconomyStoreyCost EconomyCellCost; Economy.h: WORKER_JOB_BUILDER ECONOMY_SITE_GROUND
verified: 2026-10-09
---

## Sites

A building placed in play has its storeys planned, and each stands only once it's built. Until then a
storey does nothing: no room, no family, no worker. **Wood is all it takes**:

- a building's storey: {ECONOMY_BUILD_WOOD_PER_AREA} per unit of floor area, more or less by kind (a
  camp's tents less, a winch more, a bridge less: `BuildFactor`);
- a garden's or lot's plot: {ECONOMY_GROUND_WOOD_PER_AREA} per unit of area, built from a plot beside it,
  so the fence never goes up around the builder;
- a [[Field|field]]: {ECONOMY_FIELD_WOOD_PER_CELL} a cell, every cell at once;
- roads and walls: see [[Roads and walls]].

A kind of building can also take other goods, so much a plot-storey (`EconomyStoreyCost`), and a kind
painted on cells so much a cell (`EconomyCellCost`); none does yet. The guard tower's water and provisions
will be the first ([[Posts]]). A storey stands only once all it takes of every good is there, and what it
took is used up. A site pulled down loses what was brought to it.

## Who builds

Everyone without a job, and **builders**, the job at a **builders' hut**. They go from home to the nearest
of what the site still lacks - [[Wood|wood]], or any other good it takes - (a [[Store]], the [[Camp|camp's]]
supplies, a [[Woodcutter|woodpile]] for wood, an empty [[House|house]], a [[Crate|crate]]), carry a load to the site, and build
with it for {ECONOMY_BUILD_SECONDS:#seconds} at their pace ([[Skills|strength]]). Once enough is there, every
storey it pays for stands, lowest first. Nothing is kept back: a site's food or water comes out of the same
stocks people live on.

Builders' huts are staffed before any other workplace: one builder for each standing plot-storey of the
hut, and one while it is still a site, who builds the hut first. When everyone has a job, the huts are
what keep building going. With nothing to build and no road work, a builder carries a [[Crate|crate]]'s
goods to a store.

## Which site first

Nearest by the walk, so a colony builds close together. Among errands within {ECONOMY_QUEUE_SLACK} world
units of the nearest, though, the one placed first goes first. A site the player marks **Build first**
comes before everything, however far away. Roads and walls share this queue.

The settlers' supplies hold {ECONOMY_START_WOOD} wood, so the first buildings go up before there is a
woodcutter.

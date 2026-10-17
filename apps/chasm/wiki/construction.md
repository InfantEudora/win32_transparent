---
title: Construction
aliases: building site, building sites, site, sites, builder, builders, builders' hut, builders' huts, build first
kind: systems
status: built
summary: What the player places in play is a site, built a storey at a time from wood that people without a job, and builders, carry to it.
plans: construction_plan.md > As built; construction_plan.md > Which site goes first; construction_plan.md > Cancel (the user, 2026-10-16); builders_plan.md > As built; yards_plan.md > Noted; woodcutting_plan.md > Decided (the user, 2026-10-11)
code: Economy.cpp: FindSiteWork Build SiteNeed AssignJobs EconomyStoreyCost EconomyKindStoreyCost EconomyCellCost KeepTowers MakeProvisions MoveCrate SiteTreesStanding Felled Derive; Economy.h: WORKER_JOB_BUILDER ECONOMY_SITE_GROUND CRATE_SITE; Zones.h: ZONE_OP_SITE_CANCEL ZONE_OP_FIELD_CANCEL; ApplicationChasmGhost.cpp: ToolCostTip DrawCostTips
verified: 2026-10-17
---

## Sites

A building placed in play has its storeys planned, and each stands only once it's built. Until then a
storey does nothing: no room, no family, no worker. **Wood is all it takes**:

- a building's storey: {ECONOMY_BUILD_WOOD_PER_AREA} per unit of floor area, more or less by kind (a
  camp's tents less, a winch more, a bridge less: `BuildFactor`);
- a garden's or lot's plot: {ECONOMY_GROUND_WOOD_PER_AREA} per unit of area, built from a plot beside it,
  so the fence never goes up around the builder. Anyone else it goes up around may climb out over it, but
  nobody climbs in;
- a [[Field|field]]: {ECONOMY_FIELD_WOOD_PER_CELL} a cell, every cell at once;
- roads and walls: see [[Roads and walls]];
- an arch over a road: its building's own cost a storey, as part of that building's site ([[Roads and walls]]).

A kind of building can also take other goods, so much a plot-storey (`EconomyStoreyCost`), and a kind
painted on cells so much a cell (`EconomyCellCost`): a [[Guard tower|guard tower]] takes water and
[[Provisions|provisions]], and keeps them as its stock. A [[Winch|winch]] takes [[Rope|rope]], which the colony has
to make first. A storey stands only once all it takes of every good is there, and what it took is used up.
**Cancel** on a site's card, while none of it stands, takes it off whole - a [[Bridge|bridge]] too - and what was
brought is left where it stood as a [[Crate|crate]], "A site's goods"; so when the eraser takes its last plot.

**A site over trees** waits for them: they stand until a [[Woodcutter]] fells them, the first he goes for,
and nothing of it rises until the last is down - then at once, if it has all it takes. Its card counts them.

**Before placing**, the tool in hand shows beside the cursor what a click would cost, by these same rules
(`EconomyKindStoreyCost`), and the trees to fell first: a dragged line or bridge all of it, nothing for erasing,
only HIDDEN UNDER THE CLOUDS under them. A tool hovered on the build bar shows about what a plot or cell of it costs.

## Who builds

Everyone without a job, and **builders**, the job at a **builders' hut** - only for sites in their own
[[Settlement|settlement]]. They go from home to the nearest
of what the site still lacks - [[Wood|wood]], or any other good it takes - (a [[Store]], the [[Camp|camp's]]
supplies, a [[Woodcutter|woodpile]] for wood, an empty [[House|house]], a [[Crate|crate]]), carry a load to the site, and build
with it for {ECONOMY_BUILD_SECONDS:#seconds} at their pace ([[Skills|strength]]). Once enough is there, every
storey it pays for stands, lowest first. Nothing is kept back: a site's food or water comes out of the same
stocks people live on.

Builders' huts are staffed before any other workplace: one builder for each standing plot-storey of the
hut, and one while it is still a site, who builds the hut first. When everyone has a job, the huts are
what keep building going. A builder is also the colony's **caretaker**, and his errands go in this order:

1. a [[Guard tower|guard tower]] short of its water or provisions: he carries them there;
2. [[Provisions|provisions]] something wants, or the colony keeps: he makes them at his hut, and takes the
   kept ones to a store;
3. building, as above, and road work;
4. a [[Crate|crate]]'s goods to a store.

The jobless only ever do the third.

## Which site first

Nearest by the walk, so a colony builds close together. Among errands within {ECONOMY_QUEUE_SLACK} world
units of the nearest, though, the one placed first goes first. A site the player marks **Build first**
comes before everything, however far away. Roads and walls share this queue.

The settlers' supplies hold {ECONOMY_START_WOOD} wood, so the first buildings go up before there is a
woodcutter.
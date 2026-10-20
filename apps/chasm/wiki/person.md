---
title: Person
aliases: people, settler, settlers, villager, villagers, worker, workers
kind: people
status: built
summary: Everyone in the colony, each with a family, a home, four skills, a job, and two bars that must not run out.
plans: people_plan.md > As built; lifeline_plan.md > As built: step 1; needs_plan.md > Decided; selection_plan.md > What a person carries; selection_plan.md > How far along; zombies_plan.md > Step 1b: spacing out; zombies_plan.md > Step 1c: a crowd keeps its room
code: Economy.h: EconomyWorker EconomySkillFactor EconomyBody EconomyCarryPace EconomyStartTimer EconomyTaskProgress EconomyLoadRoom; Economy.cpp: AssignJobs HouseFamilies TickWorker Needs EconomyPackFood FindSiteWork; Spacing.cpp: SpaceOut
verified: 2026-10-20
---

The colony is the **settlers** it starts with, a few families waiting at the [[Camp]], and nobody else.
Nobody arrives and nobody leaves, so a colony that can't feed itself dies out.

## Who they are

- A **family** and a **home**: the camp, until a [[House]] has room for the whole family.
- Four **skills** from 1 to 10, random at the start: strength, precision, husbandry and ingenuity.
- An **age** and a **stage of life**: baby, child, young adult or adult ([[Lifelines]]). Babies and
  children do no work; children go out to play near home.

## Jobs

The player never assigns jobs, and nobody takes on work outside their own [[Settlement|settlement]]. An empty
place at a workplace goes to the free grown-up of its settlement with the best
score: their skill for that job, minus the walk from home (one point of skill is worth
{ECONOMY_SKILL_DISTANCE} world units). So **where you paint houses decides who works where**.
[[Builders' hut|Builders' huts]] are staffed first, then the other workplaces in the order they were
placed, so with too few people the newest workplaces stand empty. [[Field|Fields]] are farmed by the
family whose land they are. A workplace's card can **stop work** there - a [[Water collector|collector]],
woodcutter's hut, ropewalk, builders' hut or range: it takes nobody, and its people go free.

The job's skill sets the **pace** of the work (felling, sowing, building): average at 5, faster above
that, slower below (`EconomySkillFactor`). Empty-handed everyone walks alike; with a load, the
stronger walk faster ([[Skills]]).

Anyone without a job carries whatever [[Construction|building sites]] want, and clears and lays
[[Road|roads]] - after fetching any [[Crate|crate]] the player has put PICK UP. The player can **order** a grown-up to stand on a [[Posts|post]]; that is their job until
they are released. A load they are carrying when the order comes is put down as a [[Crate|crate]].

## A round

Every round starts and ends at home. At home, a person first sees to a pressing [[Thirst|thirst]] or
hunger, then [[Fetching home|fetches]] for the house if it is running low, then does the next piece of
their job. People walk straight across open ground, and follow the grid where something is in the way.

## Keeping apart

Out of doors, people keep apart from one another, keep to the right meeting someone, and queue behind someone walking
their way - only the body moves, never the route ([[Crowds]]). A [[Zombie|zombie]] is kept out of his room as well.

## Needs

A person has two bars that drop by one every tick: [[Health]], which food fills, and water
([[Thirst]]). In winter, a cold house drains health faster ([[Cold]]). At zero health a person dies, and
the colony records the cause: hunger, thirst or cold.

## Their card

By the name, age and job ("Age 25 - Woodcutter / posted"), a [[Kills|rank]] first once they have one ("Expert, age 25 - ..."); under it family, home, workplace, what they are doing,
two bars, skills, [[Archery range|archery]] once trained and [[Kills|kills]] once any. A [[Posts|pack]] is a goods strip,
as a [[Store|store's]] card shows what it holds - hollow, their own, with what it is worth in days at the right ("water 12 days, food 10").

Work done on the spot - felling, building, sowing, drawing water - has a **bar** filling under what they are
doing. What they **carry** is a strip too, **striped** as reserved goods are, since it is on its way somewhere
("Load - for a site"), against what that errand carries: {ECONOMY_LOAD}, a woodcutter two logs.

## Planned

- [planned] Births, old age, and couples moving into a free house as a new family.

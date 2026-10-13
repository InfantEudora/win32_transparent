---
title: Skills
aliases: skill, strength, precision, husbandry, ingenuity, pace
kind: people
status: partial
summary: Four base skills from 1 to 10. A job's skill decides who gets the job and how fast they work it.
plans: gameplay_plan.md > People; lifeline_plan.md > Skills passed on
code: Economy.h: SKILL_STRENGTH EconomySkillFactor EconomyJobSkill; Economy.cpp: AssignJobs Walk
verified: 2026-10-12
---

| Skill | Used today by | Proposed for later |
|---|---|---|
| Strength | [[Woodcutter|woodcutters]], [[Construction|builders]], anyone laying a road or carrying a load | |
| Precision | [[Archery range|archers]], [[Guard tower|guards]], [[Ropewalk|ropemakers]] | toolmakers |
| Husbandry | [[Field|farmers]], [[Fruit|pickers]] | herders, cooks |
| Ingenuity | [[Water collector|water carriers]] | boilers, machines |

- **Who gets a job**: an empty place goes to the free grown-up with the best score, which is their skill
  for the job (each point worth {ECONOMY_SKILL_DISTANCE} world units) minus how far they live from the
  workplace, in a straight line ([[Person|jobs]]).
- **Pace**: the job's skill sets how fast its work goes (felling, sowing, building, laying a road, and how
  fast a field grows): average at 5, faster above, slower below (`EconomySkillFactor`). A water collector
  fills at the same rate whoever carries for it.
- **Carrying**: anyone with a load walks by their **strength**, whatever their job: {ECONOMY_CARRY_STRENGTH}
  faster a point over 5, slower under, about 25% from 1 to 10. Empty-handed, everyone walks alike.
- Settlers' skills, and babies' for now, are random, drawn from the world's seed.

## Planned

- [planned] Skills passed on from the parents, at birth and for the settlers' grown children
  ([[Lifelines]]).
- [planned] Skills growing with the work, towards the house's trade, and callings.

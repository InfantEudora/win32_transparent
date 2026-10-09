---
title: Skills
aliases: skill, strength, precision, husbandry, ingenuity, pace
kind: people
status: partial
summary: Four base skills from 1 to 10. A job's skill decides who gets the job and how fast they work it.
plans: gameplay_plan.md > People; lifeline_plan.md > Skills passed on
code: Economy.h: SKILL_STRENGTH EconomySkillFactor EconomyJobSkill; Economy.cpp: AssignJobs
verified: 2026-10-09
---

| Skill | Used today by | Proposed for later |
|---|---|---|
| Strength | [[Woodcutter|woodcutters]], [[Construction|builders]], anyone clearing a road | carriers |
| Precision | | archers, toolmakers |
| Husbandry | [[Field|farmers]] | herders, cooks |
| Ingenuity | [[Water collector|water carriers]] | boilers, machines |

- **Who gets a job**: an empty place goes to the free grown-up with the best score, which is their skill
  for the job (each point worth {ECONOMY_SKILL_DISTANCE} world units) minus the walk from home
  ([[Person|jobs]]).
- **Pace**: the job's skill sets how fast its work goes (felling, sowing, building, laying a road, and how
  fast a field grows): average at 5, faster above, slower below (`EconomySkillFactor`). Everyone walks at
  the same speed, and a water collector fills at the same rate whoever carries for it.
- Settlers' skills, and babies' for now, are random, drawn from the world's seed.

## Planned

- [planned] Skills passed on from the parents, at birth and for the settlers' grown children
  ([[Lifelines]]).
- [planned] Skills growing with the work, towards the house's trade, and callings.

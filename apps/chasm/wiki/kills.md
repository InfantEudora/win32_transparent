---
title: Kills
aliases: kill, kill count, rank, ranks, Expert, Experts, Novice, Master
kind: people
status: partial
summary: The zombies a person killed, by bow and by hand - whoever's hit took the last of its health - and the rank they make: Novice, Expert, Master, never lost.
plans: ranks_plan.md > Decided; ranks_plan.md > Proposed; ranks_plan.md > As built: step 1, kills (2026-10-19); ranks_plan.md > As built: step 2, ranks (2026-10-19)
code: Economy.h: KILL_BOW KILL_HAND EconomyKills EconomyWorker EconomyDeath EVENT_ZOMBIE_FELLED EVENT_RANKED ZOMBIE_HEALTH ARROW_HIT RANK_NOVICE RANK_EXPERT RANK_MASTER EconomyArrowHarm; Economy.cpp: EconomyRank EconomyKillsTold AddPerson; Zombies.cpp: ZombieKilled Fights; Shooting.cpp: Shoot EconomyShotTicks; ChasmStats.cpp: ChasmStatsCheck
verified: 2026-10-19
---

Every [[Zombie|zombie]] killed is someone's **kill**: whoever's hit took the last of its {ZOMBIE_HEALTH} hit points. No
shares and no assists - several archers often shoot at one, an arrow taking {ARROW_HIT}, and the kill is the archer
whose arrow lands last.

- **By bow**: the arrow that lands as the last hit. One still in the air when its archer dies counts too, on his
  record among the dead.
- **By hand**: the blow it falls by - of several on one tick, the last struck, the one it falls away from
  ([[The fallen]]).
- Everyone keeps **both counts**, by bow and by hand, and keeps them when they die. They only ever go up.

## Ranks

A **rank** comes from all the kills together, bow and hand - worked out from them each time, never kept, so never lost:

| Rank | Kills | Pace in a fight | An arrow's hit kills outright |
|---|---|---|---|
| Novice | {rank_kills[RANK_NOVICE]} | x{rank_pace[RANK_NOVICE]} | {rank_one_shot[RANK_NOVICE]}% |
| Expert | {rank_kills[RANK_EXPERT]} | x{rank_pace[RANK_EXPERT]} | {rank_one_shot[RANK_EXPERT]}% |
| Master | {rank_kills[RANK_MASTER]} | x{rank_pace[RANK_MASTER]} | {rank_one_shot[RANK_MASTER]}% |

- **Pace**: a shot's turn is shorter by it, and blows by hand come sooner - no harder.
- **Killing outright** is decided as the arrow leaves the bow, for a hit only. Nobody shoots again at a zombie such an
  arrow is on its way to.
- **Rising** a rank is an [[Events|event]]: in the journal, on the notice line, with the chime. The dead don't rise,
  though a kill landing after them still counts on their record.

## On screen

- **[[Person|A person's card]]**: the rank first by the name ("Expert, age 24 - Guard"); under archery, the kills
  and what the next rank takes - "Kills 2: 1 by bow, 1 by hand - Master at 3", or "Kills 2, by bow - ...".
- **A death**: the pop-up and the red line give the rank and the kills - "Jelle De Wit, Expert, 2 kills, of
  thirst"; the journal, the rank only - "Expert Jelle De Wit died of thirst".
- **The [[Statistics|statistics]]** count the zombies killed each day, and the page shows their total once there
  has been one. Its check: the zombies killed, and the kills people were put down with, are everyone's kills.
- A kill is an event too, counted, with no sound of its own: the arrow's hit or the punch is heard.
- **Debug**: `add_person`'s `kills`, and a kills slider by the People panel's "Put an archer down".

## Planned

- [idea] Perks by rank: choosing the most dangerous target, nerve when the alarm comes, steadying those near.
- [planned] An old Expert teaching at the [[Archery range|range]] once the old stop work ([[Lifelines]]).

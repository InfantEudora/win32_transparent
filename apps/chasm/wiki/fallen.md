---
title: The fallen
aliases: fallen, dead, the dead, death, body, bodies, corpse, corpses, angel, carrion
kind: systems
status: built
summary: The dead lie where they fell, by the blow that killed them - a zombie for days, then sinking into the ground; a person for seconds, then sinking, and an angel rises.
plans: zombies_plan.md > Step 6: the fallen (the user's, 2026-10-19); zombies_plan.md > As built: step 6 (2026-10-19)
code: Economy.h: EconomyFallen EconomyFallenSinking EconomyFallenRising EconomyZombieLooks EconomyWorkerLooks FALLEN_ZOMBIE_DAYS FALLEN_ZOMBIE_SINK_DAYS FALLEN_PERSON_LIE_SECONDS FALLEN_PERSON_SINK_SECONDS FALLEN_PERSON_ANGEL_SECONDS; Zombies.cpp: ZombieFalls PersonFalls TickFallen; ApplicationChasmWorkersView.cpp: UploadFallen; Birds.h: BirdCarrion BIRD_CARRION_TAKE BIRD_CARRION_REACH
verified: 2026-10-19
---

A [[Zombie|zombie]] or a [[Person|person]] who dies doesn't vanish: he falls, by the blow that killed him, and lies
there a while.

## Which way

**Struck from the front, he falls backward; from behind, forward**, and he keeps facing the way he faced. The blow is
an arrow's flight, a person's blow from where he stands, or a zombie's bite from where it stands. Whoever dies of
hunger, thirst or cold has no blow and falls forward. A zombie stood by whom it is after faces him; a person faces
the zombie he shoots at or fights by hand. So most zombies fall back from their killers, and one shot in the back from
a [[Guard tower|tower]] it has walked past falls on its face.

## How long

- **A zombie** lies {FALLEN_ZOMBIE_DAYS:#days}: still at first, sinking into the ground over the last
  {FALLEN_ZOMBIE_SINK_DAYS:#days}, and then it is gone.
- **A person** lies {FALLEN_PERSON_LIE_SECONDS:#seconds} from his fall, sinks over {FALLEN_PERSON_SINK_SECONDS:#seconds},
  and an **angel** appears where he sank and rises for {FALLEN_PERSON_ANGEL_SECONDS:#seconds}. Someone who dies at home
  leaves no body: his angel rises from over his roof. A child's figure has no rig, so it tips over whole.

The dead are kept in the save, so a body lies on after a load. Nothing else about them is a rule yet: nobody buries
them, and they stand in nobody's way.

## Crows

A zombie's body lying still is carrion for the [[Birds|crows]]. A flock moving on takes the nearest within
{BIRD_CARRION_REACH} units over the trees {= BIRD_CARRION_TAKE * 100}% of the time: it comes down on the ground round
the body, a few on it, and pecks. Anyone coming near puts it up, as from a tree, and once the body sinks it moves on.
The wanderers never come down on one.

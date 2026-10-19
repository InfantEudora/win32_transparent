---
title: Zombie
aliases: zombies, enemy, enemies, the threat
kind: systems
status: partial
summary: The colony's enemy. For now dropped by debug tools only, a zombie walks after the nearest person it can get to and bites him; a grown-up fights back, and archers and guards shoot it.
plans: zombies_plan.md > Decided; zombies_plan.md > Why it is won by placement; zombies_plan.md > The first slice; zombies_plan.md > Step 1: a zombie, dropped by debug; zombies_plan.md > Step 1b: spacing out; zombies_plan.md > Step 2: meals, and a fight; zombies_plan.md > Step 3: guards shoot; zombies_plan.md > Step 3b: guards hold their posts
code: Spacing.cpp: SpaceOut; Economy.h: EconomyZombie EconomyFightPace ZOMBIE_LOOK_TICKS ZOMBIE_PACE ZOMBIE_REACH ZOMBIE_REPLAN ZOMBIE_TRIES ZOMBIE_LOST_LOOKS ZOMBIE_SPAWN_MOST ZOMBIE_HEALTH ZOMBIE_BITE FIGHT_REACH FIGHT_HIT; Zombies.cpp: SpawnZombies TickZombies ZombieLook ZombieRouteTo Fights; Shooting.cpp: Shoot ShotTarget ZombiesInRange DrinkBetweenShots; Economy.h: EconomyArrow ARROW_SHOT_SECONDS ARROW_HIT; Walkers.h: WALK_WHO_ZOMBIE; ApplicationChasmZombies.cpp: UploadZombies RenderZombiePanel
verified: 2026-10-18
---

A zombie goes for **people only**. Walls, gates and buildings are only what stands in its way. There are no
waves yet: zombies come only by the debug tools, in debug builds - **Z** drops one on the plot under the cursor,
the panel's **Zombies** section a number on the selected plot (or clears them all), and the `chasm_zombie` tool.
Each is a recorded command. At most {ZOMBIE_SPAWN_MOST} at once; refused where a [[Posts|post]] would be.

## Whom it goes for

The **nearest person out of doors and on the ground**, by the straight line: never someone indoors, a baby in
its mother's arms, or a [[Guard tower|guard]] up on his tower's platform. The nearest {ZOMBIE_TRIES} are
tried until one can be walked to. It looks again every {ZOMBIE_LOOK_TICKS:seconds}, each on a tick of its own,
so it switches to whoever is nearest as people move. With nobody it can get to, it stands, and looks only one
time in {ZOMBIE_LOST_LOOKS}. Anything built or taken down sends every one after someone looking again at once.

## How it walks

As a [[Person|person]] does: straight where the line is clear, otherwise along the grid by the villagers'
rules - so a standing [[Roads and walls|wall]] or a shut gate stops it - but never up onto a deck. At
{ZOMBIE_PACE} of a person's speed, so anyone outwalks one. It stops {ZOMBIE_REACH} short of him, and goes after
him again once he is {ZOMBIE_REPLAN} further off. Zombies keep apart as people do, so several round one person
stand in a ring; a zombie is pushed out of a person's room, {SPACING_BODY}, never the other way.

## The fight

Within {FIGHT_REACH} of whom it is after, it **bites** him every {ZOMBIE_BITE_SECONDS:#seconds}, for
{ZOMBIE_BITE:days} of [[Health|health]] - and a grown-up within that reach of a zombie **hits back** at the
nearest every {FIGHT_SWING_SECONDS:#seconds}, for {FIGHT_HIT} of its {ZOMBIE_HEALTH} hit points: both by his
strength and ingenuity together (`EconomyFightPace`), harder and more often for a strong, clever one. Each side's
first blow comes a whole interval after it is in reach; a zombie at 0 falls and is gone. A child is bitten and
does not fight. No meal begins, nor goes on, with a zombie near, so nothing eaten heals a fight. An average person
at full health kills one in about 3 seconds; hungry, or against two, he dies first. Nobody flees or stops work yet.

## Shot at

Anyone grown with a bow - an [[Archery range|archer]] or a [[Guard tower|guard]] - who **stands**, on a
[[Posts|post]], a tower's platform or the range's line, shoots the **nearest zombie they can see**: over the ground
between, as far as [[Exploration|sight]] reaches, so further from a tower. Walls and trees do not stop it. A shot is a
turn of the Shoot clip, {ARROW_SHOT_SECONDS:#seconds}; a trainee's stint and a meal wait meanwhile, a drink is taken between turns. The arrow flies at
{ARROW_SPEED} units a second and hits or misses by a scatter that widens with the distance and narrows with archery,
{ARROW_SPREAD_NOVICE} to {ARROW_SPREAD_MASTER} a unit; a hit takes {ARROW_HIT}. None goes at a zombie the arrows in
the air will fell. With one within {FIGHT_REACH}, they fight it by hand instead. A trained archer usually drops one
walking at them from 20 units off; an untrained one wounds it. Arrows never run out, for now. While one is in range,
someone [[Posts|posted]] stays on his post however thirsty, and a [[Guard tower|guard]] keeps his tower until released.

## On screen

The props file's `zombie` figure at the people's scale, rigged as they are and turned along its walk: the man's walk
while it walks, at its pace so its feet are planted, and his idle standing by whom it is after, toward him - biting
too, which has no clip of its own yet. Not drawn under the clouds. A person's card says "Fighting a zombie" or
"Shooting at a zombie". Arrows fly from the bow, stay stuck in a zombie until it falls, and lie where they missed for {ARROW_LIE_SECONDS:#seconds}.

## Planned

- [proposed] Arrows as a tower's stock, kept full by the builders; the ballista.
- [planned] **Walls break** (step 4): walls and gates cost a zombie time to break rather than stopping it.
- [proposed] **Waves from the swamp**, none while it is frozen; people fleeing and the alarm; the swamp's haze
  thickening before a wave; fields trampled; winter raids on the frozen swamp.
- [proposed] A zombie's own clips: a bite, a shamble.

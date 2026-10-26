---
title: Zombie
aliases: zombies, enemy, enemies, the threat
kind: systems
status: partial
summary: The colony's enemy. For now dropped by debug tools only, a zombie walks after the nearest person it can get to and bites him; a grown-up fights back, and archers and guards shoot it.
plans: zombies_plan.md > Decided; zombies_plan.md > Why it is won by placement; zombies_plan.md > The first slice; zombies_plan.md > Step 1: a zombie, dropped by debug; zombies_plan.md > Step 1b: spacing out; zombies_plan.md > Step 1c: a crowd keeps its room; zombies_plan.md > As built: step 1c, a crowd keeps its room; zombies_plan.md > Step 2: meals, and a fight; zombies_plan.md > Step 3: guards shoot; zombies_plan.md > Step 3b: guards hold their posts; zombies_plan.md > Step 4: walls break; zombies_plan.md > As built: step 4b; zombies_plan.md > As built: step 4d, the look
code: Spacing.cpp: SpaceOut; Zombies.cpp: ZombiePlaces; Economy.h: EconomyZombieHitting; Economy.h: EconomyZombie EconomyFightPace EconomyCarriesBow ARROW_SHOT_MASTER ARROW_SHOT_JITTER ZOMBIE_LOOK_TICKS ZOMBIE_PACE ZOMBIE_REACH ZOMBIE_REPLAN ZOMBIE_TRIES ZOMBIE_LOST_LOOKS ZOMBIE_SPAWN_MOST ZOMBIE_HEALTH ZOMBIE_BITE FIGHT_REACH FIGHT_HIT; Zombies.cpp: SpawnZombies TickZombies ZombieLook ZombieRouteTo Fights ZombiesHit ForgetHarm CanGetAt; Economy.h: ZOMBIE_BREAK_WALL ZOMBIE_BREAK_GATE ZOMBIE_BREACH_DOOR ZOMBIE_CLIMB_SECONDS ZOMBIE_HITTERS EconomyHarm; Walkers.h: WalkBreaks; Shooting.cpp: Shoot ShotTarget EconomyShotTicks ZombiesInRange DrinkBetweenShots; Economy.h: EconomyArrow ARROW_SHOT_SECONDS ARROW_HIT; Walkers.h: WALK_WHO_ZOMBIE; ApplicationChasmZombies.cpp: UploadZombies RenderZombiePanel; ApplicationChasmSound.cpp: SignalZombies SignalZombieVoices; Zones.h: ZoneKindHasDoor
verified: 2026-10-25
---

A zombie goes for **people only**: walls, gates and houses are only what stands in its way. No waves yet - zombies
come by the debug tools, in debug builds: **Z** drops one under the cursor, the panel's **Zombies** section a number on
the selected plot (or clears them all), `chasm_zombie` too; recorded, at most {ZOMBIE_SPAWN_MOST} at once, never on a wall.

## Whom it goes for

The **nearest person out of doors and on the ground** by the straight line, or indoors in a breached house - never a
baby in its mother's arms or a [[Guard tower|guard]] on his tower - and a wave **shares itself out** over the people
near ([[Crowds]]). The best {ZOMBIE_TRIES} are tried until one can be walked to, every {ZOMBIE_LOOK_TICKS:seconds}; with
nobody to get to it stands, looking one time in {ZOMBIE_LOST_LOOKS}. Anything built, taken down or broken sends all looking again.

## How it walks

As a [[Person|person]] does: straight where the line is clear, otherwise along the grid by the villagers' rules,
never up onto a deck - and what stands in its way it **breaks**, where that is quicker than going round: a standing
[[Roads and walls|palisade]] plot in {ZOMBIE_BREAK_WALL:#seconds} of hitting, a shut gate's door in
{ZOMBIE_BREAK_GATE:#seconds} (the weak point), any building's door in {ZOMBIE_BREACH_DOOR:#seconds}, then walked
through; at most {ZOMBIE_HITTERS} hit one thing at once. A garden's wall or a lot's fence it climbs, in
{ZOMBIE_CLIMB_SECONDS:#seconds}. At {ZOMBIE_PACE} of a person's speed, so anyone outwalks one. Round whom it is after
each has a **place of its own** - the first ring {ZOMBIE_REACH} out, where it bites from, the rest waiting beyond - and
at what it breaks only the front row hits ([[Crowds]]); it goes after him again once he is {ZOMBIE_REPLAN} further off.

## The fight

Within {FIGHT_REACH} of whom it is after, it **bites** him every {ZOMBIE_BITE_SECONDS:#seconds}, for
{ZOMBIE_BITE:days} of [[Health|health]] - and a grown-up within that reach of a zombie **hits back** at the
nearest every {FIGHT_SWING_SECONDS:#seconds}, for {FIGHT_HIT} of its {ZOMBIE_HEALTH} hit points: both by his
strength and ingenuity together (`EconomyFightPace`), harder and more often for a strong, clever one. Each side's
first blow comes a whole interval after it is in reach; a zombie at 0 falls, by that blow - its striker's [[Kills|kill]], as the last arrow is its archer's - and lies a few days ([[The fallen]]). A child is bitten and
does not fight. No meal begins, nor goes on, with a zombie near, so nothing eaten heals a fight. An average person
at full health kills one in about 3 seconds; hungry, or against two, he dies first. Nobody flees or stops work yet.

## Shot at

Anyone grown with a bow - anyone trained at the [[Archery range|range]], whatever their work, and every
[[Guard tower|guard]] (`EconomyCarriesBow`) - who **stands**, on a [[Posts|post]], a tower's platform or the range's line, shoots the **nearest zombie they can see**: over the ground
between, as far as [[Exploration|sight]] reaches, so further from a tower. Walls and trees do not stop it. A shot is a
turn of the Shoot clip, {ARROW_SHOT_SECONDS:#seconds} with no archery down to {ARROW_SHOT_MASTER:#seconds} with all of it, each turn up to {= ARROW_SHOT_JITTER * 100}% longer or shorter (`EconomyShotTicks`); a trainee's stint and a meal wait meanwhile, a drink is taken between turns. The arrow flies at
{ARROW_SPEED} units a second and hits or misses by a scatter that widens with the distance and narrows with archery,
{ARROW_SPREAD_NOVICE} to {ARROW_SPREAD_MASTER} a unit; a hit takes {ARROW_HIT}. None goes at a zombie the arrows in
the air will fell. With one within {FIGHT_REACH}, they fight it by hand instead. A trained archer usually drops one
walking at them from 20 units off; an untrained one wounds it. Arrows never run out, for now. While one is in range,
someone [[Posts|posted]] stays on his post however thirsty, and a [[Guard tower|guard]] keeps his tower until released.

## On screen

The props file's `zombie` at the people's scale, rigged as they are: the man's walk at its pace, feet planted, his idle
standing, and its own scratch when it bites or hits what it breaks, facing it - a blunt strike heard on its swipe, and a
punch as each bite and blow lands, and the one bitten crying out; in sight, a murmur now and then from the nearest few, a short noise when one is put down, turns on someone new or is hit, and a cry as it falls; what it breaks gives as it goes ([[Roads and walls|walls]], [[House|doors]]). Not drawn - or heard - under the clouds. A card says
"Fighting a zombie" or "Shooting at a zombie". Arrows stay stuck in a zombie until it falls; misses lie {ARROW_LIE_SECONDS:#seconds}.

## Planned

- [planned] Repairs by the builders with no zombies about; whom to go for by the way there (steps 4c, 4e).
- [proposed] **Waves from the [[Swamp hut|swamp huts]]**, the first a year in, none while it is frozen; people fleeing
  and the alarm; the swamp's haze before a wave; fields trampled; winter raids on the frozen swamp.
- [proposed] Arrows as a tower's stock, kept full by the builders; the ballista; a shamble of its own.

---
title: Crowds
aliases: crowd, keeping apart, spacing, following, queue, queueing, places, attack slots
kind: systems
status: built
summary: How people and zombies keep out of one another - pushed apart, stepping round, queueing behind one ahead - and each zombie at a place of its own round whom it is after.
plans: zombies_plan.md > Step 1b: spacing out; zombies_plan.md > As built: step 1b, spacing out; zombies_plan.md > Step 1c: a crowd keeps its room; zombies_plan.md > As built: step 1c, a crowd keeps its room
code: Spacing.cpp: SpaceOut Lean Follow; Zombies.cpp: ZombiePlaces ZombieLook EndAt; Economy.h: EconomyBody SPACING_RADIUS SPACING_MOST SPACING_AHEAD SPACING_BODY SPACING_FOLLOW SPACING_OVERTAKE ZOMBIE_CROWDED ZOMBIE_KEEP_TARGET ZOMBIE_SLOT_GAP ZOMBIE_RING_COST ZOMBIE_HITTERS
verified: 2026-10-20
---

Out of doors, everyone on the ground keeps apart from **their own kind** - people from people, zombies from zombies,
so a zombie is never pushed off whom it reaches for. Someone's route, and where he is counted, never move: only where
his body stands does, at most {SPACING_MOST} off his route, and never onto water, a wall, a building or a field.
Indoors, carried, or up on a deck, nobody takes part.

## Keeping apart

- Within {SPACING_RADIUS} of another, a body is pushed away from him, the harder the nearer, and drifts back onto its
  route once nobody is near.
- Walking, he only steps **aside**, so a group on one path walks side by side. Meeting someone, or someone standing in
  his way, within {SPACING_AHEAD} ahead, he keeps to his **right**, as traffic does.
- A [[Zombie|zombie]] is kept out of a [[Person|person's]] own room, {SPACING_BODY}, never the other way.

## Queueing

- Walking behind one of his kind going his way, within {SPACING_FOLLOW}, he **slows** and follows instead of walking
  into him - all but stopped right behind him - so a crowd on one path strings out. His walk slows with him.
- Someone slower than {= SPACING_OVERTAKE * 100}% of his pace, a carrier under a heavy load, he steps round and passes.
- Nobody waits behind anyone standing or coming at him: he steps round, a little slower while he passes.

## Zombies round whom they are after

- **A wave shares itself out**: to a zombie, every zombie after someone and nearer him counts as {ZOMBIE_CROWDED}
  units further to walk, so the ones behind go for the next person. Its own target counts {ZOMBIE_KEEP_TARGET} nearer.
- **A place of its own**: rings of places round him, {ZOMBIE_SLOT_GAP} apart. The first ring, {ZOMBIE_REACH} out, is
  where they bite from; the rest stand in the rings beyond, out of reach, waiting their turn.
- **At what it breaks** - a [[Roads and walls|palisade]], a gate, a door - the places are rows across the way they
  came, {ZOMBIE_HITTERS} in the front row and one more each row back. Only the front row hits.
- Places are handed out every tick, the nearest first, an inner ring before an outer (as if {ZOMBIE_RING_COST} units
  nearer), the one it holds kept: as one falls, the nearest of the next ring steps in.
- A place counts only where it could walk from him, on open ground, never on someone else or another's place. With
  none to be had, a zombie stops short of him as before.

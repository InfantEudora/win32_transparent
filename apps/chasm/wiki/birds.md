---
title: Birds
aliases: bird, flock, flocks, crows
kind: world
status: partial
summary: Black flocks that perch in the trees and fly up when someone walks near or a tree is felled. Only a look - they change nothing.
plans: birds_plan.md > The one rule; birds_plan.md > Step 2 - flushed; birds_plan.md > As built
code: Birds.h: BirdSky BIRD_KEY_UP BIRD_KEY_DOWN BIRD_KEY_FOLDED; Birds.cpp: StepFlock Scare NextRoost; ApplicationChasmBirds.cpp: UploadBirds BirdScares HomeBirds PerchPoints
verified: 2026-10-17
---

Flocks of {BIRD_FLOCK_MIN} to {BIRD_FLOCK_MAX} black birds live in the forests. They are **view, not
state**: never saved, never in a replay's trace, and nothing in the game reads them. A load starts them
again from their homes.

## Where they live

{BIRD_FLOCKS} flocks to a map. The first {BIRD_COLONY_FLOCKS} are homed in the woods round the colony,
{BIRD_COLONY_MIN} to {BIRD_COLONY_MAX} from the middle of where its people are. The rest are homed in the
forests' dense cores, at least {BIRD_HOME_SPACING} apart.

A flock sits in a [[Tree|tree]] and its neighbours, a bird to a perch at the top of the crown. A felled
tree or one built over is no perch. Every so often, after {BIRD_RESTLESS_MIN_S} to
{BIRD_RESTLESS_MAX_S} seconds of game time, it moves on to other trees within {BIRD_ROAM} of its home.

**The colony's flocks follow the axe.** Moving on, they settle within {BIRD_LURE_REACH} of a tree a
[[Woodcutter|woodcutter]] has felled at lately, if there is one, and that becomes their home. If their
woods are all felled, they find a new home in the nearest trees left.

**Wanderers** are mostly in the air: {BIRD_WANDERERS} of them, one bird or two or three, {BIRD_WANDER_COLONY}
homed round the colony. They sit for only {BIRD_WANDER_SIT_MIN_S} to {BIRD_WANDER_SIT_MAX_S} seconds, then fly
to trees at least {BIRD_WANDER_AWAY} away, circle longer, and as often fly on as come down.

## What puts them up

| | Reach |
|---|---|
| anyone out of doors who moved since the last tick | {BIRD_REACH_WALK} |
| a woodcutter chopping at a tree, or clearing one for a [[Road|road]] | {BIRD_REACH_CHOP} |
| a tree coming down | {BIRD_REACH_FELLED} |

The nearest bird goes first and the rest follow within {BIRD_STAGGER_S} seconds. The flock climbs away
from the scare together and regroups after {BIRD_SCATTER_MIN_S} to {BIRD_SCATTER_MAX_S} seconds.
It then flies to trees clear of anything scaring birds, and stays out of {BIRD_WARY_REACH} times the
scare's reach for {BIRD_WARY_S} seconds.

## Their clock

The game's: a pause holds them in the air, and they fly faster at the faster play speeds.

## Planned

- [planned] Their calls and wing bursts, on the cue table (birds_plan.md step 3).
- [planned] Not drawn over unexplored ground; larger flocks in autumn and winter (step 4).
- [planned] A flock circling the title scene (step 5).
- [idea] Crows rising at the forest edge where [[Zombie|zombies]] walk; ravens below the chasm's rim; crows on
  sown fields; magpies.

---
title: Birds
aliases: bird, flock, flocks, crows
kind: world
status: built
summary: Black flocks that perch in the trees and fly up when someone walks near or a tree is felled. Only a look - they change nothing.
plans: birds_plan.md > The one rule; birds_plan.md > Step 2 - flushed; birds_plan.md > Step 3 - the sounds; birds_plan.md > Step 4 - a few flocks across the map; birds_plan.md > Step 5 - a flock over the title scene; birds_plan.md > As built; test_maps_plan.md > Step 6: the title's diorama, made by code
code: Birds.h: BirdSky BIRD_KEY_UP BIRD_KEY_DOWN BIRD_KEY_FOLDED BIRD_VOICE_MAGPIE; Birds.cpp: StepFlock Scare NextRoost CallGap Gather; ApplicationChasmBirds.cpp: UploadBirds BirdScares HomeBirds PerchPoints BirdGather UploadTitleBirds BirdDioramaMarkers; ApplicationChasmSound.cpp: SignalBirds
verified: 2026-10-18
---

Flocks of {BIRD_FLOCK_MIN} to {BIRD_FLOCK_MAX} black birds live in the forests. They are **view, not
state**: never saved, never in a replay's trace, and nothing in the game reads them. A load starts them
again from their homes. They keep the game's clock: a pause holds them in the air, and they fly faster at
the faster play speeds.

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

**They are seen and heard over unexplored ground too**, on purpose: a flock over the clouds is a hint that there
are trees under them.

**Wanderers** are mostly in the air: {BIRD_WANDERERS} of them, one bird or two or three, {BIRD_WANDER_COLONY}
homed round the colony. They sit for only {BIRD_WANDER_SIT_MIN_S} to {BIRD_WANDER_SIT_MAX_S} seconds, then fly
to trees at least {BIRD_WANDER_AWAY} away, circle longer, and as often fly on as come down.

**In autumn and winter the flocks gather** to {BIRD_GATHER_AUTUMN} and {BIRD_GATHER_WINTER} times their own size
(not the wanderers). Birds join only while a flock is in the air, a few at a time from off its edge. In spring
they leave one at a time the same way, until the flock is back to its own size.

## What puts them up

| | Reach |
|---|---|
| anyone out of doors who moved since the last tick | {BIRD_REACH_WALK} |
| a woodcutter chopping at a tree, or clearing one for a [[Road|road]] | {BIRD_REACH_CHOP} |
| a tree coming down | {BIRD_REACH_FELLED} |

The nearest bird goes first and the rest follow within {BIRD_STAGGER_S} seconds. They climb away together,
regroup after {BIRD_SCATTER_MIN_S} to {BIRD_SCATTER_MAX_S} seconds and fly to trees clear of anything scaring
birds, keeping out of {BIRD_WARY_REACH} times the scare's reach for {BIRD_WARY_S} seconds.

## What they sound like

The flocks caw as crows and the wanderers chatter as magpies, each flock on its own clock:
{BIRD_CALL_SETTLED_MIN_S} to {BIRD_CALL_SETTLED_MAX_S} seconds apart sitting, {BIRD_CALL_FLYING_MIN_S} to
{BIRD_CALL_FLYING_MAX_S} flying (wanderers keep the sitting rate). Scared off its perches, a flock goes up
with a burst of wings. The first bird calls at once, and another answers {BIRD_CALL_ANSWER_MIN_S} to
{BIRD_CALL_ANSWER_MAX_S} seconds later. Calls are heard within {SOUND_BIRD_REACH} of the view, panned, and not
in a pause. The cue table (`bird_call`, `birds_flush`) says what each one sounds like.

## On the title

One flock of {BIRD_TITLE_FLOCK} flies over the title's town, silent, on the title's own clock, and perches in the
town's trees: the town is made by code at start, its trees' perches with it (test_maps_plan.md). A diorama loaded
from a file without perches has the flock circle and pass over instead. The town's walkers put it up.

## Planned

- [idea] Crows rising at the forest edge where [[Zombie|zombies]] walk; ravens below the chasm's rim; crows on
  sown fields; magpies.

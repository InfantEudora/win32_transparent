---
title: Founding
aliases: found, take a tent, taking a tent, pitch, pitching, expedition, outpost
kind: world
status: partial
summary: People sent out take a tent from a camp and pitch it where they stand. It becomes their home, and outside every settlement it founds a new one.
plans: settlements_plan.md > Founding: decided; settlements_plan.md > Founding: as built, step 4; settlements_plan.md > Founding: as built, steps 2, 3, 5 and 6; settlements_plan.md > Founding: steps
code: Economy.h: ECONOMY_PITCH_RINGS ECONOMY_TENT_CARRIED NOTICE_NO_TENT; Economy.cpp: TakeTent TentToPack GoForTent PitchSpot Pitched SettleHere CampSpare OwnHousehold HouseFamilies; Zones.h: PitchTent
verified: 2026-10-11
---

A new [[Settlement|settlement]] is founded by taking a tent out of a [[Camp|camp]] and pitching it somewhere
outside every settlement.

## Taking a tent

Send people out with an order ([[Posts|posts]]) and press **Take a tent** on their card - "Take a tent each" for a
group. On their way, before any gathering, they go to the nearest camp with a tent to spare and pack one, and that
tent comes out of the camp. A tent somebody sleeps in isn't spare, but each person's own tent always is. They fill
their pack there as on a gathering stop, and carry the tent rolled across their back.

With no tent to spare anywhere they can get to, they go on without one, and the player is told: "NO TENT TO SPARE
FOR NAME TO TAKE".

## Pitching it

Anyone carrying a tent - posted or not - has **Pitch the tent here** on their card. The tent goes up at once on the
plot they stand on, or the nearest one within {ECONOMY_PITCH_RINGS} plots that a camp may take: nothing built on it,
no [[Crate|crate]], no standing [[Tree|tree]], on their own terrace step. Beside another pitched tent, it joins that
camp, so a group pitching together makes one camp. Outside every settlement it founds a new one; inside one, it
adds a bed there.

## Moving in

Pitching makes the tent their home. Until then they keep their place in their old home, so an expedition turned
back walks home to its bed. Pitching gives it up: they become a household of their own, as a new couple does,
their order is over, and they're offered an empty [[House|house]] in their new settlement, as anyone in a camp is.

Each camp employs its own [[Woodcutter|woodcutter]], so three tents pitched apart are three camps and three
woodcutters; pitched together, they're one camp with one.

A tent can be packed up again by whoever sleeps in it, and the move repeated. A settlement of nothing but tents is
gone once they are all packed, and what its camp held is left there as a [[Crate|crate]].

## Settling without a tent

Someone posted onto another settlement's ground, carrying no tent, has **Settle here** on their card from the
moment they're inside its borders. They move in as when pitching, into a camp there with a tent to spare or an
empty house; with neither, they squeeze into the home there with the most room - belonging to that settlement
without a bed of their own, until a house with room comes free.

## Planned

- [planned] Foraging: food from the wild, slowly.

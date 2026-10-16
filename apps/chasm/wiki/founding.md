---
title: Founding
aliases: found, take a tent, taking a tent, pitch, pitching, expedition, outpost
kind: world
status: partial
summary: People sent out take a tent from a camp and pitch it where they stand. It becomes their home, and outside every settlement it founds a new one.
plans: settlements_plan.md > Founding: decided; settlements_plan.md > Founding: as built, step 4; settlements_plan.md > Founding: as built, steps 2, 3, 5 and 6; settlements_plan.md > Founding: steps
code: Economy.h: ECONOMY_PITCH_RINGS ECONOMY_TENT_CARRIED EVENT_NO_TENT; Economy.cpp: TakeTent TentToPack GoForTent PitchSpot Pitched SettleHere CampSpare OwnHousehold PartnerLivingIn GoesWith WithTheGrown BringAlong HouseFamilies; Zones.h: PitchTent
verified: 2026-10-14
---

A new [[Settlement|settlement]] is founded by taking a tent out of a [[Camp|camp]] and pitching it somewhere
outside every settlement.

## Taking a tent

Send people out with an order ([[Posts|posts]]) and press **Take a tent** on their card - "Take a tent each" for a
group. On their way, before any gathering, they go to the nearest camp with a tent to spare and pack the tent nearest
them, and that tent comes out of the camp. From another camp, a tent somebody sleeps in isn't spare; their own camp's tents its
people may always take, however crowded it is. They fill their pack there as on a gathering stop, and carry the
tent rolled across their back.

With no tent to spare anywhere they can get to, they go on without one, and the player is told: "NO TENT TO SPARE
FOR NAME TO TAKE".

## Pitching it

Anyone carrying a tent - posted or not - has **Pitch the tent here** on their card. The tent goes up at once on
the plot they stand on, or the nearest within {ECONOMY_PITCH_RINGS} plots a camp may take: never on anything built, a
[[Crate|crate]] or a standing [[Tree|tree]], and on their own terrace step. Outside every settlement the first tent
founds a new one with a camp of its own; every tent after it, anywhere in that settlement, joins that camp, so a
settlement has one camp however its tents fall.

## Moving in

Pitching makes the tent their home. Until then they keep their place in their old home, so an expedition turned
back walks home to its bed. Pitching gives it up: they become a household of their own, as a new couple does,
their order is over, and they're offered an empty [[House|house]] in their new settlement, as anyone in a camp is.
If their partner already lives in that settlement, they **rejoin** them instead: back in the partner's household and
home, whatever its room, and the pitched tent stands spare. Partners living apart have no children.

With one camp to a settlement, it has one [[Woodcutter|woodcutter]], one water carrier and one picker of the camp's,
however many tents. A woodcutter's hut built there takes the woodcutter's place over.

A tent can be packed up again by whoever sleeps in it, and the move repeated. A settlement of nothing but tents is
gone once they are all packed, and what its camp held is left there as a [[Crate|crate]].

## Children and babies

Children are never sent anywhere; they go with their household. When its last grown-up leaves - every grown-up of
it carrying or taking a tent, or posted onto another settlement's ground - the children go along beside their
mother (else their father, else the eldest), packing and gathering as the grown-ups do, with no tent of their own.
With one grown-up staying, they stay too. A baby always goes with its mother: carried, unseen, whenever she is
posted, until she is home again, and fed from her pack. Pitching or settling, they move in with whoever they came
with, and come home with them if that one turns back.

## Settling without a tent

Someone posted onto another settlement's ground, carrying no tent, has **Settle here** on their card from the
moment they're inside its borders. They move in as when pitching: with their partner if they live there, else
into a camp there with a tent to spare or an empty house; with none of those, they squeeze into the home there
with the most room - belonging to that settlement without a bed of their own, until a house with room comes free.

## Living off the land

Sent out, someone can pick a ripe plant or draw water by hand, before or without pitching a tent
([[Fruit|picking by hand]]); a camp they pitch has a picker of its own where plants bear.

---
title: Deposit
aliases: deposits, seam, seams, floatstone seam, floatstone seams, outcrop, outcrops, the mountain's stone
kind: world
status: built
summary: Ground holding a good to be dug - floatstone seams in the balconies' walls, the mountain, and outcrops in its pockets. Laid out with the map and drawn, and dug by mines.
plans: stone_plan.md > 4. Deposits; stone_plan.md > As built: step 4 (2026-10-14); stone_plan.md > As built: the third round (2026-10-15); stone_plan.md > As built: step 5 (2026-10-15)
code: Deposits.h: DEPOSIT_SEAM DEPOSIT_MOUNTAIN DEPOSIT_OUTCROP DEPOSIT_SEAM_CHANCE DEPOSIT_SEAM_PLOTS DEPOSIT_SEAM_FLOATSTONE DEPOSIT_OUTCROP_CHANCE DEPOSIT_OUTCROP_APART; Deposits.cpp: BuildDeposits; TerrainMesh.cpp: WallColumn; Forest.h: PROP_OUTCROP; Economy.cpp: PlaceRubble; ApplicationChasmExplore.cpp: LookForSeams; Economy.h: EVENT_SEAM_SEEN
verified: 2026-10-15
---

Ground that holds something to be dug out of it, by a [[Mine|mine]]. Deposits come with the map, from its seed, as
the [[Terrain|terrain]] and the [[Tree|forest]] do: a map always has the same ones, and they are never saved.

| Deposit | Where | Gives | Holds |
|---|---|---|---|
| **A seam** | along the foot of a [[Terrain|balcony's]] wall | [[Floatstone|floatstone]] | {DEPOSIT_SEAM_FLOATSTONE} |
| **The mountain** | the plots touching it, on the plateau and round its pockets' floors | [[Stone|stone]] | never runs out |
| **An outcrop** | on a pocket's floor | [[Stone|stone]] | never runs out |

## Seams

About {= DEPOSIT_SEAM_CHANCE * 100}% of the balconies off the mountain hold one, never more than one: a run of
{DEPOSIT_SEAM_PLOTS} plots along the foot of one wall. In that wall, the stratum just above the run is crystal - a
band of purple-blue facets - and on every plot of the run its crystal lies fallen, in piles of
[[Floatstone|floatstone]] the builders pick up like rubble. The rest of the seam waits for a mine.

A seam is **found by exploring**. The [[Exploration|clouds]] hide it until someone sees its balcony - from the rim
above is enough - and the first to see it is told at the bottom of the screen: "... has seen floatstone in a
balcony's wall". Each seam is told once; a load tells nothing again.

## The mountain and the outcrops

The mountain is one deposit: any plot at its foot is against it. Where the plateau meets it the ground rises gently;
round a pocket's floor the crags stand right behind.

Outcrops are beds of rock on a pocket's floor, about one plot in {= 1 / DEPOSIT_OUTCROP_CHANCE} before none may stand
within {DEPOSIT_OUTCROP_APART} of another. They stand where the pockets' rock clusters used to, and are not rocks: a
[[Stonecutter|stonecutter]] leaves them alone. A building over one hides it.

## Dug

A [[Mine|mine]] digs a deposit: what has been dug from each is saved. A seam dug out is rock again in its wall, and its
mines stand idle; the mountain and the outcrops never run out.

## Seeing them

`chasm_economy` op `deposits` lists every one: its kind, good, what it holds, its plots, how many have been explored,
what has been dug and is left, a seam's wall, band and crystal piles, and how far the mountain rises behind its foot.

## Planned

- [idea] Seams on the islands, once zeppelins reach them.
- [idea] Coal in the mountain's pockets.

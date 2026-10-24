---
title: Mine
aliases: mines, miner, miners, digging, mine portal
kind: buildings
status: built
summary: One plot on a deposit - a floatstone seam's foot, the mountain's foot, an outcrop - where a miner digs floatstone or stone a load at a time and carries it to a store. A seam is dug out at last; the mountain and the outcrops never are.
plans: stone_plan.md > 5. The mine; stone_plan.md > As built: step 5 (2026-10-15); crates_plan.md > As built: the ghost goes yellow over a crate (2026-10-24)
code: Zones.h: ZONE_KIND_MINE; Zones.cpp: MineIsBuildable; Economy.h: WORKER_JOB_MINER ECONOMY_MINE_SECONDS ECONOMY_MINE_STONE ECONOMY_MINE_FACE EconomyCrateInTheWay; Economy.cpp: GoDig DugOut EconomyDepositLeft; Deposits.cpp: DepositDigWay; ApplicationChasmMines.cpp: UploadMines MinePropsAt AppendMineProps
verified: 2026-10-24
---

A mine is one plot, one storey, placed with a click of its tool (**O**) - never dragged. It costs a storey's [[Wood|wood]] and
**{ECONOMY_MINE_STONE} [[Stone|stone]]**, and waits for that stone as any [[Construction|site]] does.

## Where it goes

Only **on a [[Deposit|deposit]]**, and with the [[Winch|winch]] the only building that stands against a cliff:

| On | Dug into | Gives |
|---|---|---|
| a floatstone seam's foot | the seam's wall | [[Floatstone|floatstone]], until the seam is dug out |
| the mountain's foot - the plateau's or a pocket's | the mountain's slope | stone, without end |
| an outcrop | the outcrop, as a pit | stone, without end |

The ground it digs into is left out; the rest round it must be its own level, dry, gentle, off the mountain and
with no field - and never where a winch lands. Elsewhere it is refused: "no seam in this wall" at the foot of a
bare wall, "a mine is dug into a seam's wall, the mountain or an outcrop" anywhere else.

**A seam's piles come first.** Every plot of a seam's foot has its fallen [[Floatstone|floatstone]] lying on it, and
in play nothing is built over a [[Crate|crate]]: the mine's ghost is yellow there, "FLOATSTONE LIES THERE: PICK IT UP
FIRST". PICK UP one plot's pile, and once it is carried off the mine goes on that plot. Its ghost is the plot's tile
with the portal and cart - or the crane - where they would stand.

## The miner

One place, a **miner**, by strength ([[Person|jobs]]), from home. While its deposit has some left and a
[[Store|store]] of his takes a load of what it gives whole, he walks to the mine, digs a load - {ECONOMY_MINE_SECONDS:#seconds}
a unit at strength 5, quicker when stronger - and carries it to that store, up a winch from a balcony as anyone
would. A store next to the mine takes only stone and floatstone. Its card can stop work.

## Dug out

A seam holds {DEPOSIT_SEAM_FLOATSTONE}. When the last of it is dug, every mine on it has no place: its miner goes
free for other work, NEEDS ATTENTION says **MINE: WORKED OUT**, and the seam's band in the wall is rock again.
The mine stands until erased. Its card shows what is left of a seam, or that the mountain's or an outcrop's stone
never runs out.

A site waiting for stone the colony cannot come by - more wanted than the stores and the rubble on its land hold -
shows in NEEDS ATTENTION as **NEEDS STONE**, pointing at the first link missing: a winch down to the rubble, a
[[Stonecutter|stonecutter's hut]], or rubble further out to pick up.

## Seeing it

Dug into rock, a timber portal with a black mouth and a cart on its rails - its rock grey against the mountain, as
the mountain's own; on an outcrop, a crane over the beds, the outcrop left in sight. Selected, those are outlined.

## Planned

- [idea] A spoil heap beside the portal that grows with what is dug.
- [idea] Coal from seams in the mountain's pockets.

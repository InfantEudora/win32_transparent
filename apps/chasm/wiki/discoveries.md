---
title: Discoveries
aliases: discovery, unlock, unlocks, new tools
kind: systems
status: built
summary: What the colony has found or done for the first time, kept for good. In play, the build bar shows a tool only once what it needs has been discovered.
plans: done/demo_plan.md > Discoveries; done/demo_plan.md > As built: discoveries (2026-10-16)
code: ApplicationChasm.h: DISCOVERY_RIVER DISCOVERY_CHASM DISCOVERY_BOULDER DISCOVERY_FLOATSTONE DISCOVERY_BUILDERS DISCOVERY_WINCH DISCOVERY_BALCONY; ApplicationChasmExplore.cpp: UpdateDiscoveries LookForFirsts ChasmToolNeeds ToolsLocked; Economy.h: EVENT_BOULDER_FOUND EVENT_BALCONY_REACHED
verified: 2026-10-24
---

A **discovery** is something the colony has found or done for the first time. It is kept for good: a hut
erased later takes nothing back. Nothing of its own is saved. Each is read from the [[Statistics|journal]]
(an [[Events|event]] the rules made), from what has been [[Exploration|explored]], or from a building
standing whole.

| Discovery | Made when | Brings |
|---|---|---|
| River | someone first sees a river or a waterfall ([[Areas]]) | WATER ([[Water collector]]), BRIDGE ([[Bridge]]) |
| Chasm | someone first sees the chasm's floor from the rim | WINCH ([[Winch]]), STEAM ([[Steam collector]]), CONDENSER ([[Condenser]]), SAIL LOFT ([[Sail loft]]), DOCK ([[Zeppelin dock]]) |
| Boulder | a boulder stands on the colony's own land ([[Settlement|its territory]]), on explored ground | STONECUTTER ([[Stonecutter]]), TERRACE ([[Terrace]]) |
| Floatstone | someone first sees a floatstone seam ([[Deposit|deposits]]) | MINE ([[Mine]]) |
| Builders' hut | the builders finish one | WALL ([[Roads and walls]]), RANGE ([[Archery range]]), TOWER ([[Guard tower]]) |
| Winch | the builders finish one | nothing: half of [[The demo's goals|the second goal]] |
| Balcony | someone stands on a balcony, lowered by a winch | nothing: the rest of the second goal |

Everything else is on the build bar from the landing. A boulder is found by whoever is nearest it.

- **What the landing sees counts.** Its first look round tells nothing, but a river or the chasm in sight
  then is a discovery all the same.
- **So does a building standing whole** of a kind that needs one: a collector or a bridge shows a river, a
  winch, a steam collector, a condenser, a sail loft or a zeppelin dock the chasm, a stonecutter's hut a boulder, a mine floatstone, and a builders' hut, a range or a tower
  the builders. That covers one put down in debug mode, and a save from before the journal: a colony that
  has one can build another.
- **Told** on the notice line with what it brings: "Anouk Wever has found a river - new tools: WATER,
  BRIDGE". A tool new to the bar has an amber dot until it is first taken up.
- **Play mode only.** A tool still waiting is not on the bar, its key does nothing, and NEEDS ATTENTION
  never offers it: BUILD A WATER SOURCE takes up the condenser while the water collector waits for a river, and
  FIND A RIVER stands in for it while both wait. Debug mode builds anything.
- `chasm_economy` lists each with its tick and who made it, and what play mode still hides.

## Planned

- [proposed] The first of each building kind, if more tools come to wait on discoveries.

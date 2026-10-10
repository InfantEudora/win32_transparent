# Arches: building over a road

An ARCH is Townscaper's: a road plot between tall buildings is built over by their upper storeys, over a
passage one storey high (`roads_plan.md`, "Under houses"; `ZoneArchStoreys`). It was built with the roads, before
buildings did anything in the game, and it is still only drawn: the plot stays a road with no building on it.
Talked through 2026-10-10 from the user's screenshot; step 1 built the same day.

## What is wrong today

- **Nobody owns it.** Clicking the arch selects the road, the building's outline has a gap there, its floor is
  not counted (no room for a store, no places in a house), nothing builds it - it appears free the moment the road
  is laid and goes when a neighbour loses a storey.
- **Its look is picked per drawing cell** (`BuildingMesh.cpp`, "An arch takes the look of the building beside
  it"): the first building among that cell's four corners, which can be a DIAGONAL corner the arch does not bridge.
  In the screenshot a store's arch took the woodcutter's yellow roof from beside it. Each cell decides alone, so one
  arch can mix looks, and its roof joins the buildings on both sides into one ridge.
- **Any kind bridges any kind**: the rule asks only for two storeys standing on every non-road side
  (`ArchCandidate`), so a house bridges into a store - and a guard tower, two storeys and drawn on its own, counts
  as tall (see "Towers", below).

## Decided (the user, 2026-10-10)

1. **Same kind only**: an arch bridges buildings of one kind - a house to a house, a store to a store. A house
   never bridges into a store.
2. **One owner**: where one building stands on both sides (a road through it, as in the screenshot), that building;
   otherwise the building with the most plots beside the arch, the older (lower id) on a tie.
3. **A real part of its owner**: drawn in its look, picked and outlined with it, and its upper storeys are the
   owner's FLOOR - a store's room, a house's places.
4. **Built, not free**: an arch's storeys are built like any storey, from what the owner's kind costs a storey,
   as a site with SCAFFOLDING over the road while it goes up. So adding a storey beside a road can make that
   storey dearer: the arch's storey on that level comes with it.

## Proposed (to talk through)

- **State**: the arch's storeys become zone state of their own, planned and standing per arch plot and saved, as a
  building's are - no longer worked out from the neighbours each time. The plot stays a road, walked as one, its
  first storey the passage.
- **When it is planned**: when the neighbours' storeys reach two either side and the run is short (the rule as
  today, by kind), the arch's storeys are planned up to the lower side, and built by the owner's builders like the
  owner's own storeys.
- **When it goes**: a neighbour losing storeys, the road erased, or a building of another kind placed beside it
  takes the arch's storeys down as removing storeys does - so a house's places can drop and its family be crowded
  (the issues panel says so already).
- **Its cost**: `EconomyStoreyCost` of the owner's kind for the arch plot's area, a storey at a time - the site costs
  per good (`defence_plan.md` step 4) cover it, so a kind that takes food or water a storey takes it here too.
- **Build order**: (1) same kind and the owner, drawn and picked - the look fixed; (2) the arch's storeys as state,
  counted as floor, saved; (3) built as a site with scaffolding, its cost; (4) losing it, and the crowding.

## Towers (decided, the user and the defence window, 2026-10-10)

- **Towers are never arched, for now** (the user's). A tower has no house body to carry a floor - it is logs round
  a deck (`BuildTower`) - so a tower neighbour stops an arch, by `ZoneKindHasBody`. A tower's ladder can then never
  stand under an arch either: its plot is always a tower's neighbour.
- **No arch over a gate**: a palisade gate's posts (`WALL_GATE_HEIGHT`, 1.80) and door (`WALL_DOOR_HEIGHT`, 1.42)
  stand taller than the passage (one storey, 1.30).
- **A GATEHOUSE later**, as its own thing (the user: "a really nice feature"): a walkway between two towers' decks
  over a palisade gate, with guard places of its own, reached by the towers' ladders and paid by the guard place -
  in `defence_plan.md`'s open list. It is what solves a gate between tall buildings.
- **For step 2** (the defence window's read): keep an arch's storeys APART from the road plot's `storeys` /
  `standing` / `building`, which several rules read as "built on the ground here" - the walkers would treat the
  road under an arch as a house (walked into only at a walk's end), straight walks would be blocked, and a tower's
  ladder check on load would fail. And keep arch plots out of a building's `plots_of`, whose first plot is read as
  its door in several places.

## Towers, as raised (kept for the record)


A guard tower is one plot, two storeys, drawn on its own (`BuildTower`) - so today it is tall enough for an arch:

- **A gate between two towers**: a road through a wall line, the wall's plots on its other sides being towers,
  is an arch candidate now. Same kind would make it a tower-to-tower arch - a GATEHOUSE. Wanted (guard places on it,
  a gate door under it), or towers never arched?
- **A ladder under an arch**: a tower's ladder may stand on a road plot (`ZoneTowerLadderOk` refuses buildings,
  walls and gates, not roads). An arch over that plot puts storeys over the ladder's foot.
- **A house arch over a gate**: a road crossing a wall line between tall houses can be arched by the houses; the
  gate's door would shut under it.
- **Costs**: a tower's storey costs per guard place (`TowerStoreyCost`), not by area; a tower arch would need a cost
  of its own if wanted.

## As built: step 1, the owner (2026-10-10)

Files: `Zones.*` (`ZoneKindHasBody`; `ArchCandidate` same kind with a body, never over a gate; `ZoneArchOwner`;
`ZoneState::arch` / `arch_owner` and `ArchOf` / `ArchOwnerOf`; `Zones::RefreshArches` at the end of every command,
a load and a new map), `BuildingMesh.cpp` (an arch corner is its owner's: look, roof colour and the roof's joins;
`CornerSolid` by `ZoneKindHasBody`, so a house beside a tower now closes its wall toward it),
`ApplicationChasmSelect.cpp` (the outline takes its arches; the pick takes an arch above its passage as its owner,
through the passage it goes on), `ApplicationChasmRoads.cpp` and `ApplicationChasm.cpp` (`chasm_road` and
`chasm_pick` report an arch's storeys and owner).

- **Worked out once, not saved**: the arches are derived from the zones after every change and kept beside them, so
  the drawing, the pick and the outline read one answer - and the outline, drawn from a state holding the one
  building alone, can be given its arches. Not hashed: nothing in the rules reads them yet.
- **Checked** (debug, a copied exe on 8772; seed 12, a road laid by the camp, two-storey buildings painted on the two
  plots beside a bend of it):
  - a store each side: an arch, two storeys, its owner the older store (4 of 4 and 5 - a plot each, a tie); in the
    screenshot it carries the store's grey roof and timber walls and meets the other store in a valley;
  - the pick at the screen's middle, the camera on the arch: plot 28565 (the arch), building 4 - where the ground
    alone gave the other store;
  - a house in place of one store: no arch;
  - one store painted round the far side, its plots either side of the road: the arch its own, one roof over the
    road; selected in play mode, its outline round the store and the arch together.

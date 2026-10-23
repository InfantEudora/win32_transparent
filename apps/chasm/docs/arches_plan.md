# Arches: building over a road

An ARCH is Townscaper's: a road plot between tall buildings is built over by their upper storeys, over a
passage one storey high (`roads_plan.md`, "Under houses"; `ZoneArchStoreys`). It was built with the roads, before
buildings did anything in the game, and it is still only drawn: the plot stays a road with no building on it.
Talked through 2026-10-10 from the user's screenshot; steps 1 to 4 built the same day.

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
  (the issues panel says so already). **Its goods stay** (the user's, 2026-10-10): a store - or a house's own
  stock - that loses an arch floor keeps what it held, over its room until it is drawn down, as one that loses a
  storey or part of its plots to an erase does today. Only a building pulled down loses its goods.
- **Step 4, decided** (the user, 2026-10-10, "those are all fine"):
  1. **Building on never takes one down**: the run limit (`ZONE_ARCH_MAX_RUN`) holds only NEW arches; one with a
     floor built stays while its own sides stand, however long tall buildings make its run.
  2. **No wall across a built arch in play**: refused, "an arch stands over the road", as a wall on a tower's ladder
     is. The wall has a gap there until a gatehouse (`defence_plan.md`); taking the arch down first closes it. A
     debug placement takes the arch down as before.
  3. **Said when it happens**: a player's command that takes an arch's floor down says so on the message line, in
     amber - "THE ARCH OVER THE ROAD CAME DOWN WITH IT".
  4. **Crowding, nothing new**: the crowded row and the house card already say it; the family stays. What crowding
     costs, and a family moving to a bigger empty house, is `lifeline_plan.md` step 7, for every cause alike.
- **Its cost**: `EconomyStoreyCost` of the owner's kind for the arch plot's area, a storey at a time - the site costs
  per good (`defence_plan.md` step 4) cover it, so a kind that takes food or water a storey takes it here too.
- **Build order**: (1) same kind and the owner, drawn and picked - the look fixed (BUILT); (2) the arch's floor
  counted as its owner's (BUILT); (3) the arch's storeys as state of their own, planned and standing, saved - built
  as a site with scaffolding, its cost (BUILT); (4) losing it, and the crowding (BUILT). Saving moved from step 2 to step 3: while
  an arch is worked out from the zones there is nothing of it to save, and a planned storey is what first needs it.

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

## As built: step 2, the floor (2026-10-10)

Files: `Zones.*` (`ZoneArchFloors`; `ZoneBuildingFigures` counts its owner's arches), `Economy.cpp` (`Derive`'s
`arch_floors`, in a builders' hut's places and a house's firewood).

- **An arch's floor** is its storeys over the passage, one less than it rises to (`ZoneArchFloors`), on the arch
  plot's area. `ZoneBuildingFigures` adds it to its owner's storeys and floor area, so everything read from them
  takes it: a house's places and pantry room, a store's room, a building's card, the tools' figures. Where the
  economy counts plot-storeys instead - a builders' hut's builders, a house's firewood burned and woodshed - it
  adds the arch's floors to the owner's.
- **Still free and at once**: the arch stands the moment both sides do, and goes when they no longer qualify - so a
  house's places can drop with it (step 4 is what makes that a deliberate loss; the issues panel's crowded row
  already says when it happens).
- **Checked** (a copied exe on 8772; seed 12, the road by the camp, two-storey houses painted either side of its
  bend): the first house alone - 2 storeys, 8.98 floor, room 35, burns 2, woodshed 8; the second painted, the arch
  formed, the first its owner - 3 storeys, 13.41 floor (the arch plot's 4.43), room 53, burns 3, woodshed 12. Its
  places stayed 6: a house holds one family at most (`ECONOMY_HOUSE_MOST`). A 300-tick recording with the arch in
  it: two replays identical, the arch back from the recording's state.
- **Not checked by a run**: a builders' hut's places from an arch between two huts, and a store's room (the same
  figures as a house's room, by the same path).

## As built: step 3, built as a site (2026-10-10)

Files: `Zones.*` (`ZoneState::arch_built`, `ArchBuiltOf` / `ArchStandingOf`; `RefreshArches(f_stand)`; the arch
branch of `ZONE_OP_BUILD_RAISE`; `ZoneSavedGround::arch`, put back in `Restore`; `ZoneBuildingFigures`' site storeys;
`SettlePriorities`), `BuildingMesh.cpp` (`BuildArchSite`; the body to what is built), `Economy.*` (`arches_of`; site
storeys, `EconomySiteCost`, `SiteNeed`, `Build`'s lowest-first raises, `EconomyStoreyCost` by the owner's kind),
`ApplicationChasm.cpp` (the save's grounds, the zones hash, `chasm_pick`'s `zone_arch`), `ChasmSave.cpp` (a ground's
fifth number), `ApplicationChasmSelect.cpp` (the outline), `ApplicationChasmRoads.cpp`.

- **What is planned and what is built**: what an arch may rise to is still worked out from the zones - from what
  STANDS either side, so an arch's floor is planned only once both sides stand to its height, and it is always
  buildable when it appears: nobody carries for a floor that must wait on the other side. What of it is BUILT is
  state: `arch_built`, its floors over the passage, saved with its road plot's ground entry and hashed.
- **A site of its owner's**: its floors still to build are the owner's site storeys, at `EconomyStoreyCost` by the
  owner's kind on the arch plot's area - a house's 9 wood for the 4.43 here. The owner's card goes back to "building
  on"; carriers bring the wood as to any site, and the builders raise the arch's floor in the same lowest-first
  order as the building's own storeys. So adding a storey beside a road can bring an arch floor's cost after it.
- **A debug placement** - a building painted or a storey added, a road laid, not in play - makes its arch stand at
  once, as it does the building; the builders' raises and a play command leave it a site.
- **Scaffolding over the road** (`BuildArchSite`): poles up from the road at the footprint's corners, a frame at the
  passage's ceiling while nothing of it stands, a ring of planks at each storey to build. The road stays walkable.
- **What no longer qualifies comes down**: a side lowered or gone, the road erased, another kind beside it - its
  built floors are cut to what may stand, as a storey taken off. Step 4 is what to do about it.
- **Checked** (a copied exe on 8772; seed 12, the road by the camp, two houses either side of its bend):
  - one storey each, in debug: no arch. A second storey each as a PLAY command: two sites, built by carriers; on the
    tick both stood the arch was planned, nothing of it built, its owner's card "House - building on: It takes 9
    wood, 1 storey to build, 2 standing, 2 carrying for it" - and soon after its floor stood, the owner at 3 storeys;
  - seen in play mode: the scaffolding over the road, outlined with the selected owner; then the arch, its roof the
    owner's, meeting the other house in a valley;
  - a save made while the arch was a site, loaded after it was built: back as a site, nothing of it built;
  - a recording from the play commands to the arch standing, 1,400 ticks: two replays identical.
- **Not checked by a run**: a debug placement's arch standing at once (the code path is the same flag as the
  building's), a store's or builders' hut's arch as a site, and an arch coming down when a side loses a storey.
  (The first and last were checked with step 4; the user saw the first in play too.)

## As built: step 4, losing it (2026-10-10)

Files: `Zones.*` (`ZoneArchStoreys`' run limit for new arches only; `RefreshArches` returns the floors that came
down, kept as `Zones::last_arch_lost`; a play wall over a built arch refused in `ZONE_OP_GROUND_PAINT`),
`ApplicationChasmRoads.cpp` (`LinePlotAllowed`: the wall's line red there, and refused as the line is placed),
`ApplicationChasmGhost.cpp` (the wall tool's hover red there), `ApplicationChasm.*` (the command handler counts a
play command's arch losses, `arch_notices`; `chasm_paint` reports `arch_floors_lost`), `ApplicationChasmTime.cpp`
(the notice, in the refusal's place and amber).

- **The run limit, for new arches only**: an arch plot with a floor built (`arch_built`) is an arch whatever its run,
  while its own sides qualify; a plot with none is planned only in a run of `ZONE_ARCH_MAX_RUN` or less, the built
  ones counted. So no passage of new arches grows past three, and tall buildings beside the next road plot never
  take down floors already paid for. It reads saved state, so a load works it out the same: `Restore` puts
  `arch_built` back before its last `RefreshArches`.
- **What still takes one down**, at once and its wood lost, as a storey taken off: a side lowered or erased, the road
  under it erased, a debug wall over it. Its goods stay with the owner (above).
- **The wall**: a PLAY wall over a road under a built arch is refused, "an arch stands over the road" - in the
  command, and before it, red in the line being drawn and on the tool's hover, as a tower's ladder is. An arch
  planned with nothing of it built does not stop a wall: its gate unplans it.
- **The notice**: a play zone command that took built arch floors down bumps `arch_notices`; the play overlay
  shows "THE ARCH OVER THE ROAD CAME DOWN WITH IT" for 3 seconds in the refusal's place, in `HUD_AMBER` - it is no
  refusal. A debug placement and the builders' raises say nothing.
- **Checked** (a copied exe on 8772; seed 12, a road laid through the small plots south-west of the camp, x 231 to
  242 at z -40 - plots there are mostly under 3.9, where a two-storey house holds fewer than `ECONOMY_HOUSE_MOST`):
  - at road plot 27698, one-plot two-storey houses either side, debug: the arch standing at once (built 1), its owner
    holding 5 with it (3 without); family 1, five, moved in, family 2, four, across the road;
  - a storey off the house across, in play: `arch_floors_lost` 1, the notice on screen; the owner's card "Holds 3, 5
    living here", "Crowded - add a storey or a plot", and NEEDS ATTENTION's HOUSE CROWDED, 5 IN ROOM FOR 3 (and the
    lowered house's 4 IN ROOM FOR 2); the storey back in debug: the arch standing again, nobody crowded;
  - a wall over the arch's plot: in play refused, "CAN'T BUILD: AN ARCH STANDS OVER THE ROAD" on screen; in debug the
    gate made and the arch gone; the gate's mark taken off, the arch back; the road erased in play: down, the notice;
  - stores either side of 27680, 27698 and 27691, debug: three arches, each standing; recorded from there, stores
    either side of 27689 too, a run of four: the three stayed, 27689 none. A storey off a side of 27698 in play took
    that one down, and the run broken, 27689 got an arch planned as a site (0 of 1 built) - the rule as it stands;
  - a save and load of that: the same arches; two replays of the 30-tick recording: identical.
- **Not checked by a run**: a store's or builders' hut's arch as a site (unchanged from step 3).

## As built: bends and heights (2026-10-23)

Two things the user found building on the terraces (`terraces_plan.md`):

- **An arch over an open corner.** Two-storey houses either side of a road's corner, on its outside, bridged the
  corner plot, with walls missing where the road turned (the user: nothing should be built there). Bends stay - a
  road stepping through a block of houses turns round plots that are built - but round a bend the cell the two roads
  turn round must have a building of the kind on its fourth corner too (`ArchCandidate`, `Zones.cpp`).
- **Heights by the sides, not the road.** An arch's floor was the road's ground plus a storey, and its sides had to
  stand two storeys each, counted from their own floors. So a one-storey house on a terrace a storey up never bridged
  (its ground floor is just where the floor goes), and where the road's ground rounded half a storey off the houses'
  the bridge stood half a storey off them. Now the floor is its **owner's storey** standing a storey over the road (a
  storey and a half at most, `ZONE_ARCH_PASSAGE_MOST`) - another side's, if the owner's leaves no room - and the arch
  rises to the **lowest side's eaves**; every side stands down to the floor and a storey of floor fits under the eaves.
  The floor is worked out with the rest (`ZoneState::arch_floor`, `ArchFloorOf`) and drawn, scaffolded and picked from
  (`BuildHouseCell`, `BuildArchSite`, `ShapeTop`); `chasm_pick`'s `zone_arch` gives it as `floor_y`.
- **Checked** on `saves/terraces_test.json`: a road along the foot of the top-5 terrace between a two-storey house below
  and a one-storey house on the terrace - bridged at 6.5, the terrace's top, the terrace house's ground floor (the house
  below, on a foundation half a storey off, meets it with a half step); a road round a corner with two-storey houses on
  its outside - no arch, until a house went on the inside corner; a straight road with the road half a storey over one
  side - no arch with two storeys there (its eaves under the bridge's), one at three.
- On a slope, a side whose floors stand half a storey off the bridge's needs a storey more than before - the bridge never
  rises past its eaves. An arch in an older save that no longer qualifies comes down on load, as any does.

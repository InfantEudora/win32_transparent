# Foundations and terraces

Talked through with the user 2026-10-21. The slope grid (the ghost's, `ApplicationChasmGhost.cpp`) showed what the
ground hides: round the second colony on the starting map's hills, between 45% and 77% of the plots near the cursor
were too steep for a house - a plot may rise only `ZONE_RISE_HOUSE` (0.7) across it. The user's idea: **build on
steeper ground once foundations are laid**, so hill towns can grow terraced, Greek-like, with one building's upper
storey meeting the next one's ground floor up the slope - which the castle prototype's walk layers
(`ramparts_plan.md`, `Walkers.h` LAYERS) already half do.

What is there to build on:

- **Houses already step.** Each plot's floor is its own ground rounded to half a storey (`BuildingMesh.cpp`,
  `foot[k]`), so a row of plots on a slope steps with a short wall between; a fixed `HOUSE_FOOTING` (1.2) of wall
  runs on under the floor to hide the slope.
- **A footing already costs** (`building_size_plan.md`): `ECONOMY_FOOTING_WOOD` more on a plot's ground storey, for
  the kinds drawn as a house body.
- **The walk graph has layers**: a node is a plot and a layer of half storeys over its ground; decks, stairs (odd
  layers) and connectors (a tower's ladder) join them. Zombies walk no deck.

---

## Decided (the user, 2026-10-21)

- **Build up only, for now.** A foundation is a stone plinth raised on the ground; nothing digs into a hill. The
  terrain (`Terrain::ground`, its mesh) is the map's and stays as it is - everything that reads it (walkers, the
  rules, shooting, snow, birds) goes on reading the same ground. Digging may come later.
- **Automatic**: painting a building on ground too steep for its footing gives it a foundation - no tool of its own,
  no step first. Its **stone cost shows** on the ghost's price tag, and the slope grid shows such ground in a colour
  of its own.
- **At most one storey**: with a foundation a plot may rise `ZONE_RISE_FOUNDATION`, one storey (1.3) - hills, not
  crags. Steeper is still refused.
- **Zombies do not use terraces**, for now - whether they may later depends on how hard the game turns out once
  tested. (A note for stage 2: a terraced town is then a fortress, its stairs the ways in.)

---

## Stage 1: foundations under buildings

**Which kinds**: those drawn as a house body (`ZoneKindHasBody`) - house, store, woodcutter's hut, builders' hut,
ropewalk, stonecutter's hut, condenser - but **not the two collectors**: a water collector's ground is a bank's and
a steam collector's a drop's, each by a rule of its own (`ZoneKindTakesFoundation`). Not gardens, lots, roads or
walls (ground, stage 2), nor the one-plot kinds drawn apart (winch, tower, mine, well) or a camp's tents.

1. **The rule** (`Zones.cpp`) - BUILT 2026-10-22: such a kind may stand where its plot rises up to
   `ZONE_RISE_FOUNDATION`; past `ZONE_RISE_HOUSE` it **needs a foundation** (`ZoneNeedsFoundation`). The rise is
   measured as before - every corner of every cell round the plot (`ZonePlotRise`, which `PlotIsBuildable` now
   asks too) - so nothing changes for ground a house already took. Refused past it with "too steep, even for a
   foundation".
2. **The look** (`BuildingMesh.cpp`) - BUILT 2026-10-22: a wall runs below its floor only as far as the ground under
   it, and `FOOTING_DEPTH` into it, instead of a fixed 1.2 - so the selection outline, which is drawn through the
   ground, no longer shows a wall a storey deep under every house. What shows below the floor is the **plinth**, in
   stone (a darker stone under stone walls) - a course under every building where its floor stands over the ground,
   and a tall one on a foundation's downhill side. A chimney's stack stands on the same bottom. **A foundation's
   floor is built up**: the highest ground under its walls rounded up to a half storey (`BuildingFootHalves`), where
   every other building's is its ground rounded to the nearest - so nothing, a door least of all, is dug into the
   hill. The site's scaffold, its flag and the builders' ladder go by the same floor.
3. **The slope grid** - BUILT 2026-10-22: three colours - green where the tool's building stands on its footing,
   **amber where it needs a foundation** (`ZoneLandSays`, ZONE_LAND_FOUNDATION), red where it may not stand;
   `chasm_tool`'s `slope_grid` counts all three. Round the second colony on `two_colonies` the house tool's red
   went from 78, 106, 106 and 143 plots at four places to 22, 4, 49 and 2 - the rest amber.
4. **The cost** (`Economy.cpp`, `EconomyKindStoreyCost`) - BUILT 2026-10-22: a plot that needs a foundation takes **stone on its
   ground storey**, by its area and how far its ground rises (`ECONOMY_FOUNDATION_STONE`), beside the footing's
   wood. So the builders bring it before the first storey rises, as they bring anything else - no stage of its own.
   The price tag says it: "COSTS 12 WOOD, 5 STONE - ON A FOUNDATION" for a click, and a drag counts them: "COSTS
   33 WOOD, 8 STONE - 4 PLOTS, 2 ON A FOUNDATION". Placed in play, the site's card asks "It takes 12 wood, 5 stone".
   Picking a building by its shape (`ApplicationChasmSelect.cpp`, `ShapeTop`) asks `BuildingFootHalves` too, so a
   building on a foundation is picked by its raised roof.

5. **Steps up to a door** (`BuildingMesh.cpp`, `DoorSteps`) - BUILT 2026-10-22: a ground-floor door over the ground
   outside it gets a straight flight of stone out from it, `STEP_RISE` a step and `STEP_TREAD` deep, each block
   measured against the ground under it - downhill the ground goes on falling - to the first that would stand less
   than `STEP_LEAST` over its own ground, at most `STEP_MOST`. None where another building stands across the wall.
   On gentle ground a door is all but at the ground and gets none; a foundation's downhill door gets six or so.
   The user's confirmation (2026-10-22): foundations are picked by their raised roofs.

Later, small: a card line saying the building stands on a foundation.

### Current tuning

| Constant | Value | What |
|---|---|---|
| `ZONE_RISE_FOUNDATION` | 1.3 | the most a plot may rise with a foundation: one storey |
| `ECONOMY_FOUNDATION_STONE` | 1.0 | stone a unit of the plot's area, a unit of its rise - a 3.4-area plot rising 1.0 takes 4 |
| `FOOTING_DEPTH` | 0.15 | how far a wall runs on under the lowest ground beneath it (`BuildingMesh.cpp`) |

---

## Stage 2: terraces as ground

A **terrace** levels a patch of plots at one height, so roads, gardens, fields and buildings go on it as on flat
ground; round it a **retaining wall**, and **steps** where a road crosses one. **Earth terraces for fields**
(cheaper, work only: a vineyard's or a paddy's) were suggested as a lighter way to farm the hills - not taken yet.

### Decided (the user, 2026-10-22)

- **Painted**: a TERRACE tool on the build bar, dragged over plots as a lot is. The patch is one flat platform.
- **Its height**: built up only - the patch's highest ground rounded UP to a whole storey. Terraces then always
  differ by whole storeys, which is what lets stage 3 join a house's upper floor to the terrace above. A drag that
  starts on a terrace EXTENDS it, at its height.
- **Between levels**: no stair tool - a road crossing a retaining wall of up to a storey is steps there, walked and
  drawn as such: stepped streets.
- **Everything stands on one**: buildings, roads, walls, gardens, lots, fields.
- And from stage 1: zombies do not walk terraces.

### Ground, not decks

This turns round what was written here before the decisions. The castle's decks (`ramparts_plan.md`) are counted from
each plot's own ground (`BuildingLayerHeight`: the ground and so many half storeys), so a castle wall on a hill slopes
with the hill; a terrace is flat across the slope. And what stands on a terrace stands on a plot's GROUND - a
building, a road, a garden, a field - none of which knows a deck. So a terrace is the plot's ground: one function,
`ZoneGroundAt` (as `BuildingFootHalves` is the one floor), that everything placing something on a plot asks rather
than the terrain - the rules, the walkers, the meshes, people's feet. The terrain itself (`Terrain::ground`, its mesh)
is untouched: the terrace is drawn over it, and only builds up, so the terrain never shows through its top.

About 300 places in 37 files read the terrain's height today. Not all of them place something on a plot (the
terrain's own mesh, water, the exploration's sight lines, the camera), and the ones that do move over step by step.

### The rules

- **Where**: a terrace plot is on one ground (no cliff in its cells), dry, off the mountain, on the plateau or a
  balcony, not the map's edge, with nothing built or laid on it and no field - terraces are painted first and built
  on after. Its top is at least the highest ground under its tile (the plot's polygon, as `BuildingFootHalves`
  measures a foundation), and at most `ZONE_TERRACE_MOST_FILL` over the lowest of it: a patch dragged straight down a
  hill is refused at its foot, so long terraces follow the hillside.
- **On it**, the ground is its top: the slope rules see a terrace plot flat (no foundation), a field's cell whose nine
  vertices are all on one terrace flat, a building's floor its top.
- **Built first**: nothing is placed on a terrace still being built ("the terrace is still being built").
- **Erased** only with nothing on it.

### Steps

- **2a, the terrace**: `ZoneState` (each plot's top in storeys, and whether it stands), `ZONE_OP_TERRACE` (paint and
  erase), the rules above, the tool and its drag (the height worked out over the drag; red where refused), the save
  and the zones hash; the look - a flat top and stone retaining walls down to the ground round it; `ZoneGroundAt` and
  what stands on a terrace drawn on it (buildings, roads, walls, gardens, lots, fields and crops, fences), the props
  under it gone, the slope grid. Debug paints it standing; play waits for 2c.
- **2b, walking**: the walkers' ground is `ZoneGroundAt`. An edge between a terrace plot and a neighbour more than
  `ZONE_TERRACE_STEP` above or below it is a retaining wall, shut - the uphill side, near the terrace's height, is walked
  on. Between two ROAD plots across a wall of up to a storey it is open, as steps: slower, as a stair is
  (`WALKER_SPEED_STAIR`), and drawn as steps. Zombies: shut out of every terrace plot. People's feet, crates and piles
  on a terrace at its top.
- **2c, building it**: a site as a lot is - its trees felled first, then its stone brought, by how much it fills
  (area and depth, `ECONOMY_TERRACE_STONE`), raised by the builders; until then the terrain is walked and nothing goes
  on it. Its price on the tag.

### As built (2026-10-22)

All three steps, checked in a copy of `two_colonies` on its eastern hillside, which falls about one unit in five to the
east (`saves/terraces_test.json` holds three terraces stepping down it, 116 plots at tops of 3, 4 and 5 storeys, with
houses on two of them).

- **By the square, not by the plot** - one change from the decisions, for the user to overrule. Painted plot by plot
  as a lot is, a terrace's outline zigzagged and a field almost never fitted (its cell needs all nine of its corners on
  the terrace: two of two were refused). The TERRACE tool drags over the large squares as the field tool does, and
  each square crossed takes its nine plots (`ZoneCellPlots`): straight outlines, and a field always fits. The rules
  and the commands are still a plot's (`ZONE_OP_TERRACE`).
- **The data** (`Zones.h`): `ZoneState::terrace`, each plot's top in whole storeys (`ZONE_NO_TERRACE` none), and
  `terrace_built`; `ZONE_OP_TERRACE`, `_ERASE`, `_RAISE`; saved as `terraces` ([plot, top, built], only with some),
  restored before anything that stands on them, and hashed only where there are some - so a colony without terraces
  saves and hashes as before.
- **The ground** (`ZoneGroundAt`, `ZoneGroundHeight`): a standing terrace's top, else the terrain's. What stands on a
  plot is drawn on it - houses (`BuildingFootHalves`, plinths, door steps, sites, the tower, the builders' yard, the
  ropewalk), roads, stakes, palisades and gates, garden walls and fences (on the higher side of an edge), gardens,
  lots, fields and crops, the ghosts and the slope grid - and the rules see it: no foundation on a terrace, a field's
  cell flat when all nine corners are on one. The kinds that may stand on one are those that take a foundation, and
  the guard tower; not a collector, winch, mine, bridge, well or camp. The castle prototype keeps off terraces.
- **The look** (`TerraceMesh.cpp`): the top in its biome's grass, so the seasons and the snow lie on it; a dressed-stone
  retaining wall with a coping wherever the plot across is lower; a site staked out on the ground. Props under a
  standing terrace are hidden (`EconomyPropCovered`). Its outline runs where a building's walls would
  (`BuildingWallCrossing`), not by the ground's edge midpoints: round a point of five or six cells those drew a STAR
  where a house there is a plain n-gon (the user, 2026-10-25), and the two now meet wall on wall. A road's stair, the
  road pieces either side of it and the walkers' climb (`RouteHeight`) go by that crossing too, so the stair meets the
  wall and the road on the terrace runs to its edge.
- **The retaining wall's look** (the user's, 2026-10-25, `TerraceMesh.cpp`): the face is **battered**, leaning back
  into the fill 1 in 6 - its head on the outline, its foot stepping out onto the ground below, so whatever stands on
  the terrace keeps its shape and the batter hides inside a building at the foot. The faces meet mitred at the
  cells' centres and along one shared direction at each crossing (`CrossingOut`), so the foot runs on unbroken. On it:
  a darker **footing course**; overhanging **capstones** with joints, a gap where a road's stair climbs the wall and
  none along a building standing over the top; lighter **quoins**, long and short by turns, up every outer corner -
  blocks standing proud of the face with joints between, in front of the creepers, which they z-fought as flat
  panels at one depth (the user, 2026-10-25); a
  **buttress** where a wall at least `TERRACE_PIER_FROM` high crosses a fine edge and runs straight on there, none
  where anything stands against it or a stair or door is; **creepers** hanging down about a third of the walls
  (chosen by the two plots, in the leaf colours, which keep summer's, so they read as ivy); and **tufts** at the foot
  on open ground.
- **Walking** (`ZoneTerraceStep`, asked by `Walkers::EdgeOpen`, `EdgeSpeed` and `EconomyStraightClear` alike): a
  terrace standing more than `ZONE_TERRACE_STEP` over the plot across is a WALL; up to a storey between two standing
  roads, a STAIR, walked at `WALKER_SPEED_STAIR` and drawn as stone steps on the lower plot's half (`RoadStairs`), where
  `RouteHeight` takes people up and down it. A zombie never steps onto a terrace. A villager sent from the upper
  terrace to the low ground east of it went down the road's stair and round the north end of the terraces - the gaps
  between them are walled - crossing no wall; one sent back was drawn on the steps. Crates and woodpiles on a terrace
  lie on its top.
- **Building it**: an unbuilt terrace plot is a ground site whose one good is STONE (`ground_site_stone`): its stone
  `EconomyTerraceStone` - its area times how far it fills on average, times `ECONOMY_TERRACE_STONE` - its trees felled
  first. Four plots painted in play by the first colony, with 40 stone put in its store: three raised once 10 stone
  was carried out, the fourth waiting on a tree. The price tag adds it up: a square's stone and its trees to fell.
- **Doors** (2026-10-23, the user: a house placed beside a terrace had its door inside the terrace). A wall's door was
  only ever its hash's, whatever stood across it. Now a ground-floor wall takes a door only where it leads somewhere
  (`WallDoor`): open ground or an arch's passage over its floor, and no WALL between (`ZoneTerraceStep`) - so none into
  a terrace's fill, none off a terrace's edge onto the drop, and none into another building or the same one standing
  higher, which happened on a slope before. A building none of whose walls drew a door gets one on its first wall
  that can take one (`BuildingSureDoor`) - one small house in ten had none at all. A shut-in building has none. A
  chimney's stack keeps off a terrace's wall too, and a window comes only clear of the ground outside, as it was cut
  by the grass of a terrace across. Checked with nine houses on the test save's terraces: against the long wall, in
  three-sided notches, on a terrace's edge, and one walled in on four sides.
- `chasm_tool` takes `terrace`; `chasm_paint` ops `terrace` (with `top`) and `terrace_erase`; `chasm_pick` gives a
  plot's `zone_ground_y`, `zone_terrace` and `zone_steps` (open, wall or stair to each neighbour).
- Unlocked with the stonecutter, by a boulder found (`ChasmToolNeeds`); key Y.

Open, for the user:
- **A road over a wall taller than a storey** is laid but not walked there: drawn up to the wall from both sides, which
  reads as broken. Refusing the road plot, or a longer stair, are the two ways out. (Decided 2026-10-26: refused - see
  "Steps built from below", below.)
- **Whole storeys on flat ground**: ground just over a whole storey gets nearly a storey of fill (seen at the first
  colony: ground at 0.05 under a top of 1.3). On a hill it hardly shows; on flat ground nobody needs a terrace.
- Not yet terrace-aware: a terrace site's card (it cannot be picked as a garden's can), a crate's pick height
  (`CrateMiddle`), the ray pick scripts use, arrows' and the exploration's sight lines. Zombies' keeping off terraces
  is in the rules but was not tried with a wave.

### Current tuning

| Constant | Value | What |
|---|---|---|
| `ZONE_TERRACE_MOST_FILL` | 2 storeys | the most a terrace's top stands over the lowest ground under it |
| `ZONE_TERRACE_STEP` | 0.35 | the most a terrace stands over the plot across and is still walked onto |
| `ECONOMY_TERRACE_STONE` | 0.5 | stone a unit of a plot's area for each unit it fills |
| `TERRACE_BATTER` | 0.16 | how far a retaining wall's face stands out for each unit down from its head |
| `TERRACE_PIER_FROM` | 1.6 | the least height of wall that takes buttresses |
| `TERRACE_CREEPER_SHARE` | 30 | percent of the walls between two plots that creepers hang down |

## Stage 3: buildings that join the levels

A building against a terrace's wall with a **second door on an upper storey**, onto the terrace above - so the levels
of a hill town join through the buildings, not only by stairs.

### Decided (the user, 2026-10-23)

- **Its own people walk through**: a walk that starts or ends at the building may go in at one level and out at the
  other - its family, its workers, carriers to it. Nobody else cuts through.
- **Onto open ground on the terrace**: a street, a yard or open ground, where its floor meets the terrace's top (or
  half a storey over it, with steps down). Not into the building next door.
- **Upper doors only**: flat roofs that are the terrace of the house above (the Santorini look) and appeal for a view
  wait.

### As built (2026-10-23)

- **The rule** (`ZoneUpperDoor`, `Zones.cpp`): a building on plot b against the wall of a standing terrace on plot u -
  nothing built, planned or shut on u - opens onto it on the lowest storey whose floor is at the terrace's top or half
  a storey over it, once that storey stands; the ground floor itself, on a foundation built up to it. Floors and tops
  are counted in half storeys (`BuildingFootHalves`, moved to `Zones` with `BuildingWallCrossing`, since the walkers
  now ask a building's floor). **One door a level from a plot**: onto the first of its neighbours that level opens
  onto - a plot meeting one terrace along two edges has one door onto it, not two.
- **The step** (`ZoneTerraceStep`): such a wall is `ZONE_STEP_DOOR`, not a WALL. The walkers (`Walkers::EdgeOpen`)
  pass it as they pass a stair - its climb is the building's stairs, walked at `WALKER_SPEED_STAIR` - and
  `chasm_pick`'s `zone_steps` say "door", with the door's storey.
- **Through one's own building** (`Walkers::EdgeOpen`): a building is walked inside by a walk to or from it - any of
  its plots, not only its door plot - and by nobody else. A general change: a walk to a building of several plots may
  now come in at its far side. Not a zombie's, whose way in stays breaching.
- **Drawn** (`BuildingMesh.cpp`): every upper door is drawn, always - on the piece of the wall in the lower-numbered of
  the two cells either side of the plots' edge (`BuildingUpperDoorAt`) - with steps down where it stands over the top,
  and no coping on the terrace's wall along a building that stands over its top (`TerraceMesh.cpp`). A chimney's
  stack keeps off such a wall. The ground-floor sure door (`BuildingSureDoor`) is the ground's: an upper door is no
  way out at the bottom.
- **Walked, drawn** (`ApplicationChasmWorkersView.cpp`, `RouteDoorLeg`): the rules walk people straight from plot to
  plot, over the wall where the plots' edge crosses it - beside the door, a quarter of a cell off. Drawn, they go up
  the building's stairs on its own side to the door's floor, out of the door, straight down its steps
  (`BuildingDoorStepsRun`, DoorSteps' own reach) and on - and back the same way. Only the drawing bends; the rules'
  positions are as ever.
- **Checked** on `saves/terraces_test.json`: a two-storey house below the top-5 terrace opens onto it from storey 1, its
  floor half a storey over the top (it stands on a foundation), with three steps; a person living there, sent onto the
  terrace, was drawn in the doorway, on the steps and off onto the terrace. A three-storey house in a notch, SHUT IN
  with one storey, opens onto the top-4 terrace from storey 1 and the top-5 one from storey 2 and is no longer shut in;
  its route to the top-4 terrace leaves by the door, and none goes from that terrace through it to the pocket at its
  foot. A walk to a two-plot house came in at its far plot; one past it went round.
- Found on the way: the test save's three terraces are **walled all round** - rounded up, nowhere does the hill meet
  them at about their top - so nothing reaches them on foot without a stair (the stage 2 test drew a road for it,
  not saved). A house with an upper door gives its own people a way up; everyone else still needs a stair.

## Steps built from below (2026-10-26)

The user, with a terrace of houses and gardens a storey up (`saves/gardens.json`): there was no way onto a terrace in
play without laying a road there in debug. The stair rule was a deadlock: a road across a terrace's wall became steps
only once BOTH its plots were laid (`ZoneTerraceStep`), and a road plot is laid by someone walking onto it - so the one
on the terrace could never be reached, and nothing walked onto a terrace without the steps. Debug paint lays both at
once, which is why stage 2's test never met it. Seen in a copy of `gardens.json`: a road painted in play from the
ground up onto the terrace's edge, the plot above still staked out 24,000 ticks later, its step still a wall, and the
house on the terrace listed HOUSE SHUT IN.

Agreed with the user the same day, and built:

- **The steps are the lower road's.** They are already drawn on the lower plot's side (`RoadStairs`); now they stand with
  it: a STAIR once the road at the wall's foot is laid and the plot above is a road, planned or laid. The road-layer
  climbs them and lays the one on the terrace as any other; builders reach what is going up on it. The lower road draws
  its way to the wall and up the steps before the one above is laid (`RoadNeighbours` joins a planned road its steps go
  up to).
- **A road across a wall taller than a storey is refused** (`ZoneCanGround`, `ZoneTerraceTooTall`): "steps climb a
  storey at most - put a terrace a storey lower between". Either way round - the road below or the one above painted
  second. A road along a tall wall's foot beside one along its top is refused alike: the road drawing joins any two road
  plots side by side, so the two would read as broken just the same.
- Nothing saved changes. A save that already holds a road over a tall wall loads as it is; the debug checks list it
  (`RunZoneChecks`).
- **Checked** (debug build, a test copy on 8771): in `gardens.json`, a garden plot on the terrace's edge erased and a
  road painted in play from the plot below up onto it: the lower plot laid within 5,000 ticks, the step a stair at once,
  the plot above laid by 6,000, and HOUSE SHUT IN gone - steps drawn up through a gap in the capstones. In
  `terraces_test.json`, a road on a three-storey terrace's edge (plot 8439) and one painted on the plot 1.7 under it
  (34442): refused with the message, in play and in debug.
- **Left**: a longer stair for taller walls - a flight along the wall's face, a plot of run a storey - for later; the
  castle's stairs (`castle_plan.md`, "The stairs") climb several storeys and may be the place to start.

## Open

- Whether a foundation's stone should go by the plinth's actual height under the walls rather than the plot's rise.

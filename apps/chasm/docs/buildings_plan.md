# Buildings as things

Talked through with the user 2026-10-05. Until now a plot held storeys and a ground kind and nothing
else: "a house" existed only in how the mesh drew a row of plots under one roof, and nothing in the
simulation could say *this is the store* or *this family lives here*. The gameplay (`gameplay_plan.md`)
needs exactly that - families in houses, goods in stores, workers at workplaces, a crop on a field,
and later the winch to a balcony. This is the step that comes first.

## Decided

- **A building is a group**: plots for buildings, coarse cells for fields. One rule for every kind.
  A house of one plot, a storehouse of twenty, a field of six cells are each one building.
- **A building has an identity of its own**, kept in the simulation: every plot (and every field
  cell) names the building it belongs to. Nothing is worked out afterwards from what touches what,
  so two houses side by side are two families, and joining or erasing never leaves it ambiguous what
  became of whom.
- **A drag is one building.** A drag that starts on empty ground makes a new building; one that
  starts on a building of the same kind extends it. Separate drags stay separate buildings even where
  they touch. A click on a building's plot adds a storey to that plot, as before. A drag only claims
  plots joined to the building it is making - one that jumps across a gap starts another; a diagonal
  step across a cell brings the corner plot between, since a drag on this grid steps diagonally as
  often as not.
- **Erasing through a building** can leave it in pieces: the largest keeps its identity (and later
  its people and goods); each other piece becomes a building of its own (later with its share of the
  stock, by floor area). Deterministic, so a replay stays exact.
- **Each building draws as one**: roofs join within a building and meet in a valley against another,
  and a building has one roof colour - so where one ends and the next starts can be seen.
- **Sizes come from the economics, not the brush.** One hard limit where anything else would be
  nonsense - a house is at most `ZONE_HOUSE_MAX_PLOTS` plots, since it holds one family - and the rest
  by what each kind costs and holds, once there are costs (prototype P2-P4):

  | Kind | Meant to be | What pushes it there |
  |---|---|---|
  | House | 1-3 plots, up in storeys | One family, at most 5-6 people; room beyond that is wasted, too little is crowded |
  | Store | big | One keeper whatever its size, room by floor area |
  | Workshop (woodcutter first) | medium | A fixed cost per building plus workers by floor area, up to a most |
  | Field | several coarse cells | One crop per field, a farmer per area; each cell keeps its own history for rotation |
  | Water collector | small, at the water | Must touch a river's bank or a pool - the one exception to "too close to water" |
  | Winch (later) | one plot, on the rim above a balcony | A hard rule, and the exception to "too close to a cliff" |

- **Gardens and town ground stay ground**, not buildings. A garden could later belong to the house
  it adjoins; nothing needs that yet.
- **First the buildings food, water and wood need**: house, store, field, woodcutter, water collector.
  The winch comes after them.

## The order

| Step | What |
|---|---|
| 1 (BUILT 2026-10-05) | Buildings with identity: the building table, plots and field cells naming their building, drag = one building, erase splitting, kinds (house, store, woodcutter, water collector), the waterside rule, each building drawn as one with its kind's look, fields as buildings with a crop each; saves, the state hash, replay, tools, MCP, the panel showing the building under the cursor |
| 2 (BUILT 2026-10-06) | The winch: the rim rule, its look, its link in the walkers' graph |
| 3 | Into the gameplay prototype (P2-P4): goods in stores, workers at workplaces, families in houses |

## Step 1: how

- **State** (`Zones.h`): a building table - id, kind, crop for a field - with ids handed out in order
  and never reused, so a save names a building the same way for ever; `building[v]` per plot and
  `field[c]` per coarse cell now hold a building's id, 0 for none. `storeys[v]` stays per plot, for
  every kind: a two-storey store holds twice a one-storey one.
- **Commands**: the zone command carries the kind and a STROKE number - every command of one drag
  has the same - and the zones keep which building the current stroke is making. Recorded and
  replayed like every zone command; the stroke in progress is saved and hashed with the rest, so a
  recording that starts mid-drag replays the same.
- **Rules** as now for every kind, plus: a house's plots at most `ZONE_HOUSE_MAX_PLOTS`; the water
  collector must have water among its corners - a vertex under the water or on the bank - instead of
  keeping off it, and may stand on a bank's slope.
- **The look** (`BuildingMesh.cpp`): roofs raised only toward plots of the same building; roof and
  walls by kind - houses plaster under tile or shingle with chimneys, stores timber under shingle,
  the woodcutter's hut timber with a log pile, the water collector stone by the water.
- **Fields**: a field is a building of cells; its crop decides the rows' colour; a fence runs between
  two fields as round a field's outside. Crop chosen per field (a panel button and the MCP), not by a
  hash per cell.
- **Saves**: a list of buildings, each with its kind, its plots and their storeys or its cells, and its
  crop. A save from before this loads each old house plot as a one-plot house and each old field cell
  as a one-cell field.

## Step 1, as built (2026-10-05)

Files: `Zones.*` (the table, the strokes, splitting, the rules, `ZoneSaveBuildings`, the checks),
`BuildingMesh.*` (roofs by building, `kind_looks`), `CropMesh.cpp` (the crop), `BoundaryMesh.cpp`
(fences between fields), `ChasmSave.*` (version 2), `ApplicationChasm.*` (tools, strokes, the hash, the
panel, `chasm_paint`), `tools/chasm_replay_test.py`.

- **The table**: `ZoneState::buildings`, by id, from 1 up, never reused - an erased building's entry
  stays, kind none. `building[v]` per plot and `field[c]` per coarse cell hold ids; `storeys[v]` stays
  per plot for every kind. Storeys at most: house 4, store 3, woodcutter 2, water collector 1.
- **Strokes**: the zone command carries the stroke in `value[2]` (exact in a float below 2^24). A
  press in the view starts one; a tool's calls with the same `stroke` are one drag (`chasm_paint`,
  kept in a range of their own). The zones keep the stroke in progress and its building, and both are
  saved and hashed. A load sets the view's counter to the save's, so the next press never continues
  the saved stroke. Calls without a stroke - old scripts, old recordings - are each a building.
- **A diagonal step** brings the corner between: a drag along a straight line steps from a plot to
  the one diagonally across a cell as often as not, which shares no edge with it - without this a
  straight seven-plot drag made four stores. The first of the two corner plots that can be built
  joins first. Fields the same, through a cell beside both.
- **A full house** goes on with the next: a drag past four house plots starts another house rather
  than refusing, so a long drag lays a row of houses.
- **Splitting**: an erase floods from the removed plot's old neighbours in the building; the largest
  piece keeps the id (ties to the lowest index), the others get new ids in order of their lowest
  index, and are drawn again for their new roof colour.
- **Two rules found on the way**: nothing is built on the CHASM FLOOR (gameplay: under the mist); it is
  flat, so nothing refused it, and the replay test's village had been standing there since the chasm
  came from the seed - it is now on the home side's plateau (`DX` in the script) and reports its
  refusals. And the ground the rules read leaves the river channels out, so the water collector's
  rule puts the channel back in: a corner of its cells under the water or within 0.05 of it, its own
  vertex 0.15 above it.
- **The look**: a roof runs on only to a corner of the same building at the same height (an arch
  matches either side), so neighbouring buildings meet in a valley; one roof colour per building, by
  its id. Houses plaster under tile or shingle with chimneys; stores timber under slate, wide doors,
  few windows; the woodcutter dark planks under thatch; the water collector stone under shingle.
  Fields: the crop's colour for the whole field; one fence between two fields, none inside one.
- **Saves**, version 2: `buildings` - id, kind, plots with storeys or cells, crop - with
  `next_building` and the stroke. A version 1 save loads each house plot as a one-plot house and each
  field cell as a one-cell field (checked).
- **The panel** names the building under the cursor (kind, id, plots or cells, storeys, floor area)
  and lets the selected field's crop be chosen; tools 8, 9 and 0 are store, woodcutter and water
  collector. `chasm_pick` and `chasm_paint` return the building at the place; the `layout` summary
  lists the rivers' courses, for finding a bank.
- **Checks**: `zones` now also counts each building against what names it, checks each is one piece
  of one kind, and no house is over its plots.
- **Measured**: the replay test (now painting a store in one stroke and splitting it, a woodcutter, a
  house extended by a stroke that starts on it, a crop change) passes in debug and release; a demo
  village of separate two-plot houses, an eighteen-plot store, a woodcutter, three fields with three
  crops and a water collector on a bank passes every zone check.

**Open:**
- No gameplay yet: a building holds nobody and nothing (step 3).
- A field's cells keep the bare margin at every cell edge, so a field of several cells shows earth
  strips between them; the rows could run on across.
- A water collector looks like a small stone house; a jetty or a wheel would say what it is.
- The woodcutter's hut has no log pile: dressing has to go on the plots around a building, which
  nothing draws yet.
- A walker cannot reach a water collector: it stands on wet ground, which walkers do not cross.

## Step 2, the winch, as built (2026-10-06)

Files: `Zones.*` (`ZONE_KIND_WINCH`, `WinchIsBuildable`, `ZoneWinchLanding`, `ZoneWinchLinks`, landings
kept clear), `Walkers.*` (the link, the ride, `Walkers::Height`), `BuildingMesh.cpp` (`BuildWinch`),
`ApplicationChasm*.cpp` (tool H, the walkers' height), `tools/chasm_replay_test.py`.

- **Where it stands**: one plot, on the plateau, where the cells round it have balcony ground among
  their corners and no other lower level - the rim right above a balcony, not above the open chasm.
  Its plateau corners follow the usual rules (dry, off the mountain, no field, a rise of at most 0.7).
  In practice that is a plot of the rim's own chain above a balcony's stretch.
- **Its landing**: of the balcony corners of those cells, the nearest the plot's vertex (the lower
  index on a tie) - a property of the ground alone, so the rules, the walkers and the look agree on
  it. Nothing may be built on a landing ("a winch lands there"), and a winch is refused if its
  landing is built on.
- **One plot, always**: a winch never joins a stroke's building; a drag of the winch tool makes one
  each.
- **The walkers' link**: from the winch's plot to its landing - the only edge across a level. It takes
  `WINCH_RIDE_SECONDS` (5) whatever its length, and A* counts it at that. A winch's plot is a passage,
  walked through, unlike other buildings, which are a walk's first or last plot only. The links are
  worked out again whenever the zones change (`Walkers::EnsureLinks`), and a walk across one is open
  only while its landing is clear.
- **The ride, seen**: for the first fifth of a ride the walker steps out from the rim to above the
  landing, at the rim's height; then straight down the rope (going up: up first, then in). View only -
  the simulation knows the edge and the distance down it, as for any edge.
- **The look**: a timber deck from behind the plot's vertex to the lip (halfway to the landing, where
  the terrain cuts the cliff), a gantry of two posts and a crossbeam at the lip, a jib out to above the
  landing, a rope straight down to a basket resting on the balcony, and the winding drum in a small
  shed behind. Drawn by the first cell round its plot; the generic building body is skipped for it.
- **Tool**: H (for hoist - the digits are taken), and `kind: "winch"` in `chasm_paint`.
- **Measured**: on seed 1's home balcony a winch stands on the first rim plot tried; a walker living
  on the balcony crosses it to the landing, rides up, and walks on across the plateau. The replay test
  now builds a winch above the home balcony (found from `layout.balcony_list` by trying, so the test
  survives layout changes) and spawns a walker who rides it, and passes.

**Open:**
- Only walkers ride it; there are no goods yet (step 3), and no limit on how many ride at once.
- The rope and basket are always at the bottom; the basket does not move with whoever rides.
- A winch stands only where a balcony runs right under the rim's chain; a balcony with a ledge of
  plateau between it and the rim edge (none today) would want the rule to look one ring further.

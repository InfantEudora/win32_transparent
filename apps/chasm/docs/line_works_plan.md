# Line works: roads and walls, drawn and then built

Agreed with the user 2026-10-06. Until now a road was painted like a pen: each plot the cursor crossed
became road at once, and the trees under it vanished from the view (`PropHidden`). In play a road should
be PLANNED by a drag and BUILT by the villagers, who walk out and fell whatever stands in the way. Walls
round the village are the next tool of the same kind - the opposite of a road, impassable.

The bridge (`bridge_plan.md`) already works this way - a drag previewed green or red, placed whole, built
by the idle - and the gardens and lots made into sites the same day (`ground_built`, `ZONE_OP_GROUND_RAISE`,
`ground_site` per plot) give ground the planned / built split this needs. Line works are those two put
together.

## Decided (the user, 2026-10-06)

- **Drag, preview, release.** Press, and the line follows the cursor's trail - FREEHAND, today's feel -
  drawn green where the rules allow it and red where they do not; let go and the whole line is placed.
  Right-click cancels it. Auto-routing a road round obstacles may come later; not now.
- **Idle villagers do the work**, the same people who carry wood to construction sites now. No builder
  job yet (that question stays with P5, `construction_plan.md`).
- **Trees in the way are felled to logs**, at the felling person's woodcutting pace - their strength,
  the skill a woodcutter's pace goes by (`EconomySkillFactor`) - and left lying where they fall.
- **Roads cost labour only.** No wood, no stone.
- **Building sites clear their trees too.** A house painted in the woods today just hides the trees under
  it; it should have them felled first, like a road.

## The plan

### The model: ground on a chain of plots

- **A road stays ground** (`ZONE_GROUND_ROAD`), so the mesh, the gates into gardens, the arches and the
  walkers' road speed are what they are. Painted in PLAY it is a SITE: `ground_built[v]` 0 until it is
  laid. Painted in debug it is laid at once, as now - the difference travels in the command's play flag,
  as it does for buildings.
- **A planned road plot** holds its plot (nothing else may be painted there) and is walked as plain
  ground. It is not yet a road: no road speed, no gate, no arch, no road mesh - it is drawn as a line of
  stakes instead - and the trees beside it stand. One helper, `ZoneRoadStands(z,v)`, takes the place of
  the `ground[v] == ZONE_GROUND_ROAD` tests where a planned road must not count (about 25 of them, in
  `Zones`, `Walkers`, `RoadMesh`, `ZoneMesh`, `ApplicationChasm*`). Where it means "walk fast" it also
  takes a standing bridge plot (`ZoneBridgeWalkable`), as the walkers' EdgeSpeed does now.
- **A wall is ground too**: `ZONE_GROUND_WALL` (4, appended - never renumber), on a chain of plots,
  drawn along the fine edges between them the way a road is. A palisade of logs for now; stone when
  there is stone. Its wood goes the ground sites' per-plot way (`ground_site[plot]`, worker.site =
  ECONOMY_SITE_GROUND | plot).
- **A wall stops walkers only once it stands.** A half-built wall has gaps, and a planned ring does not
  shut the builders in. A chain joined along fine edges seals: on a planar grid no path along the edges
  can cross it without passing through one of its plots - provided the drag fills the corner where it
  steps diagonally across a cell, which the road tool already does (`RoadBridgePlot`).
- **A gate is where a road crosses a wall**: a road plot in the wall's chain stays road, and the wall
  stops either side of it with gate posts. A road painted across a standing wall takes that plot over
  and opens a gate; a wall dragged across a road leaves the road and gates it.

### The tool (view side only)

- `UpdatePaint`, road and wall tools: a press starts a stroke and remembers the plots the cursor
  crosses (the corner between a diagonal step included); nothing is sent while dragging. The chain is
  history, not a function of the hover (unlike the bridge's, which `ZoneBridgeChain` works out again from
  `bridge_from`), so it is kept - under a lock with a version - for the render thread to draw.
- Drawn by the PLAY-MODE GHOST (`ApplicationChasmGhost.cpp`, window 39's, render thread): it already
  shows a road tool's hovered plot as a flat tile (`GhostPlotTile`). The chain goes into its `GhostKey`
  and each chain plot gets a tile. The ghost is tinted as ONE mesh (`f_ghost_ok`), so a per-plot verdict
  (`ZoneCanGround`: green allowed, red refused) needs a second ghost object for the red plots, or a
  vertex colour. In debug mode the line pick view draws the chain too, beside the bridge's preview (play
  mode hides the pick view for every tool, the bridge included since 2026-10-07: its chain is ghost tiles).
- On release the chain goes out as one stroke of per-plot `ZONE_OP_GROUND_PAINT` commands with the play
  flag. They are recorded like every zone command, so working out the chain need not be deterministic.
  Refused plots are skipped - a road broken at a river is where a bridge goes. A refusal shows above the
  build bar as any refused zone command does (`zone_refusals`).
- Right-click during a drag drops the chain. In play it must go through `UpdatePlayPick`'s right-click
  (ApplicationChasmSelect.cpp) or before it, so a cancel does not also put the tool down or deselect.
  Shift-drag erases, freehand, also previewed.
- `chasm_road` (MCP) gains `play:true`, placing a planned road along its line.

### The work (Economy)

- **A new errand for the idle** beside carrying wood: a LINE WORK (a new flag bit in worker.site's
  encoding, beside ECONOMY_SITE_GROUND). From home, to the nearest plot of a planned road or wall still
  to be done (nearest to him, so a line grows outward from the village end). There:
  1. **Clear**: every standing tree whose foot is within the line's verge - the clearance the view
     hides trees by today (`PROP_ROAD_CLEARANCE`, measured from the centre line `RoadNear` draws, with
     planned plots counted as road) - is felled, one at a time, `ECONOMY_FELL_SECONDS` at his strength
     pace. It becomes `PROP_STATE_LOG` and lies there; in the view it falls away from the line. Claimed
     through worker.prop, as the woodcutter's trees are.
  2. **Lay** (a road): `ECONOMY_LAY_SECONDS` at his pace, then `ZONE_OP_GROUND_RAISE` on that tick, as
     the app applies the builders' raises now.
  3. **Build** (a wall): once its wood is there, through the ground sites' wood delivery, then raised
     the same way.
- **What the ground sites assume today**, to widen (window 39, 2026-10-06): `Zones::Apply` GROUND_PAINT
  sets `ground_built = (f_play && ZoneEnclosesGround(kind)) ? 0 : 1`; `Economy::ground_sites` and
  `SiteAlive` test `ZoneEnclosesGround`. Both become "a ground kind that is built" - gardens, lots,
  roads, walls. `ZoneGroundStands` (encloses && built) stays as it is for boundaries. A road site's need
  is not wood: `SiteNeed` 0 would make `FindSiteWork` skip it, so roads get their own errand and never go
  through the wood fetch; a wall's wood goes through it (`EconomyGroundWood`, `ground_site[plot]`).
  He goes on to the next plot of the same line for a stint, then home - so a long road far out is not a
  walk home per plot, and a round still ends at his house.
- **Two people never take the same plot or the same tree.**
- **Logs lie where they fell.** Woodcutters already take a lying log within reach of their hut before a
  standing tree. Possibly later: idle carriers take lying logs as wood for any site, so a wall through a
  forest is built from its own trees.
- **Building sites** get the same first stage: a house, store or hut site's trees on its plots are
  felled before its wood is carried to it (`FindTree`'s "hidden under a building" stops being true for
  a site whose trees still stand).
- **Saved and hashed**: the new worker states and the plot he works, with the people; trees already are
  (`prop_state`), and a planned road with the grounds (`ZONE_GROUND_SAVED_SITE`).

### The look

- A planned road: stakes and a cord along its centre line; laid, the road as now.
- A planned wall: stakes, then the palisade rising plot by plot as it stands, gate posts at roads.
- Stumps on a laid road are hidden, as trees near one are today.

## Steps

| Step | What | State |
|---|---|---|
| 1 | The tool: drag, preview, release, cancel - for roads; debug and play still lay them at once | BUILT 2026-10-06 |
| 2 | Road sites: `ZoneRoadStands` (+ `ground_built`), the staked look, the clearing + laying errand, saves, replay test with a road through a forest edge | BUILT 2026-10-06 |
| 3 | Walls: the kind, its rules, walkers closed where it stands, the palisade mesh and gates, its wood | planned |
| 4 | Building sites fell their trees first | planned |
| later | A bridge's chain drawn freehand with the same stroke (dry bank, wet run, dry bank) - c4's suggestion | idea |

**Depends on** the gardens-and-lots-as-sites work (`ground_built`, `ZONE_OP_GROUND_RAISE`, `ZONE_GROUND_LOT`)
landing first - steps 2 and 3 build on its sites and its wood delivery. `ZoneRoadStands(z,v)` itself
came in with the bridge as a road over water (window c4, 2026-10-06): `ground == ROAD || ZoneBridgeWalkable`,
used where a road is judged for walking and for its curve; step 2 adds `&& ground_built` to its road half.
The plain `ground == ZONE_GROUND_ROAD` tests that mean "painted as road" (the rules) stay as they are.

**Testing**: `python apps/chasm/tools/chasm_replay_test.py` after each step (PASS debug and release); the
replay test grows a planned road through trees, and later a wall with a gate.

## As built

### Step 1: the tool (2026-10-06)

Files: `ApplicationChasmRoads.cpp` (`UpdateLineDrag`, `PlaceLine`, `DropLine`, `LineStepTo`,
`LinePlotAllowed`), `ApplicationChasm.cpp` (`UpdatePick`: the right press and the scripted button;
`UpdatePaint` hands the road tool over; the pick view draws the chain in debug; `chasm_tool`, `chasm_pick`),
`ApplicationChasmGhost.cpp` + `chasm_ghost.frag` (the chain in play).

- **The chain** is the physics thread's (`line_chain`, under `pick_mutex`, `line_version` for the
  previews). Each new hover plot is stepped onto it along fine edges; a jump is walked, each step to the
  neighbour nearer the cursor that keeps nearest the straight line (nearest-the-cursor alone zig-zagged).
  Coming back onto one of the last 8 plots cuts the line back to it; further back is a line coming round
  to itself (a wall ring), which goes on. A straight line still steps where the grid runs across it - a
  road along edges must (roads_plan.md, "Roads curve").
- **Release** sends each plot once, in drawing order, as `ZONE_OP_GROUND_PAINT` with one stroke and the
  play flag of the mode; plots the preview showed red are left out and the first of them is sent last, so
  the zones refuse it and the overlay says why ("CAN'T BUILD: A BUILDING IS THERE" for a line through the
  camp). Released anywhere - over the overlay or off the map too: the line is what was previewed.
- **A right press** drops the line, before `UpdatePlayPick`, so it does not also put the tool down; the
  release that follows places nothing. Putting the tool down mid-drag drops it too.
- **Shift at the press** makes an erasing line: it takes up road plots only (the old road tool's shift
  took up any ground), all of it red in the ghost.
- **The preview**: in play, the ghost draws a tile per chain plot, one mesh, a refused plot's vertices
  marked by u < 0 for the shader's red; the outline is green while any plot will be placed. In debug,
  the pick view outlines each chain plot green or red. Both by `LinePlotAllowed` - `ZoneCanGround` for a
  road, and play's "under the clouds".
- **Scripts**: `chasm_tool` takes `button` ("down", "up", "mouse") and `right_click` with the pinned hover
  (hover_x/hover_z), and reports `line_plots`; `chasm_pick` reports the plot's `zone_ground` and
  `zone_ground_built`. A frame between calls, or a press and let-go land in one pass.
- **Tested** (debug, seed default): a drag of 6 steps and a 12-unit jump made a 20-plot line, placed
  whole; drawing back shortened it (7 -> 6 -> 5 -> 3); a right press dropped a line and placed nothing;
  in play the ghost showed the line green with the camp's plots red, and release laid the road round the
  camp with the refusal above the bar. Replay test PASS (debug).

### Step 2: roads as sites, cleared and laid by the idle (2026-10-06)

Files: `Zones.*` (`ZoneRoadLaid`, `ZoneRoadPlanned`, `ZoneRoadStands` laid-only, the paint and raise ops,
gates and arches), `Economy.*` (the errand), `RoadMesh.*` (the stakes, `ROAD_PROP_CLEARANCE`, `RoadNear`
laid-only), `ZoneMesh.cpp`, `ApplicationChasm.cpp` (`PropHidden`, the felled logs, the save flag,
`chasm_pick trees_within`), `ApplicationChasmEconomy.cpp` (hash, `chasm_economy` `roads`),
`ApplicationChasmWorkersView.cpp`, `ApplicationChasmRoads.cpp` (`chasm_road play:true`), `ChasmSave.cpp`
(`stint`), `tools/chasm_replay_test.py`.

- **The zones.** A road painted with the play flag has `ground_built` 0. `ZoneRoadPlanned` is such a plot,
  `ZoneRoadLaid` a built one, and `ZoneRoadStands` - what walking and the drawn road go by - is now laid
  roads and standing bridges only. Gates (`ZoneGateOf`) and arches open only on laid roads. The rules
  still test the painted ground, so a planned road holds its plot. `ZONE_OP_GROUND_RAISE` lays a road plot
  as it builds a garden's wall; a DEBUG paint over a planned road lays it at once (as a debug click
  finishes a building's site). A save marks it with `ZONE_GROUND_SAVED_SITE` like a garden site.
- **The errand.** An idle person at home looks for wood to carry first (`FindSiteWork`); with none, for road
  work (`FindRoadWork`): the planned plots nearest him that nobody else is on, and at the first he can walk
  to, the nearest standing tree in its way nobody is after - else, with none standing, the plot to lay.
  States `WORKER_TO_ROAD`, `WORKER_CLEARING` (`ECONOMY_FELL_SECONDS` at his pace), `WORKER_LAYING`
  (`ECONOMY_LAY_SECONDS`, 4, at his pace); the pace of the jobless is strength, a woodcutter's skill. His
  `site` is `ECONOMY_SITE_GROUND | plot`, as a ground site's carrier's is; a road's `SiteNeed` is 0, so the
  wood errand never takes one. After each piece he takes the next within `ECONOMY_ROAD_REACH` (10) of him,
  up to `ECONOMY_ROAD_STINT` (8) pieces a trip (`stint`, saved and hashed), then goes home - and goes home
  at once if he was given a job meanwhile.
- **Trees in the way** (`Economy::RoadTrees`): standing trees on the plot's own ground within
  `ECONOMY_ROAD_VERGE` (the view's `ROAD_PROP_CLEARANCE`, 1.3, + 0.4 for the curve inside a corner) of its
  WHOLE edges to the road plots beside it, planned or laid, and standing bridges - or of its vertex alone.
  Felled, a tree is `PROP_STATE_LOG` and lies there; woodcutters already take lying logs before trees.
  Laying checks again, so a neighbour painted meanwhile cannot leave a tree under the road.
- **The look.** A planned plot is a stake with a red ribbon at its vertex and a cord out to each edge toward
  a road plot (to the next stake, or down to a laid road at its edge's midpoint) - a line of them reads as
  a road pegged out. Its trees, bushes and rocks stand until it is laid; then the view's verge hides them
  as before (`RoadNear` laid-only). A felled tree's stump under a laid road's verge is hidden; its LOG is
  always drawn, and if the way it fell puts it on the road it is dragged off along its line until its near
  end and middle are clear (`RoadNear` with `f_line_only` - a whole road plot counting as near hid nothing
  useful here). A clearing or laying worker bobs like a woodcutter at work.
- **Tools.** `chasm_road play:true` plans a road; `chasm_economy` reports `roads` (planned, laid,
  `people_on_them`) and a road worker's `ground_site` and `stint`; `chasm_pick trees_within` lists the trees
  round a point with their state.
- **Measured** (debug, seed 1, 3x): two lines of 29 plots on open ground by the camp, laid by up to ten of
  the idle in about 16 s; a 16-plot road through four oaks - every oak felled to a log, the road laid. The
  replay test now plans a 17-plot road through those oaks BEFORE its save, so the save catches settlers
  mid-errand and both replays must match tick by tick; afterwards it must be laid with all four felled.
  PASS (debug), also with window 39's one-lot-per-woodcutter fix. Release not run.

**Open:**
- Erasing a planned road loses nothing (no wood), but trees already felled for it stay felled.
- The economy fells a little wider than the view hides, so a stump can stand just off a laid road.
- No priorities between road work and anything else: wood carrying always comes first.

# Placing by drag: every tool as the road is

Agreed with the user 2026-10-17/18, from feedback on the build tools. Until now only the road and the wall
(`line_works_plan.md`) were drawn first and placed on release; every other tool painted as the cursor went,
each plot a zone command the moment it was crossed, so what a drag would make was only seen once it was made -
a house that filled at four plots went on as another without a sign. The tool's tip, beside the grid, said
only about what a plot cost.

## Decided (the user, 2026-10-17/18)

- **Every building tool drags as the road does**: a green preview of where it is going to be built, placed on
  release where possible. Seeing where a house past its size becomes a separate one.
- **A size for every dragged building**: 4 plots (`ZONE_BUILDING_MAX_PLOTS`) - a house, a woodcutter's hut, a
  water collector, a builders' hut, a ropewalk, a stonecutter's hut - but a store 6 (`ZONE_STORE_MAX_PLOTS`) and
  a field 8 cells (`ZONE_FIELD_MAX_CELLS`). The range, a building on cells, takes the 4 (`ZONE_RANGE_MAX_CELLS`).
- **The one-plot tools are not dragged, only placed**: the winch, the guard tower, the mine - a click each.
- **The tool's tip is at the bottom**, and says what the tool is, how its drag works and what it does - the
  one-plot tools that they are not dragged. The list of what each tip reads was agreed from a draft; the
  garden's "nothing grows in it yet" dropped.

Mine, said at the time: **the camp has no size** - its tents are its settlement's one camp (`settlements_plan.md`),
and a cap would cap the settlers' beds. A bridge is placed whole and spans what it spans.

## As built (2026-10-18)

- **The rules** (`Zones.h`): `ZoneKindMaxPlots` gives every kind its size - 0 for none (camp, bridge), 1 for the
  one-plot kinds (`ZoneKindOnePlot`) - and a stroke that fills a building goes on with a new one, as it did for a
  house. Fields and ranges now do the same (`Zones::Apply`, FIELD/RANGE_PAINT). `FullRefusal` says it per kind.
  The zones' check counts any building over its size ("too big"). No save on disk had one.
- **The drag** (`ApplicationChasmRoads.cpp`, beside the road's): `DragTool` - a building's tool but the one-plot
  kinds and the bridge, the garden, the lot, the field, the range, the eraser. A press starts `drag_chain` (a
  plot, or a cell tool's coarse cell), Shift making it one that takes away; held, `LineStepTo` - the road's own,
  so the trail is joined along fine edges and drawing back shortens it - or `CellStepTo` for cells; let go,
  `PlaceDrag`. Let go where it was pressed, it is a CLICK as before: a storey more on a building, a new one on
  empty ground, one off with Shift. A right press, or another tool taken up, drops it (`DropDrag`).
- **One way to the rules** (`ApplyZoneCommand`): the zone command's handler and the preview both go through it -
  play mode's refusals (clouds, crates, territory), the camp a tent joins, then `Zones::Apply`. `PlanDrag` makes
  the release's commands (each plot or cell once, in the order drawn) and runs them on a copy of the published
  zones (`Zones::Adopt`), so the preview is what the release does. The release sends what the copy took and
  leaves out what it refused - that refusal said by the overlay through `drag_last_refusal`, since sent itself it
  would be cleared by what follows.
- **The preview**: play mode's ghost draws the copy's new plots with only what the drag makes or grows standing,
  so the buildings it makes stand apart; a yard's plots; a field's cells; every other building a shade darker
  (`chasm_ghost.frag`, a u past 1), in the order made; red where refused, and all of it red taking away. Its
  price tag is the whole drag: "COSTS 138 WOOD - 2 TREES TO FELL - 5 HOUSES, 20 PLOTS". Debug mode outlines each
  command's plot or cell green or red (`UpdatePickView`). Both read one cached plan (`CurrentDragPlan`).
- **The one-plot kinds**: `UpdatePaint` acts on their press alone; with one selected, their tool places a new one
  rather than extending it (`PaintOwner`).
- **THE TIP** (`DrawToolTip`, `ToolTipText` in `ApplicationChasmTime.cpp`): hovering a tool on the build bar, a
  panel at the bottom, left of the grid and level with its foot - the name and key, what it is, how it is placed
  (sizes and storeys read from the rules), its price (`ToolCostTip`), and its controls in dim; wrapped at a
  reading width so it stays clear of the panels in the bottom left.
- **Tools**: `chasm_tool` reports `drag_steps`; its `button` drags every painting tool.

## Tested (2026-10-18, debug, seed 12, scripted drags through `chasm_tool`)

- A house drag across 20 plots: the tag said 5 HOUSES, 20 PLOTS, and five houses of 4 plots each were placed.
- A store across 8 plots: 6 and 2. A field across 13-21 cells: 8, 8 and 5, the middle one darker in the preview.
- A drag right-clicked before release placed nothing; an erase drag took two stores off; a garden drag laid 6
  plots, each garden; a click on a house added a storey.
- The tower, pressed and dragged 8-12 units, placed one - in play and debug. The mine and winch, off a deposit
  or a rim, nothing.
- Debug mode: a house drag along a bank outlined its plots red by the water and placed the two it could.
- The tips for HOUSE, ERASE and TOWER on screen; `chasm_check` passes ("0 too big").
- Not tested: Shift drags (a script cannot hold Shift), a camp drag, the price tag beside a real cursor.

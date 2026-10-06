# Water collectors and bridges

Agreed with the user 2026-10-06, after seed 12 showed that a water collector could be placed but never
worked: the river's wet band is its water AND a strip of stone bank (Terrain.h: wet = within
TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN, 4.5, of the water's edge - about twice the water's width), the
old collector rule put the collector inside that band, and walkers cross no wet plot - so its carrier
could never reach it. BUILT the same day.

## Decided

- **The wet ground stays as it is** - unwalkable, unbuildable.
- **A water collector stands at the edge of the bank**, on the last dry plot, and reaches the water
  with a PIPE over the stones - which also makes it easy to tell from any other shed.
- **A bridge is a building**: built with wood by the idle like any other, a straight drag from a dry
  bank to the other, spanning at most TWICE THE RIVER'S USUAL WIDTH - so rivers can be crossed, lakes
  cannot.

## As built

Files: `Zones.*` (the kind, the op, `ZoneBridgeChain`, the collector rule), `Walkers.cpp`, `Economy.*`,
`BuildingMesh.cpp` (`BuildBridgePlot`, `BuildWaterPipe`, a bridge site), `ApplicationChasmBridge.cpp`
(`chasm_bridge`), the tool in `ApplicationChasm.cpp`, `Terrain.h` (`WaterEdgeDistance`, a public face on
the edge raster).

- **The collector** (`PlotIsBuildable`'s waterside rule): its own vertex dry, a corner of its cells wet.
  The pipe leaves its wall and runs down over the stones on posts to an intake box in the water - toward
  the water down the slope of the water-edge distance, as far as the edge and 0.6 past it; at a swamp
  pool (which the edge raster does not know) toward the wet corners round it. Its carrier walks to it
  as to any building, so the walkers' exception for a collector's wet plot is gone. Seed 12: placed on
  both banks near the camp, every one reachable (before: none).
- **The bridge** - `ZONE_KIND_BRIDGE` (8), placed whole by `ZONE_OP_BRIDGE` (12: the start plot in the
  index, the end plot in value[1]), never plot by plot. `ZoneBridgeChain` walks fine edges from one end
  to the other (each step the neighbour nearest the far end that stays near the line) and allows it when
  both ends are dry, every plot between is wet, all are on one ground, none has a building or ground on
  it, it keeps 9 from a fall's lip, and the water it crosses - the span less the two banks' wet margins -
  is at most 2x the nearest river's usual width (`TerrainRiver::width`). Seed 12 by the camp: a 16.9
  span, 10 wet plots of 12, allowed; one plot further back on either bank is refused ("dry ground
  between"). A PLAY bridge's both ends are held to the clouds.
- **Walking**: a wet plot is walked only on a bridge plot that stands (`ZoneBridgeWalkable`), as a
  passage, at road speed - and a road onto a bridge is all road.
- **Building it**: wood at 0.75 a house's (a deck, no walls) - 80 for seed 12's crossing. Carriers
  bring it to the dry ends (`Economy::NearestPlot`'s f_dry), and it rises from its banks inward (the
  plot nearest dry land first). Seed 12, play: eight idle settlers carried the camp's 40, the new
  woodcutter's pile the rest; built, and a walker from the camp then crosses (40 plots; before, none).
- **The look**: the plots zigzag along the grid's edges, the deck does not - it runs straight between
  the two dry ends, each plot drawing its own stretch of that line (so a half-built bridge shows what
  stands, from both banks), with rails on posts and piles in the water. A site shows its piles standing
  out of the water and planks stacked on the banks. Any change to a bridge plot redraws all of it.
- **The tool**: J (B is the previous seed), press on one bank, let go over the other; while dragging,
  the chain is drawn green or red. The build bar has it; a refused placement now says why above the bar
  for three seconds in play mode ("CAN'T BUILD: TOO WIDE - ..."). `chasm_bridge x0 z0 x1 z1` (check:true
  only checks; play:true places a construction). `chasm_pick` reports a plot's ground and wet.

**A ROAD OVER WATER (the user, 2026-10-06), BUILT.** The first look - a straight deck between the dry
ends - did not match how people cross: they walk plot to plot along the chain, which wobbles under it,
and it did not meet the roads at its banks. Now `ZoneRoadStands(z,v)` (Zones.h, agreed with
win32-transparent-cc, whose road sites will add "a planned road does not stand") counts a standing
bridge plot as road wherever a road is walked (Walkers' road speed) or drawn (RoadMesh's neighbours),
and RoadMesh draws a bridge plot as the road's own shape - the curve through the edges' midpoints,
which lie on the walkers' line, so they stay on the deck - in timber at the deck's height, with edges,
rails on posts and piles in the water. Where it meets a road on land it ramps down to the road over
the half-edge, so the road runs onto it unbroken. A bridge still being built is BuildingMesh's: piles
out of the water, planks on the banks; a half-built one shows its standing plots from each bank.
ROADS MAY RUN BESIDE THE WET (PlotIsBuildable's f_path): a road plot needs only its own vertex dry, not
every corner of its cells - otherwise no road could reach a bridge's end, which is by rule the last dry
plot, and none could follow a bank. Buildings, gardens and lots keep off the wet margin as before.
Seed 12: a bridge with a road up to each end (`chasm_road` to the end plots), joined on both banks.

**Open:**
- A bridge cut in two by an erase loses a dry end on one piece, which is then not drawn (still walked
  where it stands).
- Bridges are one plot wide; carts and two-way traffic come later if they matter.
- Saves made before this with a collector in the wet band lose that collector on load (the rules refuse it).

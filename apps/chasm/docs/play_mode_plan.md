# Play mode and the clouds

Agreed with the user 2026-10-06. Chasm has two modes from here on:

- **Debug mode** - what exists today: the ImGui panels, the debug views, the tools that place anything
  anywhere at once, the free camera. For building and testing.
- **Play mode** - the game as a player sees it: no panels, the game's camera, the clouds over what is
  not explored, and (later) building that costs goods and time.

**Switching:** `U` - core's existing UI toggle (`Application::f_show_ui`). Panels shown is debug mode,
hidden is play mode. A `CONFIG=release` exe starts in play mode (`U` still shows the panels); a ship
exe has no ImGui and is always in play mode. A debug exe starts in debug mode, as now.

**THE MODE IS A VIEW, NEVER A RULE.** The simulation must not read it, or a replay recorded in one mode
would differ in the other. Where a rule differs between the modes - building under the clouds, a
building placed at once or as a construction - the difference travels IN THE RECORDED COMMAND (a debug
placement carries a flag saying so), set by whoever issued it.

## Steps

1. **The modes and the game's camera.** `ApplicationChasm::PlayMode()`; in play mode the debug views are
   off whatever their toggles say, and the camera is the game's: zoom out at most to the user's
   reference view (2026-10-06 screenshot: a village, its roads and the woods round it filling the
   screen - `CHASM_PLAY_DIST_MAX`), a pitch range that stays a top-down view, the orbit point kept on
   the map. Leaving debug mode zoomed out eases the camera in. The cursor highlight is the game's and
   stays.
2. **Exploration - simulation state.** A raster over the map (`Exploration`, 2 units a cell) of what has
   been seen. Cleared, and staying clear, by what every PERSON out of doors can SEE as they walk,
   sampled every few ticks - a line of sight over the ground (see "Sight" below; it replaced a radius
   round everything built, 2026-10-08). A new map's settlers clear the camp's ground on the first tick.
   Saved and hashed (a trace part `explored`), so a replay and a load agree on it.
3. **The build rule.** A paint command on a plot or cell that is not explored is refused ("under the
   clouds") - unless the command carries the debug flag, which the debug tools always set. So in
   debug mode nothing changes, and a recording made there replays the same.
4. **The clouds - the view.** Lumpy flat-shaded white puffs like the chasm's mist, high over the land,
   breathing slowly (core's INSTANCE_MOTION_PUFF), one instance set per terrain chunk, rebuilt for the
   chunks whose exploration changed. At the explored edge they thin and shrink away rather than stop at
   a line. Shown in play mode; in debug mode behind a View toggle, off by default, so testing sees
   everything.
5. **Construction - BUILT 2026-10-06, `construction_plan.md`.** In play mode a building is a
   CONSTRUCTION SITE: it takes wood (the only thing it takes, the user's call), carried there by idle
   people, who build with each load before the storeys stand. In debug mode a placement still places it
   at once; `chasm_paint play:true` starts a construction from the tools. Play mode has the building
   tools (all but the debug walker) and a bar along the bottom showing them and their keys (a grid in the
   bottom right corner since 2026-10-08, `selection_plan.md`).

## As built (steps 1-4, 2026-10-06)

- **Modes** (`ApplicationChasm::PlayMode`): panels hidden is play mode; `#ifndef DEBUG` the app starts
  with them hidden; a build without ImGui is always play. Play mode holds the select tool (clicking
  still selects), ignores the tool keys, N/B and F, and hides the grid views and the walkers' paths
  (`UpdatePlayModeViews`; leaving play mode rebuilds them as their toggles say). `chasm_view play`
  switches modes over MCP - what U does at the desk.
- **The game's camera** (`UpdateCamera`): distance 12-55, pitch 34-77 degrees, the orbit point kept
  10 inside the map's edge. The player's input STOPS at a limit (the wheel past 55 does nothing - the
  user found zooming out and springing back odd); only a view already outside, leaving a debug view
  far out or a camera_set, eases in at 6/s. 55 matches the user's reference screenshot (pines about
  half the height they stand at distance 24).
- **Exploration** (`Exploration.*`, `ApplicationChasmExplore.cpp`): a 2-unit raster (seed 1: 385 x 216).
  As first built it was cleared 26 round each built plot, field cell and road and 14 round every person;
  since 2026-10-08 only by sight (below). Reset on a new map. Saved as `explored` - run lengths, a
  couple of hundred characters for a new colony; a save without it loads all explored - and hashed as
  the trace part `explored`.
- **The rule**: a zone command with `value[3]` set (a PLAY command) on unexplored ground is refused
  "under the clouds", in the zone command's handler before the zones see it. Nothing issues play
  commands yet; `chasm_paint play:true` does, to test it - refused far from the camp, accepted beside it,
  and the same paint without it accepted anywhere. Every recording so far has 0 there, so replays are
  unchanged.
- **The clouds** LIE ON THE GROUND (reworked 2026-10-06: the first version floated at y 17 and the
  user could look under it): puffs on a 4.5-unit jittered lattice over every unexplored point, radius
  3.2-4.8, the middle 1.6-3.0 over the HIGHEST ground under the footprint and never below the
  plateau (so the chasm is lidded at the rim), bobbing only 0.25 so the underside stays buried. Where
  the next lattice point is a cliff's drop lower, puffs stack down the face toward it, or the
  mountain's rock shows through. Within 8 of explored ground they shrink to 70% and sink into the
  ground, so the bank slopes down to its edge. The foam's white and the frozen lip's blue-white; cast
  shadows. One set per terrain chunk per shade, rebuilt whole when the exploration changes; the
  lattice and its ground heights are worked out once a map (`cloud_lattice`). Debug mode: View > fog
  of war, or V (`chasm_view clouds`), off by default.
- **Cost** (debug, minimized, seed 1, near the camp at play zoom): 7.6-7.8 ms GPU without clouds,
  9.0-9.1 with - about +1.3 ms (the high version was +2).
- **Checked**: the replay test PASSES (with `explored` in its trace), seeds 1-10 pass, seed 1's grid
  hash unchanged, a save and load keeps the clearing.

**Open:**
- On the mountain's cliffs the stacked puffs read as columns up close (debug zoom only).
- Play mode has no way to see the date's controls' effect beyond the overlay. Its build UI is the
  bar of tool keys (construction_plan.md); there is no way to click a tool yet.


## The camera over a balcony (the user's, 2026-10-14)

- **Found by the user** (`saves/casm_edge.json`, two villagers posted on a balcony 12 under the rim): the camera
  stays on the high level, which makes it hard to see what is going on, and the sun's shadow is clipped. "We'd need
  a way to determine where the camera needs to be height wise." Seeing under the cloud cover is left as it is.
- **Why**: the orbit point rode the PLATEAU's ground wherever it was - on purpose, so crossing the chasm would not drop
  it 70 units. Over a balcony it turned about the air 12 over it, so the view looked past the balcony, and the sun's
  box - a fixed square, 0.9 camera distances, round the point at height 0 - ended before the far half of what was on
  screen: the view meets lower ground further out, and the sun sees it shifted. Outside the box nothing is shadowed
  (lighting.glsl), so a cloud's shadow ended on a straight line.
- **Built** (`ApplicationChasm.cpp`, view only - no tick reads any of it):
  - *The height* (`ViewGroundHeight`): the orbit point on the ground of the plot under it - the plateau with its
    relief, a balcony, a shard. Over the floor (under the cloud deck) and bare rock it keeps the height it has, so the
    chasm is crossed level. A level is kept while ground of it lies within `CHASM_CAM_LEVEL_HOLD` (2.5), so panning
    along a rim does not bob; another level is glided to (`CHASM_CAM_LEVEL_GLIDE`), the relief followed as before.
  - *Clearance* (`ClearPitch`, play mode): the camera tilts up over the player's pitch, as little as it must, so the
    line from the point to it keeps `CHASM_CAM_CLEAR` over the ground - not inside the cliff behind a balcony, and
    seeing the point past the rim. Eased; the player's own pitch (`cam_pitch`) is untouched. The line starts on the
    ground, so the clearance ramps in over `CHASM_CAM_CLEAR_RAMP` (3) from the point: flat ground never tilts the
    view, at any zoom (2026-10-23 - asked in full of any ground above the point, zoomed in, the plateau's ripples
    flicked it between 54 and 69 degrees while panning).
  - *The shadow* (`FollowSun`): the sun's box fitted to the ground in view - where the view's corners meet the heights
    shadows fall on, `SUN_FIT_BELOW` under the point to `SUN_FIT_ABOVE` over it or the plateau - square, at most
    `SUN_FIT_MOST` camera distances, its size in steps of `SUN_FIT_STEP` so it seldom changes, still snapped to texels.
- *Checked* (8769, `casm_edge`): the point on the balcony at -11.9 (was 0.06); the balcony fills the view and its
  shadows reach across it; a low view from the plateau side (yaw 200, distance 20) looks down the wall at the two
  villagers and the waterfall from below the rim - the camera now goes under the rim over a balcony, which
  waterfall_plan.md and rivers_plan.md had as never happening; panned out over bare rock and the floor it held -11.9;
  on the plateau, 0.07. Looking at a balcony across the rim tilts the view up - at the foot of the wall, to about 83
  degrees.

## Sight (2026-10-08)

The user: a road placed at the edge of the clouds revealed what lay past it at once - placing things
explored. Exploring should be the villagers' own LINE OF SIGHT. So nothing is cleared round what is
placed or built any more; only what people see.

- **Who looks:** every person out of doors (`EconomyWorker::f_indoors` false - the camp's tents count as
  out of doors), every 10 ticks, from their exploration cell's middle, the eye 0.8 over their own plot's
  ground (`SightEyeGround`: the plot's, not the cell's, which at a rim may be the chasm floor).
- **What they see** (`Exploration::See`): rays to every cell on a square's edge, one cell a step; along
  each, the horizon is the steepest the ground has risen so far, and a cell is seen when a point 1.0 over
  its ground clears it. Range 20 on level ground, plus 1 for every unit the eye stands over the cell
  (a hill or a terrace step looks out further over lower ground), at most 40. Anything lower than both
  the eye's ground and the LID - the plateau's own ground there, its rise and fall carried on over the
  chasm, where the clouds lie - counts as air at the lower of the two: from the rim one sees ACROSS the
  chasm from its very edge (the lid is what is seen), from a balcony across it at the balcony's height.
  A flat 0 for the plateau was tried first: where the rim stands on a rise, the chasm at a person's feet
  hid behind the lip.
- **The ground it runs over** (`ExplorationSight`, `ChasmWorld::sight`): the ground's height and the lid
  under every cell of the raster, from the world alone - built with the map (12 ms, seed 7).
- **Cost:** a look is a few hundred to a few thousand cell steps. One from the same cell at the same eye
  height clears the same cells again, so it is skipped (`looked_from`, not saved: emptied on a load, after
  which the first looks clear nothing the save had not) - the daily walk to work costs nothing after the
  first time.
- **Tuning** (`SIGHT_*` in `Exploration.h`, current tuning, not rules): eye 0.8, target 1.0, range 20,
  +1 per unit of height, at most 40.
- `chasm_pick sight:true` returns what a person standing at a point would see, on a blank raster of its
  own (read only); `chasm_view` returns how many cells are explored.
- **Puffs past the raster** (fixed 2026-10-24, the bug report `saves/bug_stuck_character.json`): the clouds' lattice
  starts at the map's corner and is jittered by `CLOUD_JITTER`, so its first row and column, and a few of its last,
  stand off the exploration's raster, where `Explored` is false for good - on seed 12, 33 puffs along the west edge,
  65 along the north and 2 on the east stayed with everything round them seen. The clouds now ask
  `Exploration::ExploredNearest`, the nearest cell's, so a puff past the edge clears with the edge it hangs over. The
  rules (`Explored`) are as they were. Checked: the two on the east edge gone in the save; nothing left along the
  west and north edges with the map revealed.

## The ghost (2026-10-06)

In play mode the line mesh is gone (since 2026-10-07 for the bridge too); a paint tool
shows a GHOST of what it would place instead (`ApplicationChasmGhost.cpp`): see-through
(`chasm_ghost.frag`, lit, alpha 0.45, no depth write), green where the rules allow it and red where they
refuse, with the outline pass round it in the same colour.

- A building tool: on empty ground the new building's plot, one storey; on a building, that building with
  one storey more (the existing storeys sink into the real ones; the new one shows on top). Built with
  `BuildHouseCell` on a cut-down ZoneState, as the selection's outline mesh is.
- Garden, lot, road: the plot's ground as a tile; field: the coarse cell; erase: what would go, in red.
- Winch (2026-10-07): the gantry as above, plus its plot as a tile and its landing on the balcony as
  another - the gantry is thin, and absent wherever there is no balcony to lower to, which was most of
  the map showing nothing at all.
- Bridge (2026-10-07): tiles, as a road's line. Before the press the bank it would start from (dry, free);
  dragged, the whole `ZoneBridgeChain` to the cursor in one colour, since the verdict is the whole
  bridge's. A tile never lies below the deck's height (`BuildPlotTile`'s `floor`), so over the water it is
  where the deck will run rather than on the river bed.
- Play mode's own rule counts: red under the clouds - for a bridge, at both ends.
- Rebuilt only when the hover, tool, zones or exploration there change (`GhostKey`).
- `chasm_tool hover_x/hover_z` pins the hover for a script (a script has no mouse); `hover_release`
  frees it. The tool's name table now covers camp and bridge too (it read past its end for them).

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
   been seen. Cleared, and staying clear:
   - round everything BUILT - buildings, fields, roads, the camp - a radius each (larger than a
     person's), worked out again when the zones change;
   - round every PERSON and worker as they walk, a smaller radius, sampled every few ticks;
   - a new map starts with the area round the camp spot clear.
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
   tools (all but the debug walker) and a bar along the bottom showing them and their keys.

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
- **Exploration** (`Exploration.*`, `ApplicationChasmExplore.cpp`): a 2-unit raster (seed 1: 385 x 216),
  cleared 26 round each built plot, field cell and road (once a plot - explored never goes back, so a
  repaint costs nothing) and 14 round every person out of doors every 10 ticks. Reset on a new map, so
  the camp clears its own ground on the first tick (seed 1: a clearing about 35 across once the
  settlers have stirred). Saved as `explored` - run lengths, a couple of hundred characters for a new
  colony; a save without it loads all explored - and hashed as the trace part `explored`.
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

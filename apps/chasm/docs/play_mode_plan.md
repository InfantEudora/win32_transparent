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
5. **Construction (to design next, with the economy).** In play mode a building is a CONSTRUCTION
   SITE: it costs goods, carried there by the colony's carriers, and takes builders' time before it
   stands. In debug mode a placement still places it at once, and starting a timed construction is a
   debug tool too. Not built in this round: it leans on the people and the carriers (`people_plan.md`,
   `economy_plan.md`), and is its own plan.

Until step 5 exists, play mode places nothing - there is no way to build in it yet, and the debug
tools are debug mode's.

## As built (steps 1-4, 2026-10-06)

- **Modes** (`ApplicationChasm::PlayMode`): panels hidden is play mode; `#ifndef DEBUG` the app starts
  with them hidden; a build without ImGui is always play. Play mode holds the select tool (clicking
  still selects), ignores the tool keys, N/B and F, and hides the grid views and the walkers' paths
  (`UpdatePlayModeViews`; leaving play mode rebuilds them as their toggles say). `chasm_view play`
  switches modes over MCP - what U does at the desk.
- **The game's camera** (`UpdateCamera`): distance 12-55, pitch 34-77 degrees, the orbit point kept
  10 inside the map's edge - all EASED in at 6/s, so the wheel past 55 springs back and leaving a debug
  view far out glides in. 55 matches the user's reference screenshot (pines about half the height they
  stand at distance 24).
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
- **The clouds**: puffs on a 7-unit jittered lattice over every unexplored point, radius 6-9, at
  y 17 (or 9 over the mountain's ground), the foam's white and the frozen lip's blue-white, breathing as
  the mist does; within 16 of explored ground they shrink to 35% and sink 6, so the edge thins. They
  cast shadows - on each other, and on the explored edge. One set per terrain chunk per shade,
  rebuilt whole when the exploration changes. Debug mode: View > clouds (`chasm_view clouds`), off.
- **Cost** (debug, minimized, seed 1, the camp at play zoom): 7.9-8.0 ms GPU without clouds, 9.8-10.0
  with - about +2 ms, mostly the shadow and colour passes; 1.66M more vertices in all (the shadow pass
  draws every puff). Over solid cloud the screen is all puffs; worth watching.
- **Checked**: the replay test PASSES (with `explored` in its trace), seeds 1-10 pass, seed 1's grid
  hash unchanged, a save and load keeps the clearing.

**Open:**
- At the very edge the shrunken puffs read as separate small balls; packing them closer there, or
  letting them overlap more, would read more like a cloud bank's edge.
- Step 5, construction (c4).
- Play mode has no way to see the date's controls' effect beyond the overlay, and no build UI.

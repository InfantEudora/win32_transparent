---
name: bomber-app
description: "apps/bomber - a bomberman on this engine; 16x16 maze from bomber_assets.glb, walking character, bomb with a fuse, raymarched blast drawn two ways"
metadata: 
  node_type: memory
  type: project
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T14:28:35.382Z
---

`apps/bomber`, started 2026-09-15. A bomberman, and a bench for the volumetric blast that drives it.

**Split:** `Maze.h/.cpp` holds the RULES (grid, walker, fuse, blast reach) in tiles and ticks with
no engine type in its header - breakout's Field/Application split. `ApplicationBomber` is the view.
If running it twice for one tick would change the outcome, it belongs in Maze.

**Art** is one GLB, `assets/meshes/bomber_assets.glb`, exported by the user:
- tiles are **1x1 world units**, so `BOMBER_CELL_SIZE` is 1.0 and is NOT a free parameter.
- node X/Z translation is Blender layout spacing and is thrown away; **node Y is kept** - it is the
  height the piece needs to stand on the ground (tile_rock +0.32, bomb +0.32, character +0.47).
  Keeps the app free of a per-asset fudge table that would rot on re-export.
- the user owns art issues and fixes them in Blender (they fixed stray rotations when asked).
- **materials export with metallic 0.6-0.71**, and `brown`/`leaves` have near-black base colours.
  This renderer has no environment to reflect, so metallic is pure loss - see the trap documented
  in `core/Material.h`. Reported to the user rather than overridden in code.
- `default.frag`'s ambient is a hardcoded 0.1 with no app-side lever, so the app carries a **key
  sun plus a shadowless fill light**, as breakout and tetris do.

**Three predicates, not one flag:** `IsPassable` (water stops a walker, a bridge over it does not),
`BlocksBlast` (any block), `IsSoft` (hedge/wood burn; `wall_brick` never does). A blast arm reaches
INTO a soft block, destroys it and stops THERE; it stops BEFORE a hard wall. That one-tile
difference is the whole bomberman rule. `IsChoppable` is hedge only - the enemy has shears.

**An enemy must be able to DO something, and that took two changes not one.** `PlaceEnemies` refuses
a cell with no neighbour it can step into and no hedge it can cut. That alone was not enough: a
walled-in enemy reversed at a dead end, and `back` is the opposite of `facing`, so it flipped between
the same two directions for ever and never turned to look at the other axis - one sealed in by three
walls and a hedge stood facing the wall all round. `TickEnemies` now turns to face something it can
work on when even `back` is blocked, which has to be a RULE and not just a spawn check because a
blast can wall one in later. Measured over 200 seeds / 800 enemies: 15 born with no exit and 14 more
never moving, both now 0.

**`MazeWalker` carries everything that is true of a BODY; `Maze` keeps the board and the
statistics.** Settled 2026-09-16 because the walker is the thing that TRAVELS between levels, so
anything on it carries across for free. On the walker: position/facing/step, `f_alive`,
`death_ticks`, `chop_ticks`, `health`, `shield_ticks`, `invuln_ticks`, `f_has_key`, `score`. Left on
Maze: `deaths`, `items_taken`, `blocks_destroyed`, `blocks_cut`, `enemies_killed` - statistics,
whose per-level carry-across policy is a decision not yet made.

**Two bugs that split fixed, both invisible until measured:**
- `Maze::f_has_key` opened the door for WHOEVER asked, and `TickEnemies` asks through `CanEnter` -
  35 of 60 boards ended with an enemy standing in the player's exit. Now **`IsPassable` answers for
  the CELL and calls a door shut for everybody** (which is what generation, pruning, decor, the map
  and the view's item visibility all want), and **`CanEnter(walker, from, to)` asks the walker for a
  key**. An enemy never has one.
- **Two death clocks one letter apart** - `MazeWalker::death_ticks` (corpse drawn) and
  `Maze::dead_ticks` (respawn wait) - and the player's death set only the second, so the player's
  body vanished on the frame it died. One countdown now; the killer picks the length
  (`MAZE_DEATH_TICKS` enemy, `MAZE_RESPAWN_TICKS` player).

**`player = MazeWalker()` appears TWICE and they mean different things.** `NewGame` is the blunt
reset (a new board is a new lock); the respawn names what a death does NOT cost (the key, the
score). Anything earned added to `MazeWalker` later needs a line in the respawn AND a line in the
test, or every death wipes it silently.

**Walkers are shared by the player and up to `MAZE_MAX_ENEMIES` enemies** - they differ only in what
picks their direction. Each carries its own `step_total` so the two can move at different speeds.
Tile-to-tile with an atomic step, so "which tile" is always two integers.

**The view is built ONCE per field and then only shown/hidden.** `cell_block`/`cell_item` index the
objects by cell; `Maze::field_version` bumps on any cell change and `RefreshCells` walks the board
on those ticks only. NO object churn mid-game - which works because a cell's FLOOR never changes:
every destructible tile becomes GRASS and its floor was already the grass tile (the floor-variety
loop in `NewGame` skips soft blocks precisely to keep that true).

**Pickups are buried under soft blocks** and need no "revealed" flag - an item on an impassable
cell cannot be walked onto, so the tile above IS the lid. Five kinds: health, shield, and coin /
diamond / crystal worth 10 / 50 / 250 (`MazeItemScore`). `MAZE_ITEM_ORDER` is a FIXED LIST walked
front-to-back onto shuffled cells, so a full board buries exactly 4 coins and one of each other -
exact mix, random placement - and a board too small to carry the list loses the rare things at the
back of it first.

**Taken pickups shrink away and treasure turns, both as TWEENS in the view** - `TickPickupView`,
off `GetPhysicsTick()` so they freeze under `sim_pause`. The shrink is VIEW state
(`cell_item_shrink`), not a rule: the pickup is gone from the game the instant it is taken. The line
this app now draws: a motion with a SHAPE (the door swinging) is authored in Blender; a spin, a bob
or a fade is arithmetic in the app.

**The exit is `MAZE_TILE_DOOR`, and it is the one tile whose passability is a property of the GAME
rather than of the cell** - `IsPassable` tests `f_has_key` BEFORE it tests `IsBlock`. It stays inside
the `IsBlock` range on purpose, so a blast still stops at it, an enemy cannot cut it, and no soft
block is laid over it. `PlaceDoor` puts it in the border wall on a cell whose inward neighbour is
open, preferring one `MAZE_DOOR_MIN_DIST` from the spawn, and runs BEFORE `AddSoftBlocks` so nothing
is built on top of it (a soft block IN FRONT of it is fine - that is a bomb, not a lock).

**`MAZE_ITEM_KEY` is first in `MAZE_ITEM_ORDER`** so a board too small to bury everything still
buries the way out, and `AddItems` will not lay it under a block with no open cell beside it. That
was a real soft lock: a key nobody can reach looks exactly like a board you have not searched hard
enough. Walking through the door does NOT yet end the level - that is the remaining half.

**The door is two objects and the first unskinned thing animated from a clip** - `wall_doorway`
placed on `maze.door_x/door_z` with `door` attached as a child, `Door_Opening` on the ARCHWAY (see
[[animation-state-machine-in-object]]). Forward opens, `SetAnimationRate(-1)` shuts. THE VIEW
FOLLOWS THE RULE: `SyncView` compares `f_door_open` against `maze.f_has_key`, so `door.unlocked`
flips at once and `door.open` catches up over the fifty ticks the leaf swings.

**`equipped_shield` is a CHILD of `character`** with an identity local transform - the artist placed
it on the character in the .glb, so it follows, turns and is freed with them (`~Object` deletes
children). Visibility follows `shield_ticks`.

**The blast is drawn two ways** off one clock, `f_draw_tiles` (default) and `f_draw_cross` - see
[[volumetric-effect-gotchas]]. The user prefers per-tile and accepts that overlapping volumes do
not depth-sort (the engine has no answer yet); that is known and accepted, not a bug to hunt.

**Levels are a BLEND of three styles** (`MazeStyle`): classic bomber pillar grid, carved labyrinth,
and open plaza. Four jittered quadrants, dealt one of each style plus a free one so all three always
appear. A reachability flood fill from the spawn turns unreachable floor into wall and reports
`reachable_cells`.

**`pass_axis` is a generic per-cell "which way may this be crossed"**, not a bridge property. Bridges
set it (and take their model yaw from it, so a row lines up into one crossing); the walker checks it
at BOTH ends of a step, so you cannot step off a bridge sideways. Verified functionally, not by eye.

**`f_lock_human_input`** (panel checkbox + `bomber_lock_input` MCP tool) ignores keyboard/gamepad/
mouse so scripted tests are not disturbed - camera drift measured at 0.0000 with it on. ALWAYS TURN
IT ON before a scripted test; see [[engine-forward-is-minus-z]] for why.

**Object picking is opt-in**: an app must call `Application::CheckObjectSelection()` from UpdateView
or it silently has no selection. bomber now does. There may be a second, engine-side problem
underneath it - see the app's own `engine_notes.md`.

**`apps/bomber/engine_notes.md`** is the running list of engine gaps and workarounds the user asked
for - GetNodeScale, LoadGLTFFile returning void, Mesh bounds, no ambient setting, volume depth
sorting, picking opt-in, scripted-vs-real input, camera_set not capturing atomically. Add to it
rather than starting a new doc.

**`mingw32-make.exe rules` builds `maze_test.cpp` against `Maze.cpp` ALONE** - no core, no window,
no GPU, about a second - and checks every rule. RUN IT AFTER ANY CHANGE TO Maze: it is possible only
because `Maze.h` names no engine type, and it has already earned its place twice (it found a seed
generating a 14-cell board, and it caught a test that walked two tiles while asserting one). The
generator now re-rolls a layout scoring under `MAZE_MIN_PLAYABLE`.

**Verify rules through `bomber_state`, not screenshots** - tiles and counters are the instrument.
`bomber_input` only SUBMITS a hold and returns; on a free-running sim, reading state straight after
it is a race, and back-to-back holds simply OVERWRITE each other so only the last few land. Pair
each hold with a `sim_step` of the SAME length - `bomber_input(dir, ticks=20)` then
`sim_step(num_ticks=20)`, with 20 = `MAZE_STEP_TICKS` - and one call is exactly one cell. Anything
longer rolls into a second step and desyncs a planned path.

**A `bomber_restart` is a SimCommand and lands on a TICK**, so `sim_pause` immediately after it and
then reading the map gives you the OLD board. Restart, `sim_step` a few ticks, then read.

**Pathfinding off `bomber_state.map` must treat the item glyphs as solid.** A buried pickup is drawn
OVER the soft block it is under, so `+ S K c d x` are cells you cannot walk onto - a plan that
ignores that routes straight through a hedge and the walker stops dead on it.

**The enemy AND the player are SKINNED and animated.** The enemy has Walking/Chopping/Death on a
four-bone hand-named rig; the player was skinned 2026-09-17 onto `character_armature`, 27 MIXAMO
bones, and the file carries ONE clip - `Character_Idle`, and it really is an idle (4.5 s, upper legs
swinging 6-11 deg). `BuildCharacter` PROBES `GetAnimationNames` for which of
`Character_Idle`/`Character_Walking` exist rather than naming a fixed list, and SyncView picks the
walk when `step_ticks > 0` and falls back when `FindAnimation` is NULL - so **exporting a clip named
`Character_Walking` is the whole of adding a walk**. The character also came OUT of
`GetAssetsFromGLTF`'s list when it was skinned; leaving it there loads the skin a second time as a
plain mesh and keeps an unused copy for ever. Each enemy
needs its OWN Skeleton, bones and copies of every clip - a pose lives in the bones, and
`AddAnimation` binds a clip to the skeleton it is added to, so sharing would animate one of them.
Built once in `Init` on the RENDER thread (`BuildEnemies`), never in `RebuildField`, which is the
physics thread. `GetSkeleton` takes the SKIN name (`enemy_armature`), not the node name (`enemy`).
Needs `renderer->skinned_shader`, which is NULL by default and warns about nothing.

**Testing affordances worth knowing before writing a test by hand:** `bomber_give` lays a pickup on
the player's tile and lets the real `TickItems` collect it (it does NOT make one appear - pickup
objects are built per cell at layout); `bomber_state.field.items_shrinking` is the only evidence
the shrink ran, since it is over in 18 ticks; `door.leaf_moved` is the leaf's own local transform,
which nothing but the clip writes. **`object_list` is capped at 200 entries** and a laid-out board
is four hundred objects, so an unfiltered call quietly returns only the tail - use `name_filter`.
And a driver that walks the player somewhere must let the last step LAND: `tile` is the
DESTINATION of the step in progress and `TickItems` refuses to collect while `step_ticks > 0`.

**`Hallway.h/.cpp` is the level boundary** (built 2026-09-16): a 3 x 4..8 corridor with a doorway
at each end, its own tiny rule set, no tile array (the shape is a function of `length`). NOT a size
parameter on Maze and not a second Maze - the two are NEVER simulated at once, so the app holds a
phase and one of them ticks. It shares `MazeWalker` and `Maze::DirX/DirZ/DirOpposite` directly.

**The commit point** is the near door having FINISHED shutting - `Hallway::f_sealed` reports the tick
the player steps off the threshold, and the app waits `BOMBER_HALL_COMMIT_TICKS` (60, past the
50-tick clip) before throwing the old board away. Then `Maze::NewGame(seed, carry, entry_dir)` lays
out the next one and the corridor is SLID - not turned - so its far doorway lands on the new board's
entry cell, with the camera carried by the same translation.

**THE CORRIDOR IS NEVER ROTATED, as of 2026-09-17, and the rest follows from that.** It used to be:
the board had one fixed entry cell that had to be walked into heading east, so a corridor running any
other way was picked up and turned at the commit and the camera turned with it. That made the turn
invisible but left the VIEW pointing somewhere new every level while the keys stayed world
directions - which is the bug the user actually reported. `Maze::NewGame` now takes `entry_dir`, the
outward normal of the border walked in through, and derives `entry_x/entry_z` + `spawn_x/spawn_z`
from it (`MAZE_SPAWN_X/Z` are GONE - the spawn is a member). A pure translation is invisible to any
camera that shares it, so the camera can hold one world yaw the whole way. `Hallway::control_forward`
and the two-frame `InputToLocal` went with the rotation; one frame now, exact inverses.

**The camera is two modes and the game one is SOLVED, never nudged** - `BOMBER_CAM_GAME` (default)
from a yaw/pitch/distance/pivot every pass, `BOMBER_CAM_FREE` for the old middle-mouse orbit, toggled
by <C>, the panel, or `bomber_camera`. Yaw is pinned at 0 (camera due south, looking north) so
screen-up is always the forward key; the corridor only changes pitch and distance. **Pitch trades
against how much board is in frame** - square board, 16:10 window, 45 vertical FOV, so going
top-down spends board; 58 degrees at 19 out is the most overhead that still shows all 16x16 (62/17
cost a row and a half). The pivot LEANS a quarter of the way toward the player, clamped at 2.5.
**Under `bomber_lock_input` the game camera is frozen too**, or it would overwrite `camera_set` on
the next pass; `camera_set` also does not stick in GAME mode - switch to `free` first.
**`RiseBoard` brings the new board up** because the camera no longer always hides the swap - the
level falls away behind you and the next climbs out ahead; `EndHallway` finishes whatever is still
in the air. **`SinkBoard` drops the old board away**,
the pop-in run backwards, staggered by distance from the exit and squared so it accelerates; it runs
from the SEAL and must finish before RebuildField, and is applied as a per-tick DELTA (safe only
because SyncView returns early in the corridor). **`Hallway::f_next_ready`** is the app telling the
rules the next board exists - without it a fast walker on a short corridor reaches a far door with
nothing behind it.

Also open: sound
(`USE_SOUND` off - no wav yet), player animations, one bomb at a time, enemies that wander rather
than hunt, and no HUD. `apps/bomber/game_todo.md` is the user's own list - read it first.

**Lillies go on OPEN water only** (`MAZE_DECOR_LILLY`), never on a bridged cell, which falls out of
the decor byte: a bridge IS that cell's decor.

See also [[engine-forward-is-minus-z]], [[ship-orbit-camera-is-the-canonical-one]],
[[raymarch-volume-stage-plan]], [[per-app-build-layout]].

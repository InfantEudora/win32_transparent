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

**Walkers are a `MazeWalker` struct**, shared by the player and up to `MAZE_MAX_ENEMIES` enemies -
they differ only in what picks their direction. Each carries its own `step_total` so the two can
move at different speeds. Tile-to-tile with an atomic step, so "which tile" is always two integers.

**The view is built ONCE per field and then only shown/hidden.** `cell_block`/`cell_item` index the
objects by cell; `Maze::field_version` bumps on any cell change and `RefreshCells` walks the board
on those ticks only. NO object churn mid-game - which works because a cell's FLOOR never changes:
every destructible tile becomes GRASS and its floor was already the grass tile (the floor-variety
loop in `NewGame` skips soft blocks precisely to keep that true).

**Pickups are buried under soft blocks** and need no "revealed" flag - an item on an impassable
cell cannot be walked onto, so the tile above IS the lid.

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
it is a race. `sim_pause` then `bomber_input` then `sim_step` is the pattern that works.

**The enemy is SKINNED and animated** (Walking, Chopping, Death); the player still is not. Each enemy
needs its OWN Skeleton, bones and copies of every clip - a pose lives in the bones, and
`AddAnimation` binds a clip to the skeleton it is added to, so sharing would animate one of them.
Built once in `Init` on the RENDER thread (`BuildEnemies`), never in `RebuildField`, which is the
physics thread. `GetSkeleton` takes the SKIN name (`enemy_armature`), not the node name (`enemy`).
Needs `renderer->skinned_shader`, which is NULL by default and warns about nothing.

Still open: sound (`USE_SOUND` off - no wav yet), player animations, one bomb at a time, enemies
that wander rather than hunt.

See also [[engine-forward-is-minus-z]], [[ship-orbit-camera-is-the-canonical-one]],
[[raymarch-volume-stage-plan]], [[per-app-build-layout]].

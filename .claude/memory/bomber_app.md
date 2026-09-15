---
name: bomber-app
description: "apps/bomber - a bomberman on this engine; 16x16 maze from bomber_assets.glb, walking character, bomb with a fuse, raymarched blast drawn two ways"
metadata: 
  node_type: memory
  type: project
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T10:39:19.913Z
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

**Conventions:** green/`wall_brick` = indestructible, brown = destructible (not yet destroyable).
**Passability and blast-blocking are different predicates** - water stops a walker but not a flame;
a bridge over water makes it walkable. Walker moves tile-to-tile with an atomic step
(`MAZE_STEP_TICKS`), so "which tile" is always two integers.

**The blast is drawn two ways** off one clock, `f_draw_tiles` (default) and `f_draw_cross` - see
[[volumetric-effect-gotchas]]. The user prefers per-tile and accepts that overlapping volumes do
not depth-sort (the engine has no answer yet); that is known and accepted, not a bug to hunt.

Still open: destroying soft blocks, enemies, sound (`USE_SOUND` off - no wav yet), animations (the
character has none).

See also [[engine-forward-is-minus-z]], [[ship-orbit-camera-is-the-canonical-one]],
[[raymarch-volume-stage-plan]], [[per-app-build-layout]].

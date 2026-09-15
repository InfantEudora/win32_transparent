---
name: bomber-app
description: apps/bomber - volumetric explosion bench heading toward a bomberman game; two blast sites side by side (per-tile vs single cross volume) so the two approaches can be compared
metadata: 
  node_type: memory
  type: project
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T09:49:28.678Z
---

`apps/bomber` was created 2026-09-15 as a **volumetric-effects bench on the way to a bomberman
game**. Ground plane, two identical little maze patches, orbit camera, one retriggerable blast.

**THE TWO SITES ARE THE SAME BLAST DRAWN TWO WAYS, side by side, on one clock and one set of
knobs** - the user asked for both after pointing out that the real game has an explosion per tile
and my first suggestion (one cross volume that clips) was not how bomberman works:

- **left site (cell -4,0): one volume per tile.** 9 boxes sharing one mesh = one draw call. Each
  tile works out which tile it is from its OWN WORLD POSITION vs `blast_origin` (not gl_InstanceID,
  whose order across a batched draw the shader cannot assume), which gives it both its arm limit
  and its ignition delay. Follows the maze for free; tiles do not depth-sort against each other.
- **right site (cell +4,0): one volume shaped like the cross.** Density is distance to a
  cross-shaped skeleton of 4 segments. Composites perfectly, arms grow continuously; must be told
  its arm lengths, and marches a mostly-empty 12-unit box (hence double the view steps).

**One .frag, compiled into two Shader objects** (uniform_callback is per-Shader and both modes are
on screen at once). A ball is the degenerate cross with all arm reaches zero, so the shape code is
one path - do not fork it.

Conventions fixed: `BOMBER_CELL_SIZE` 2.0, `BOMBER_WALL_SIZE` 1.8, **green = indestructible,
brown = destructible**, `BOMBER_BLAST_RANGE` 2. `CellCentre()` centres the grid on the origin; the
real maze will want an origin corner, and that is the one line that changes. `ComputeArmLimits`
walks out and stops at the first wall - both wall kinds stop it today; when soft blocks can be
destroyed the flame occupies the block's own tile, which is `limit = step` instead of `step - 1`.

Still open: destroying soft blocks, a player, sound (`USE_SOUND` off - no wav yet).

See also [[volumetric-effect-gotchas]], [[ship-orbit-camera-is-the-canonical-one]],
[[raymarch-volume-stage-plan]] (the ship's clouds, which this descends from),
[[per-app-build-layout]].

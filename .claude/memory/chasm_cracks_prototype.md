---
name: chasm-cracks-prototype
description: "2026-10-12 testfx shader chasm_cracks.frag: the chasm shaped like cracked mud (plates, pull-apart matching walls, slumped-plate balconies); prototype only, not ported; user verdict pending"
metadata:
  node_type: memory
  type: project
  originSessionId: 867763b5-a94c-4688-a457-ce2b30a93ddb
  modified: 2026-10-13T09:15:51.187Z
---

Asked for a more natural chasm shape based on `art_source/chasm/cracked_desert.jpg`, tried in testfx.
Built `apps/testfx/assets/shaders/chasm_cracks.frag` (top-down map, game units/steps; `mode` 0
pull-apart / 1 eroded channel / 2 mud only; `view` 0 shaded / 1 game kinds / 2 height; `plot_grid`).
Idea: ground is Voronoi PLATES, each decided whole by its seed; in pull-apart every plate east of
the spine slides east, west slides west, so the walls match; straddling plates stay as islands
(4 steps), whole rim plates slump 2 steps (balconies), rim pieces crumble (fringe), side cracks
open along plate borders, deeper near the rift. HashInts/Noise copied bit for bit from ChasmLayout.cpp.
Sheet: `art_source/chasm/previews/cracks_sheet.png`. ~20 ms/frame at 1280x800 with 6 shadow taps.

2026-10-13, user: "looks so much like the picture"; asked for the 3D view. Added `view` 3 (mud) / 4
(kinds): heightfield march through the bench camera (engine.glsl iCam*, middle mouse orbits,
`world_scale` 0.01 bench units per world unit; whole map at `camera_set position [2.2,2.4,3.6]
look_at [0,-0.15,0]`), steps of `march_step` pixels (6: ~30 fps, 333 px differ from 3), bisect only
when the height changed, strata walls, steam under DECK_H inside the block. Sheet
`previews/cracks_3d_sheet.png`. The user is now trying settings - wait for their numbers.

Also fixed that session: `shadertoy_lightning.frag` lacked `#include "shadertoy_main.glsl"` (no main()).

**Why:** the spine + noise distance field gives blobby rims; the photo's look is plates.
**How to apply:** nothing in apps/chasm uses it yet. A port would evaluate the field per plot in
Terrain::BuildHeights and needs falls, ChasmRiftDistance, mountain tongue and sides-whole re-plumbed:
a big change, ask first. Related: [[chasm-terrace-columns]], [[chasm-cliff-kit]], [[testfx-bench-gotchas]].

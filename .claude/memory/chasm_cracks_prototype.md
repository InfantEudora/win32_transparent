---
name: chasm-cracks-prototype
description: "testfx shader chasm_cracks.frag (cracked-mud plates, pull-apart walls); PORTED 2026-10-18 as map kind cracks (MudCracks.cpp), terrain only; user verdict on the in-game look pending"
metadata:
  node_type: memory
  type: project
  originSessionId: 867763b5-a94c-4688-a457-ce2b30a93ddb
  modified: 2026-10-18T20:00:00.000Z
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

2026-10-18 PORTED as a TEST map kind, not the default generator (user asked for "a new map type"):
`GRID_KIND_CRACKS` "cracks", `apps/chasm/MudCracks.{h,cpp}` = shader Sample() mode 0 minus colour-only
parts, user's bench uniforms as CRACKS_* constants (plate 30, jitter 1.15, warp -0.14, rift_open 36.16,
rift_tip 1.11 = runs off the north edge, ...). Bench seed -7 = `chasm_generate {"kind":"cracks","seed":-7}`.
Terrain::BuildHeights samples it per plot then SettleHeights (saddles+pits, split out unchanged).
The bench's map depth follows its WINDOW (540x337.5 at 1280x800) vs the game's 544x305, so in-game
shapes differ from the user's screenshot; to compare like for like render the port at 540x337.5 zoom
1.13 (a scratch tool did; matched). Pit fill removes crack_far cracks and sub-plot crack ends.
Write-up: test_maps_plan.md "After: the cracks kind".

**Why:** the spine + noise distance field gives blobby rims; the photo's look is plates.
**How to apply:** making it the DEFAULT generator is still the big change - falls, rivers, mountain
tongue, sides-whole all key off ChasmLayout; ask first. Shader quirk kept on purpose: `far` uses
1.3*warp, so a negative warp shrinks the reach test (rare floor holes possible). Related: [[chasm-terrace-columns]], [[chasm-cliff-kit]], [[testfx-bench-gotchas]].

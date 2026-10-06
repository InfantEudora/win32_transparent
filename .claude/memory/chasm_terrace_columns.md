---
name: chasm-terrace-columns
description: "Chasm cliffs rebuilt 2026-10-06 as COLUMNS on the game's grid, all quads (blender_chasm_terraces.py -> chasm_terraces.blend); 12 levels of 6; patches under 8 plots bare + unbuildable; edit chasm_plots + run rebuild_terraces"
metadata:
  node_type: memory
  type: project
  originSessionId: 5ab57356-add0-49ca-8a99-acb33d9b1338
  modified: 2026-10-06T17:31:01.726Z
---

Built 2026-10-06 because chasm_cliffs.blend ([[chasm-cliff-kit]]) looked right but was welded,
triangulated loose faces the user could not edit. `apps/chasm/tools/blender_chasm_terraces.py` ->
`art_source/chasm/chasm_terraces.blend` + `previews/terraces_{game,overview,oblique,close,wire}.png`.
`--force` rebuilds from scratch, `--render [views] [--quick]`, `--render-only` re-renders the saved file.
User verdict on round 1: "quite a bit neater".

The idea: a game PLOT is a fine vertex drawn as its dual polygon (grid_plan.md section 2), so a column
per plot at that vertex's height IS the game's data - levels become heights. Grid = Grid.cpp's method
in Python (lattice side 8, two subdivide+relax levels, ~1.8-unit plots). Tops = corner quads; walls
along plot boundaries cut into GLOBAL rings (every top's turf/soil lines + every stratum), so every
edge has 2 faces; saddles joined by raising, as TerrainMesh.cpp does; the look is only ring offsets
(blocks from banded position noise, soft strata recessed, bulge at edge mids, groove at joints, LIP).

User rules, round 2 (same day): **12 levels, not 24** - STEP 6 (plateau 0, balcony -12, island -24,
floor -72; strata bands follow STEP). **A flat patch under ~8 plots (MIN_TERRACE_PLOTS) is bare rock
in ONE brown (PAL_ROCK_3), no turf, and unbuildable** - find_terraces() by grid-edge connectivity at
equal height; rebuild writes `terrace_plots` + `buildable` point attributes on chasm_plots (the rule
the game would use). ~180k quads, 46 rings; 69 buildable patches, 302 bare.

`chasm_plots` (hidden, wire) is the ONLY input: z per vertex. Edit it, run the `rebuild_terraces`
text block; heights snap to 0.5 and saddle fixes are written back. Rivers' lines live in
`chasm_plots["rivers"]` (water clipped to them; bed plots are wider = muddy shore). Clouds/steam are
VOLUMES in the preview only (opaque puffs read as boulders from above; emission must be tied to
density or long views go white). Previews render in ~1-3 s each.

**Why:** user wants an editable quad model that can later drive the game's terraces/levels, and a
less boring rim. **How to apply:** user said "update it in Blender first" - port (mesher into
TerrainMesh.cpp as dual columns instead of marching squares, levels generalised to heights, the
8-plot buildable rule) comes after they approve the Blender version.

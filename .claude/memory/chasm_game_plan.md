---
name: chasm-game-plan
description: "apps/chasm top-down colony sim on a Townscaper-style irregular quad grid; decisions so far (painting zones, palette texture, scale, replay-from-state)"
metadata:
  node_type: memory
  type: project
  originSessionId: 9ee540f3-21fa-4533-a07c-ca156fde207e
  modified: 2026-10-04T14:13:34.698Z
---

apps/chasm (empty as of 2026-10-04) is a planned top-down colony sim, Anno-like but low poly. One
huge valley split down the middle by a deep chasm with lower shards/terraces inside it; mountains
+ frozen chasm end north (impassable), swamp/desert south. Reference art in art_source/chasm/
(A Little Age demo screenshot, a mashup of it with the chasm, a painted concept).

Decided with the user 2026-10-04 (exploratory phase, no code yet):
- Grid: Townscaper-style (hex of triangles -> random merge to quads -> subdivide -> relax),
  player clicks vertices, quads render via 8-corner module lookup + deformation; Bad North
  precedent for terrain levels/cliffs.
- Buildings are PAINTED as zones over plots, not placed as fixed footprints; capacity by area.
- Art: no textures, everything uses ONE palette texture; nearly everything procedurally generated,
  only some props (trees) modelled and placed.
- Scale: map ~3x3 screens at max zoom-out = ~100k fine cells (ballpark; changes only for playtime).
- Replay from a saved game state must work like archer's (state hash per tick, bit-exact within a
  build) - so save/restore of state is a day-one requirement, not a later feature.
- Camera: A Little Age's screenshot is our MAX zoom-out; ONE level of detail, no far LOD.
- Plans written: apps/chasm/docs/README.md + grid_plan.md (6 steps, grid generator first).
- STEP 1 BUILT 2026-10-04: exe is build/chasm_nophysics.exe (USE_PHYSICS=0); run on --mcp-port 8769;
  tools chasm_grid/generate/view/check; seeds 1-3 hashes pinned in Grid.cpp, same debug+release.
  STEP 2 (picking, GridPick.*) BUILT same day; a worker window fixed the zig-zag outline
  (Grid::lines + GridLevel::pin sliding vertices - reuse for the step 3 rim). Two agents share
  build/chasm_nophysics.exe: build, then COPY to build/chasm_<id>.exe and run the copy, or the
  running exe blocks the other's link.
  STEP 3 BUILT same day: worker pinned rim+shard lines (Grid::lines[feature_line_base+i], smoothed);
  Terrain.* levels + TerrainMesh.* (marching-squares cells, strata walls, skirt, 144 chunks), sun
  shadows following the view. Objects have only 4 material slots - palette (step 4) replaces them.
  Engine HAS instancing (Renderer instancedata SSBO) - I wrongly said it didn't once.
- RRandom IS seedable now (Generate(seed)); world gen should use its own instance.

**Why:** the user wants the building mechanic tested before any gameplay goes on top.
- Testing: NO separate engine-free `make rules` build (user's call, unlike archer/bomber). Checks
  and debug views are built into the app as commands (debug panel + MCP), `#ifdef DEBUG` only, so
  failures can be SEEN; fewer rules than archer.

**How to apply:** prototype stepwise, step 1 = app + grid generator + grid debug view together; see [[archer-app]] for the replay/state-hash pattern and
[[replay-determinism-plan]].

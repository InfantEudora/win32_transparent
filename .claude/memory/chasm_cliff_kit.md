---
name: chasm-cliff-kit
description: "Chasm cliff kit generator (apps/chasm/tools/blender_chasm_cliffs.py) - aigen_1-style cliffs as a FRESH rift; user feedback rules (no flowery/scalloped, jagged, cracks inland, matching walls, spike rocks); arch still weak"
metadata:
  node_type: memory
  type: project
  originSessionId: 9137fc9f-59e8-49dd-b089-c7efc39e3e64
  modified: 2026-10-06T12:54:02.803Z
---

Built 2026-10-06: `apps/chasm/tools/blender_chasm_cliffs.py` -> `art_source/chasm/chasm_cliffs.blend`,
the "Export" collection -> `apps/chasm/assets/meshes/chasm_cliffs.glb` (nothing in game loads it yet),
`--render` -> `art_source/chasm/previews/cliffs_{game,oblique,arch,kit,props}.png`. Rebuild needs `--force`.

The target is the CHASM in chasm_aigen_1.png, not its houses. User's feedback on round 1 (same day):
- **too flowery** (rounded chamfers -> scalloped edges, radial island tops). Wanted: jagged, angular,
  "as if the crack appeared in a short amount of time". Now: fractured 2-3 facet column profiles,
  zig-zag sharp rim, V fissures inland + surface cracks with torn turf lips, the opposite wall is the
  far one mirrored (matching_wall), islands/terraces are tilted sharp-cornered blocks.
- **rim need not follow the grid** - nothing is buildable on the absolute edge (the game enforces
  square edges; the user is open to relaxing that visually).
- wanted **long spiked rocks as boulders**: spike/spike_cluster/fallen_shard/spire. Smooth tapers read
  as horns/carrots - keep them blade-like, thick, kinked.
- **palette less dull**: row 0 rock/earth/grass/lip warmed in palette.png 2026-10-06 (revert = git checkout).

Round 3 (same day, user: "all next steps, but keep it in Blender for now"): plateau is a separate
ground mesh with holes (wall strips only reach 6-7.5 back - long strips crossed at sharp corners and
covered the rivers); rivers are channels with banks; notch = exact slot column; the arch was replaced
by natural_bridge (rim -> pillar, island <-> island); 14 prop variants; `rift()` builds a game-scale
800x450 map (one crack, walls matched via across()) as the stand-in for porting to TerrainMesh.cpp.

**How to apply:** previews only read with the Stage's depth-fog volume + EEVEE AO; the far wall faces
away from the NW sun. Every hole the ground leaves must be covered by something reaching past it by
more than one ground triangle (strips, banks, crack neighbours' deep backs) or the sky shows through.
Nothing is wired into the game yet. Related: [[chasm-game-plan]].

Round 4 user verdict: walls, natural bridge, spires and levels "the best reference so far"; boulders were
too spiky and too scattered -> now stout broken-topped shards in rubble heaps just behind the rim.
Open question put to the user: real terrain cracks (fine vertices set to the floor level, so
TerrainMesh's marching-squares walls build them) for the wide part + decal props for the hairline
tails; stencil cut-outs not recommended (no stencil anywhere in core, deferred + SSAO + shadows).

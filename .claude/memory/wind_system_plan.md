---
name: wind-system-plan
description: "archer wind: 2D stream-function field built from the blocks (Wind.{h,cpp}) + debug view (WindView, archer_wind MCP) BUILT 2026-09-26; foliage + vine-leaf sway and drifting leaves (Leaves.cpp, 150% padded, by density) + streaks (Streaks.cpp) BUILT; next fireflies; leaves/grass lead the look, she is NOT pushed; plan in apps/archer/wind_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: b1199cf9-4258-42bc-98ed-184bce0a1618
  modified: 2026-09-26T13:40:16.169Z
---

Agreed with the user 2026-09-26 (apps/archer/wind_plan.md): one 2D wind field in the play plane,
built from Stage::blocks like the terrain; leaves and grass carry the look, streaks subtle but
visible; wind does NOT push her (maybe a balance input later, not now); one leaf model, varied in
tint/size, caught in eddies; fireflies emissive + a few real point lights as one group.

BUILT: `Wind.{h,cpp}` (engine-free, `wind_test.cpp` 31 checks, run by `make rules` as its own exe).
Stream function = potential flow (SOR once per level, cached by block hash) + per-corner eddies
(one BOUND + two shed) + sine-wave turbulence, both under Bridson's wall ramp; gusts multiply the
velocity. Pure function of the tick. End walls (tall thin blocks at the outermost x) are left out.

DEBUG VIEW BUILT 2026-09-26: `WindView.{h,cpp}` (line mesh slid along the eye ray to z 2 so it
overlays the z=0 plane), panel "Wind", `archer_debug_view` `wind`, `archer_wind` (tuning, `sample`
[x,y], stats, corners, view_ms). UpdateWind runs from PreRender only while shown; blocks copied in
AtTickBoundary, field under wind_mutex for the MCP thread. Next: foliage sway (core material hook).
Eddies are sized by min(drop, exposed downwind face) - island eddies used to hang in mid-air.

FOLIAGE SWAY BUILT 2026-09-26: core material_t::wind_flex + Renderer::SetWindField (SSBO binding 7,
header carries mapping - no texture unit, no uniforms) + default.vert WindBend. ALL GLSL material
mirrors must spell `float wind_flex` or the program fails to link. UploadMaterials binds each
texture once, so a plant's renamed material copy (`<name>@<node>`) is free - needed because grass
shares atlases with tiles/props. WindField::Bake = Velocity on a grid, 0.4 ms/frame.
leaf_small has an unapplied node transform (scale 3, +90 X).

LEAVES BUILT 2026-09-26: Leaves.{h,cpp} + leaves_test (physics-thread step under wind_mutex, pool of
400 Objects on one re-baked leaf_small mesh). material_t::wind_mode (0 stalk / 1 leaf, every GLSL
mirror spells it) makes vine leaves flutter. Bound eddy is now an ellipse (WindEddy::stretch 2).
leaf_small export has NO texture/colour - tinted via material colour; tell the user if re-exported.
Leaves respawn on the side OPPOSITE the one they left by.

STREAKS BUILT 2026-09-26: Streaks.{h,cpp} + streaks_test; render-thread sim in UpdateWind, custom
shader assets/shaders/wind_streak.*, alpha in normal.x. Leaf swarm now density-based over the view
padded 150% (user asked; the plant grid stays 25% since it is rebuilt every frame).

**Why:** the user wants visuals that tie into gameplay later; a deterministic field can move into
Stage when the balance mechanic needs it.

**How to apply:** don't use ParticleEmitter for leaves/fireflies (Object per particle, shared
RRandom); bake N copies into one mesh via the custom-shader pass. Related: [[archer-app]],
[[plant-mechanics-plan]], [[archer-terrain-plan]].

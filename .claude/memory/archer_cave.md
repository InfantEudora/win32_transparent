---
name: archer-cave
description: Archer cave BUILT 2026-09-29 left of the terrain bay (apps/archer/docs/cave_plan.md) - own floor block so the bay's bank is untouched; blocks reaching back into the bank raise it (closes the cave); next is lighting it
metadata:
  type: project
---

The cave: x -66..-40, roof 9..11, far wall, mouth lip (the old bay left wall, same index), its own floor block appended last. Terrain regions start at ARCHER_CAVE_X_MIN. Backdrop rule: a block with Back() at or behind the bank's wall face raises the columns behind it to roof_rise over its top. The waterfall's stream runs into it (x -62).

**Why:** the user wants an enclosed place to experiment with lighting ([[archer-water]] came first).

**How to apply:**
- Next step is lighting it: the camera-side fill light casts no shadow and still lights the inside; the bank's hazed materials read as distance inside a cave. Both are open, in cave_plan.md.
- Extending the bay's own floor was rejected because it re-lays the bank's column grid and moves the hand-placed vines on its crest.
- BIOMES (2026-09-30): StageBiome boxes in Stage.h (looks only, not zones); readers FoliageBiomeFor / BoulderBiomeFor / WindBlocks; jungle rules are exact no-ops (cave_test checks plant-for-plant). Still air = the box is solid to the wind. Two bay rock clusters at x -31 overlap - pre-existing, clusters never check each other.
- wind_test's "no jets" now measures with gusts off; gusts start at the domain's left edge, so moving that edge moved them and a one-tick sample failed.
- VISION (2026-09-30): UIOverlay::AddVignette (soft ring, closes past zero to black; vertex gained `soft`) is built, first used by archer's title fade (ScreenFade: close 1.5 s with the title music, switch at black, hold 0.35 s for the horn, open 1.4 s; wall clock, presentation only). Next is a per-biome vision size drawn UNDER the HUD. The user wants light and the vision mask kept separate. cue_replay.py now waits for archer_state.on_title to clear.
- LIGHTS (2026-09-30): the renderer has ONE shadow map, from the first visible directional light (sun, then fill), applied to EVERY directional light in lighting.glsl. Sun and fill both follow the view now (FollowView, LightFollow offsets, archer_lights tool + Archer panel Lights section). Cave vision vignette built (BiomeVision, archer_vision, under the HUD, on the scene viewport not the window).

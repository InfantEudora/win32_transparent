# Water

A waterfall in the terrain bay, cheap and flat-shaded: no reflection, refraction or
transparency. Moving colour bands on simple meshes, lit like everything else, and foam balls
that rise, drift and shrink. Built 2026-09-29.

## Where it is

One `StageWater` in `Stage::BuildMainLevel` (x -26). Looks only; the rules never read it.

- **Notch.** `Backdrop.cpp` cuts the bank's wall down to `lip_y` (12.5) over the fall and sets that
  stretch back by `notch_recess`. The lip is where the island hides it from the ground camera:
  cut at 8.5 (under the island), it showed as a pale window onto the painted backdrop.
- **Upper sheet.** Over the lip, down into a pool on a rock shelf at `basin_y` (2.6). Its arc is
  worked out from where it has to land (`WaterParams::land_at`), not fixed.
- **Pool.** Shelf, rims and a dam, meshed as their own object (`water_rocks`) with the ground's
  sharper mesher settings. The bank's 0.6 rounding turned half-unit rims into blobs.
- **Spill.** A short sheet out through a gap in the front rim, down into the gap behind the ground.
- **Stream.** Flat, just under the grass, running left to `stream_x_end` (-39.8, behind the bay's
  left wall). **Extend it into the cave when the cave exists.** It must stay within the ground
  block's length, or its front edge is out in the open (`water_test` checks).
- Ridges along the stream are set back to leave it a channel; none stand in the pool's cleft;
  no pine grows in the notch.

## How it is drawn

- `Water.{h,cpp}` is engine-free: the layout, rocks, surfaces and foam. `water_test.cpp`
  (`make rules`) checks every edge of every flat surface is inside rock or the ground.
  A plane edge in the open is a floating strip of blue that a screenshot only finds from one angle.
- `ApplicationArcherWater.cpp` holds the objects. `RebuildWater` runs from `RemeshBackdrop`;
  `UpdateWater` steps the foam from `PreRender`, catching up ticks like the streaks.
- `shaders/archer_water.glsl` is one body with two entry files, `_sheet.frag` and `_flat.frag`.
  The sheets' ragged edges are a `discard`, and a program that can discard loses early-z. With both in one
  program, the stream hidden under the grass ran the whole light loop: 1.1 ms of GPU, against
  0.09 out of view. Split, measured 2026-09-29 with `renderer_timings`: the custom-shader pass
  is 0.35-0.40 ms with the water in view, 0.025 ms without. If it ever needs to be cheaper still,
  the light loop is where the time goes, not the pattern.
- **Lit through `shared_assets/shaders/lighting.glsl`**, split out of `default.frag` unchanged.
  A custom shader calls `SelectMaterial()` then `LightSurface(albedo)`, and sets
  `Shader::f_lit` so `Renderer::UploadLighting` hands it the sun's matrix and the rest.
- The water is not in the G-buffer, so the flat shader reads the rock below it there. A small
  height difference is the foam shoreline, round every rim and bump, with nothing baked in.
- Clock: simulation ticks (`water_seconds`), so it freezes under `sim_pause`.

## Next

- **Sound.** `assets/sound/waterfall.wav` is in. A looping cue on the `ambience` bus with gain
  and pan by distance from x -26. It needs a scope begun by the game, so it enters the cue log
  and the `archer_test` baselines move: re-check with `tools/cue_replay.py`, then `--write`.
- Mist puffs at the foot of each sheet (the fireflies' glow shader, low strength); the wind
  pushing foam and mist; an arrow into the pool throwing a burst of foam.
- The cave: the stream runs into it; Bomber's Worley caustic net as light on the cave's ceiling.

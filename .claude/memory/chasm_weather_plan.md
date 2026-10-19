---
name: chasm-weather-plan
description: "BUILT 2026-10-18: chasm winter storm with the snow front - core wind field XZ plane, tree sway, snow + spindrift shader, gust stinger once, smoke/suspense/light/birds; apps/chasm/docs/weather_plan.md 'As built'; archer sway not yet looked at by the user"
metadata:
  node_type: memory
  type: project
  originSessionId: 6976ce71-6dbf-4cdd-a612-bf0f3b1b7cda
  modified: 2026-10-19T09:19:56.408Z
---

Plan + as-built: apps/chasm/docs/weather_plan.md. All six steps built and checked 2026-10-18 on a muted test copy
(camp_3, village_year5). Code: Weather.{h,cpp} (storm = active(date) x near(front line)), ApplicationChasmWeather.cpp,
assets/shaders/chasm_snow.* (snow cells round the camera; spindrift = same program, snow_band 1, flat cells on the
front's line), LeanSmoke in ApplicationChasmHearth.cpp, sun dim in UpdateSun, suspense in UpdateMusic, cue row
winter_gust, BirdPose::tree/ride + SwayOffset.

Core change: Renderer::SetWindField(..., plane = WIND_PLANE_XY), default.vert WindBendGround. Archer passes nothing
new; the user still has to look at archer's sway once (core-change rule).

Tuning/test: Weather panel under Time; chasm_time `weather` {storm 0..1 holds the front through the view, held:false,
strength, gust:true}. Holding 0 then 1 re-arms and fires the big gust each time (expected).

User 2026-10-18 after trying it: "adds quite some drama", the stinger "clear and sounds nice"; and the weather should
FREEZE in a pause like everything else ("clearer") - done: the weather clock is real seconds while running, still in a
pause (keeps natural pace at 4x); the big gust waits for the pause to end.

2026-10-19, user: all flakes "jump every few seconds" with a still camera. Cause: snow_drift wrapped at one cell while
each flake took 0.85-1.15 of it, so every wrap jumped them all up to 2.4 units. Fixed: SNOW_DRIFT_STEPS (wrap at 32
cells, shares in 32nds); also the spindrift. Header comment ApplicationChasm.h:1601 still says "wrapped to a cell"
(another agent held the file).

**Why:** user's ask - winter arrives with wind, snow and the gust stinger, then a serene still winter.
**How to apply:** in chasm everything moving holds still in a pause (water/mist/smoke on SimSeconds, weather on its
own clock) - I once told the user the opposite; keep new effects that way. Solid opaque puffs read as snowballs - use the flake
shader for snow-like effects. Debug 4x screenshots lag the sim; judge motion at 1x. Related: [[wind-system-plan]],
[[chasm-sound-plan]], [[chasm-birds-plan]], [[adaptive-music-plan]].

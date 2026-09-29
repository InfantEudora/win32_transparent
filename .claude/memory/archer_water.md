---
name: archer-water
description: Archer waterfall BUILT 2026-09-29 (apps/archer/water_plan.md) - lit custom shaders via shared lighting.glsl + Shader::f_lit; one discard kills early-z for a whole program
metadata:
  node_type: memory
  type: project
  originSessionId: 0fd25e4c-7d58-4731-aaa1-09798aee4cfb
  modified: 2026-09-29T20:16:47.587Z
---

Archer's waterfall in the terrain bay (x -26): notch cut in the bank, sheet into a pool on its own rock mesh, spill, stream behind the ground running left toward the future cave. Engine-free Water.{h,cpp} + water_test.cpp; objects in ApplicationArcherWater.cpp; see apps/archer/water_plan.md.

Core changes made for it: `shared_assets/shaders/lighting.glsl` split out of default.frag (SelectMaterial, LightSurface(albedo), CalcPBRLighting), `Shader::f_lit`, `Renderer::UploadLighting` (now also used by the colour pass for default + skinned). Other apps not built or checked, by the user's choice ([[core-changes-other-apps-separately]]).

**Why:** the user wanted cheap, flat-shaded water, lit like the rest of the scene.

**How to apply:**
- A `discard` anywhere in a program disables early-z for all of it; the water was 1.1 ms until the sheet (discard) and flat (no discard) were split into two entry files; after the split it is 0.35-0.40 ms GPU in view (measured 2026-09-29). Rules, water_test and archer_test replay all passed after it.
- Waterfall loop DONE: `waterfall` scope from SignalCues; CueSystem follow gained per-part values, a `pan` part and moving `distance`/`dx`; CueSystem::Reset now logs its marker AFTER ending scopes (a loop open across a replay start made cue_replay flaky). Open: mist, extending the stream into the cave.
- The lockd broker was seen with ALL locks gone mid-session (restart or console release-all), and someone ran their own archer from apps/archer, killing the port-8768 instance: check lock_list before a long replay.
- Leases lapse in long sessions, and another agent took #port:8768 and #build mid-task ([[lockd-lease-expires-in-long-runs]]); re-claim before each build/run.

---
name: vitals-plan
description: "Archer vitals (exertion + fear -> breathing + heartbeat) AGREED and BUILT 2026-09-27, apps/archer/docs/vitals_plan.md; sound only until the user decides how they change play"
metadata:
  node_type: memory
  type: project
  originSessionId: 1f0cffad-5fd3-4645-8f64-22d1c6e84e4e
  modified: 2026-09-28T18:19:48.503Z
---

2026-09-27: agreed and built the same day. Plan and "Built" section: apps/archer/docs/vitals_plan.md.

What is where:
- Stage::vitals (StageVitals, VITALS_* defines), stepped by Stage::TickVitals last in Stage::Tick; Stage::DropBelow is the column scan; TestVitals in stage_test.cpp.
- ApplicationArcher::SignalBody holds the breath and beat clocks (BREATH_* defines); SignalFootsteps returns its count. The recording state has `vitals` and `body_clocks`; archer_state reports `vitals`.
- archer.json has the breath_in/breath_out/heartbeat cues, the `body` bus, and priorities in the `her` group (grunt 2, lines 1, breaths 0, busy interrupt).

User answers: no other fear sources for now, but "almost falling off an edge" (they have a teeter animation, no system yet) should add a fear burst once it is a rules state. Exertion and fear WILL change her behaviour, but it is undecided how, so for now only sound reads them. The files are breathe_in_normal_1..2, breathe_out_normal_1..3 (an in/out pair, only "normal" so far; heavy later) and heartbeat_1..2.

Added the same day: top-right HUD card (DrawVitalsHud: pulsing heart dot + bpm, exertion bar), 20 s graphs at the top of the Cues tab, holds via ARCHER_CMD_VITALS / MCP `archer_vitals`. Not built: the heartbeat duck (a cue ducks through a group, which plays one sound at a time; ambience is empty).

2026-09-28: blinks added as a third SignalBody clock (face Blink key 1). User's rule: blink interval random and correlated to NOTHING (no vitals), so it signals no cue and draws from CueHash01 on the level tick; clocks in body_clocks (5 entries now, 3 still load).

2026-09-28: breathing SEEN - `chest` leaf bone scaled from the breath clock (user chose option A: out-breath at 0.35 of the cycle, min 28 ticks, so the chest's inhale lengthens at rest). Chest scale is applied one tick late (engine poses before the tick) - step 2+ ticks before judging a change.

**How to apply:** tune by the defines and the table; verify with `make rules` and tools/cue_replay.py, plus scripted probes on the test ground (archer_zone "test") and a drop via archer_place. See [[cue-plan]], [[adaptive-music-plan]].

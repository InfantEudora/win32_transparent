---
name: vitals-plan
description: "Archer vitals (exertion + fear -> breathing + heartbeat) AGREED and BUILT 2026-09-27, apps/archer/vitals_plan.md; sound only until the user decides how they change play"
metadata:
  node_type: memory
  type: project
  originSessionId: 1f0cffad-5fd3-4645-8f64-22d1c6e84e4e
  modified: 2026-09-27T18:02:23.224Z
---

2026-09-27: agreed and built the same day. Plan and "Built" section: apps/archer/vitals_plan.md.

What is where:
- Stage::vitals (StageVitals, VITALS_* defines), stepped by Stage::TickVitals last in Stage::Tick; Stage::DropBelow is the column scan; TestVitals in stage_test.cpp.
- ApplicationArcher::SignalBody holds the breath and beat clocks (BREATH_* defines); SignalFootsteps returns its count. The recording state has `vitals` and `body_clocks`; archer_state reports `vitals`.
- archer.json has the breath_in/breath_out/heartbeat cues, the `body` bus, and priorities in the `her` group (grunt 2, lines 1, breaths 0, busy interrupt).

User answers: no other fear sources for now, but "almost falling off an edge" (they have a teeter animation, no system yet) should add a fear burst once it is a rules state. Exertion and fear WILL change her behaviour, but it is undecided how, so for now only sound reads them. The files are breathe_in_normal_1..2, breathe_out_normal_1..3 (an in/out pair, only "normal" so far; heavy later) and heartbeat_1..2.

Not built: a sim command to pin a value, the heartbeat duck (a cue ducks through a group, which plays one sound at a time; ambience is empty), and graphs in the cue panel.

**How to apply:** tune by the defines and the table; verify with `make rules` and tools/cue_replay.py, plus scripted probes on the test ground (archer_zone "test") and a drop via archer_place. See [[cue-plan]], [[adaptive-music-plan]].

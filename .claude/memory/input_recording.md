---
name: input-recording
description: "Input record/replay BUILT 2026-09-25 (F9/F10, input_record/input_replay, recordings/*.rec text files); replay input is tick-exact but archer runs diverge because animation state is not restored"
metadata:
  node_type: memory
  type: project
  originSessionId: 333e7cd8-0bfc-473a-a24b-63891d98371c
  modified: 2026-09-25T11:35:54.539Z
---

Built 2026-09-25 at the user's request: they capture scenarios faster by hand (controller/keyboard) than by script, and want those as replayable test cases. Later the same stream is meant for multiplayer. First slice of step 7 in [[deterministic-sim-plan]] — record/replay, NO state hash yet.

- Core: `InputController::BeginRecording/EndRecording` (hook = `DrainAndApplyEvents`, each event stamped with the sim tick it applies before, relative to start), `StartReplay/StopReplay` (injected in `ApplyTickInput` beside scripted holds, counts as `HasSyntheticHolds` so it drives an unfocused window). `Application::ServiceInputRecording` runs every pass between `BeginPass` and the tick; `Request*` calls are any-thread. Keycodes `INPUT_RECORD_TOGGLE`/`INPUT_REPLAY_TOGGLE` (plain `INPUT_RECORD` collides with the Win32 console struct).
- Format `core/InputRecording.h`: text, one event per line, header with `begin`/`end` — the user explicitly wanted trimming to be a MANUAL edit of the output file, so trimming = edit those two numbers.
- Pad: archer maps Back (Start is restart; Tetris uses Back, so core maps no pad button).
- App hooks `CaptureRecordingState`/`RestoreRecordingState`; archer restores via NewGame + PlaceArcher + facing/aim/vel/on_ground.

**Open finding:** with identical recorded input, the archer replay ends ~0.03 units from the original, and replays alternate between two results (7e-4 apart). Trace showed equal `vx` but different distance while starting to walk → root-motion/animation (Puppet/ObjectAnimation blend and clip phase) is not reset by NewGame or the restore. Not fixed: the user was actively editing those files. Also: recordings without `restart` depend on prop state (crates sit right beside the start area).

**Why:** a fix belongs in the app's restore, not the recorder — the recorder's input stream was verified exact.
**How to apply:** when judging replay fidelity, compare replay-vs-replay; if asked to tighten it, reset the Puppet/animation state in `ApplicationArcher::RestoreRecordingState`.

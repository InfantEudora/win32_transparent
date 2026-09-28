---
name: replay-determinism-plan
description: "Replays not reliable yet (state leaks between replays, animation not restored, live key-ups reach replays); plan in docs/replay_determinism_plan.md - end-of-tick bit-exact per-component state hash; archer code waits for the bridge agent"
metadata:
  node_type: memory
  type: project
  originSessionId: 2e3d945b-d959-4cf6-b015-2e9c5cf4eed0
  modified: 2026-09-28T18:35:11.201Z
---

2026-09-28: user insists replays must be reliable ("that is the whole point of the determinism"). Plan written in docs/replay_determinism_plan.md and agreed in outline.

User decisions/answers: hash need not be every tick in the recording itself; I recommended END of every tick (end N = start N+1), with a start-of-tick hash only as a diagnostic for out-of-tick mutation. Bit-exact, never rounded. Compiler flags: no -ffast-math/-march/-mfma, -std=c++17 ISO => fp-contract off, SSE2 => debug and release PROBABLY equal, test once.

Findings: (1) second pass in one app differs, fresh apps agree; (2) Puppet/animation state not reset by RestoreRecordingState (known since [[input-recording]]); (3) WRONG at first: live key-ups are filtered during replays; the stray arrow came from the focus-loss release-all (fixed, see below); (4) auto-repeat downs are inert (ApplyTickInput ignores down on held key).

2026-09-28 evening: core steps BUILT - core/StateHash.h, Application::HashSimState (virtual; default = `bodies` part with velocities + `objects` part), TraceTickStart/End around the tick, MCP `replay_trace` (tick_starts, detail), cue_replay.py writes/compares recordings/<name>.trace (--tick-starts, --detail). Focus-loss release now skips replay-owned keys (the real cause of the stray arrow; live key-ups were ALREADY filtered - my first theory was wrong). MEASURED: fresh-vs-fresh `bodies` identical every tick of all 7 recordings; warm/reversed part at tick 1-2 first in NewGame-rebuilt props (crate_0/1, target_6/7, stand_5/8) => the rp3d world surviving restart is leak #1; fix = fresh physics world on restart. Default `objects` part is useless for archer (view-driven show). 140425 .cues now differs from a fresh app because of the bridge agent's Stage changes - rewrite after the bridge lands.

Same evening: Object::f_visual_only (SetVisualOnly/IsVisualOnly) - the default hash skips the object and its subtree. USER RULE: leaves/wind are pure visual and must NEVER tap into rrand (audited clean 2026-09-28: archer has no RRandom calls, no ParticleEmitter, no rand/<random>). Archer still has to mark its show objects (after the bridge agent).

**Why:** cue_replay only compares sounds, so it hid divergence.
**How to apply:** core steps (replay ignores live keys, hasher + HashSimState hook + replay_trace, cue_replay .trace) may go ahead; anything in apps/archer waits until the bridge agent is finished - check lockd first.

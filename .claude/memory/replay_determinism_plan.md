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

2026-09-28 evening: core steps BUILT - core/StateHash.h, Application::HashSimState (virtual; default = `bodies` part with velocities + `objects` part), TraceTickStart/End around the tick, MCP `replay_trace` (tick_starts, detail), cue_replay.py writes/compares recordings/<name>.trace (--tick-starts, --detail). Focus-loss release now skips replay-owned keys (the real cause of the stray arrow; live key-ups were ALREADY filtered - my first theory was wrong). MEASURED: fresh-vs-fresh `bodies` identical every tick of all 7 recordings; warm/reversed part at tick 1-2 first in NewGame-rebuilt props (crate_0/1, target_6/7, stand_5/8) => the rp3d world surviving restart is leak #1; fix = fresh physics world on restart. Default `objects` part is useless for archer (view-driven show).

Same evening: Object::f_visual_only (SetVisualOnly/IsVisualOnly) - the default hash skips the object and its subtree. USER RULE: leaves/wind are pure visual and must NEVER tap into rrand (audited clean 2026-09-28: archer has no RRandom calls, no ParticleEmitter, no rand/<random>). Archer still has to mark its show objects (after the bridge agent).

Step 4 DONE (late 2026-09-28): show objects marked visual-only (camera in core; archer backdrop, Sun, leaves group, fireflies+lights, streaks, hair bones) - found by measuring with replay_trace detail (objects split per top-level object) + tick_starts; afterwards nothing changes between ticks. ApplicationArcher::HashSimState = core parts + Stage::HashState (her, world) + Puppet::HashState (puppet) + anim + body + cues. MEASURED: fresh-vs-fresh all identical EXCEPT anim (and bone poses) in 5/7 - the idle clip runs on wall-clock time between replays and the restore does not reset it; warm: puppet differs from tick 0 (not reset), bodies from tick 1-2 (physics world), rules only late in the two long recordings. Next (step 5, not yet approved): reset model animation + Puppet in RestoreRecordingState; physics world with the rp3d agent. Scratch drivers: trace_run.py (--only=, --detail, --tick-starts, --reverse), trace_cmp.py, obj_cmp.py, trace_first.py in the session scratchpad.

Step 5 (anim/pose) DONE 2026-09-28 night: user OK'd excluding grass + all visual props (foliage, vines, boulders, terrain meshes, scenery tiles, arc, popups, arrow views, rope skin/marks, wind debug, Fill) -> ~150 non-physics objects left of 5415. Restore = ResetAnimationForReplay (Puppet::Reset(facing), clips rewound, Idle, ArcherModel::ResetPoseMemory for the one-tick-late layer inputs + chains). RESULT on the 22:37 export: fresh-vs-fresh AND debug-vs-release bit-identical every part every tick; warm differs only in bodies (rp3d world) and what follows. Detail mode now = one part per object. One unexplained MCP ConnectionReset in a release pass (log lost); rerun fine - keep the log if it recurs.

DONE 2026-09-28 ~23:30: rp3d agent added PhysicsWorld::rebuildInternalState() + computeStateHash() (local fixes in the fork, lib + 3rdparty headers installed) and the `physics` part in core's HashSimState; I made rebuildInternalState the LAST statement of ApplicationArcher::RestoreRecordingState. Result: fresh/warm/reversed/debug-vs-release all bit-identical in every part. From now on any trace difference between two runs of one exe is a NEW leak; start with cue_replay --detail.

USER DECISION 2026-09-28: keep ONE test recording, archer_test.rec; others in recordings/archive/ (not deleted - four were untracked). Every agent re-checks after a game change and rewrites with --write archer_test if intended; documented in CLAUDE.md.

**Why:** cue_replay only compares sounds, so it hid divergence.
**How to apply:** core steps (replay ignores live keys, hasher + HashSimState hook + replay_trace, cue_replay .trace) may go ahead; anything in apps/archer waits until the bridge agent is finished - check lockd first.

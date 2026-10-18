---
name: archer-test-baseline-stale
description: "archer_test.trace differs from tick 0 at HEAD adc929a (2026-10-18) without any archer change - stale baseline, not a regression"
metadata:
  node_type: memory
  type: project
  originSessionId: 4c59e560-7c23-462c-8e8d-4fa67026fe08
  modified: 2026-10-18T13:27:24.657Z
---

`python tools/cue_replay.py` reports `archer_test STATE DIFFERENT from tick 0, in bodies, objects, physics, snakes, world` (cues `same`) on a CLEAN HEAD adc929a build, checked by A/B on 2026-10-18 (loading-screen work: HEAD in a worktree gave the identical result). The baselines were last written 2026-10-04 (21abd0b); archer's own code hasn't changed since (4 shader files), but ~20 core commits have, e.g. 6690135 "Core running multiple physics ticks".

**Why:** it looks exactly like a side effect of whatever was just changed, and every body and blockout object differing hides any real difference.
The user confirmed the same day that archer's test recordings are old, so this is known and no need to report it as news.

**How to apply:** don't blame the current change for it, and don't rewrite the baselines unasked (`cue_replay.py --write archer_test` is the user's call). To check an archer change meanwhile, compare replay against replay (before/after builds) instead of against the baseline. Related: [[replay-determinism-plan]], [[worktree-ab-build]].

---
name: lockd-lease-expires-in-long-runs
description: a lockd lease (900 s) lapses during long replay/test runs because only lockd calls refresh it; another agent then takes the files mid-task
metadata:
  node_type: memory
  type: feedback
  originSessionId: 1f0cffad-5fd3-4645-8f64-22d1c6e84e4e
  modified: 2026-09-27T18:02:24.427Z
---

On 2026-09-27 my claims on Stage.* and ApplicationArcher.* expired while tools/cue_replay.py ran three times (about 15 minutes). Another agent, working on bridge_crumble step 2, claimed the files and started editing them before I had finished. No harm was done, because my edits were already in, but I could no longer touch ApplicationArcher.

**Why:** only lockd calls refresh a lease; builds and replays don't.

**How to apply:** before a long run in the middle of a task, claim with a larger ttl_seconds (up to 3600), or call lock_claim again right after the run. After a long run, check lock_list before editing again. `taskkill //IM archer.exe` kills every archer, including another agent's, so check tasklist and ports first. See [[lock-claim-before-build]], [[lockd-owner-must-be-session-id]].

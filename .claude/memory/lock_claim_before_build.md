---
name: lock-claim-before-build
description: "Never send a lockd #build/#port claim and the build/run command in the same parallel tool step - a refused claim does not stop the build"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 95091de0-2e6d-47f4-a5dd-59fe26a32d0c
  modified: 2026-10-25T19:55:14.963Z
---

Claim `#build` / `#port:8765` in one tool call, READ the reply, and only then build or start an app in a later step. Parallel tool calls run regardless of each other's results.

**Why:** on 2026-09-26 an archer rebuild was fired alongside its lock claim; the claim was refused (another session held #build and the port for apps/music) but the build and archer.exe ran anyway - it recompiled shared build/core/debug objects and took port 8765 for ~30 s. Had to stop it and message the other session.

Slipped AGAIN 2026-10-06 (chasm, while the other window built chasm for the core outline pass): same parallel step, same refusal ignored. It happens when the claim feels routine - treat every #build claim as a gate.

And a THIRD time 2026-10-25 with a `#port:8769` claim and a launch-plus-test-script in one step: the port was another agent's, my copy started without its server, and my script drove THEIR copy (muted it, auditioned cues on it). A refused port claim is worse than a refused build: the script still finds a server on that port and nothing looks wrong. Re-claiming a port I used earlier in the session is exactly when it slips - it is not "mine" between turns.

**How to apply:** lock_claim first, alone; on refusal do other work or lock_wait, and if a slip happens, stop what was started and tell the holder what was touched. See [[lockd-owner-must-be-session-id]] and [[shared-build-output-coordination]].

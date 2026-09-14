---
name: lockd-owner-must-be-session-id
description: "An explicit lock_claim must use the raw session id as `owner`, or the PreToolUse hook refuses your own edits as someone else's lock"
metadata: 
  node_type: memory
  type: project
  originSessionId: ece7218f-cba9-4427-8bc3-24f0eb7f7b74
  modified: 2026-09-14T10:50:58.779Z
---

`tools/lockd/claude_lock_hook.py` claims every `Edit`/`Write` target under `owner =
event["session_id"]`. So an explicit `lock_claim` made under any *other* owner string locks **you**
out: the hook sees a different owner holding the path and refuses the write with "another agent is
editing this file", naming a reason you wrote yourself.

**Always pass the raw session id as `owner`** (the uuid in the scratchpad path), not a descriptive
label like `sess-overlay-step6`.

**Why:** the broker keys leases on the owner string and does not know two owners are the same agent.
The refusal message looks exactly like a genuine collision with another agent, so the natural
reaction - go do other work and retry - never clears it.

**How to apply:** claim with the session uuid; if an edit is refused by a lock whose stated reason
is your own, that is this, and the fix is `lock_release` under the wrong owner then re-claim under
the session id. CLAUDE.md and `docs/lock_broker.md` describe the broker but not this.

Related: [[shared-build-output-coordination]].

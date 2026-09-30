---
name: lockd-lease-expires-in-long-runs
description: "leases used to lapse silently mid-task (refresh cut every lease to the caller's TTL, nothing renewed during builds); FIXED 2026-09-30 in code - heartbeat hook + lost-lease notice, live only once the broker is rebuilt/restarted"
metadata:
  node_type: memory
  type: project
  originSessionId: 00e14b47-a8d6-4a26-921e-159cc3531372
  modified: 2026-09-30T13:18:56.148Z
---

On 2026-09-27, and twice more by 2026-09-30, an agent's claims expired during a long build or replay. Another agent then took them, and the first carried on as if it still held them.

There were two causes. First, `LockTable::TouchLocked` reset **every** lease an owner held to the TTL of the current call. So the hook's 300 s auto-claim on one edit cut an explicit 3600 s `#build` down to 5 minutes. Second, only lock calls renewed a lease, and builds and replays make none.

Fixed on 2026-09-30 in tools/lockd and claude_lock_hook.py (details in docs/lock_broker.md sections 6 and 7):
- Each lease is renewed by its own `ttl_s`, and an `auto` claim never downgrades an explicit one.
- The hook's matcher is now `*`. On non-write tools it sends `lock_heartbeat`, which renews explicit leases only.
- An expired or broken explicit lease is stored and handed back as `lost` on the owner's next call. The hook then refuses that tool call once, with a `lockd: you have LOST leases` message.

**How to apply:** check whether the running broker has `lock_heartbeat`, e.g. by looking for `"auto"` entries in lock_list. If it does not, the old behaviour still applies: claim with a long ttl_seconds and re-claim after long runs. Rebuilding lockd means stopping the broker, which drops everyone's leases, so the user picks the moment. A refused `lockd: you have LOST leases` call means stop and re-claim. See [[lock-claim-before-build]] and [[lockd-owner-must-be-session-id]].

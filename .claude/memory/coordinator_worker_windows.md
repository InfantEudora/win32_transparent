---
name: coordinator-worker-windows
description: "user's preferred multi-agent setup (2026-09-30) - one coordinator window hands tasks to blank worker windows via SendMessage instead of in-process subagents, so the user can watch and intervene"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 00e14b47-a8d6-4a26-921e-159cc3531372
  modified: 2026-09-30T14:29:51.072Z
---

On 2026-09-30 the user opened one coordinating session and two blank windows. The coordinator found the blank windows with ListAgents, briefed them with SendMessage, and they reported back the same way. The user called it "already so useful". Tried on two tasks: the archer horn fix plus docs move, then the archer_test re-baseline plus stale-comment sweep. Both went cleanly.

**Why:** the user can't see in-process subagents' work. Worker windows are visible, the user can step in directly, and each window is a separate lockd owner, so lockd keeps them apart. Subagents probably share the parent's session id.

**How to apply:**
- Write self-contained briefs: point to CLAUDE.md, lockd claims in one call with owner = session id, no commit, report back to the coordinator's name, and say that the user's direct instructions win.
- Give parallel workers disjoint work, and name the files the other worker will hold.
- Spot-check a worker's claims before passing them on to the user.
- A worker may decline to edit CLAUDE.md on a peer's request. The coordinator makes that edit itself when the user asked for it directly.
- An idle window wakes on a message.
- A lockd task board (owner, status, result next to the lock table) was discussed as the next feature, if the overview gets hard to keep. See [[lockd-lease-expires-in-long-runs]].

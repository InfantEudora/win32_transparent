---
name: header-edit-mid-build-stale-objects
description: "A header edited while ANOTHER agent's build compiles leaves objects newer than the header but built from the old one - make keeps them, the exe mixes class layouts"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 0458ec6c-789b-4dff-b918-cd2b316d95b8
  modified: 2026-10-19T11:40:31.092Z
---

Seen 2026-10-19 (chasm): another session's build compiled 13:28:40-13:29:38 while I edited `ApplicationChasm.h`
(added a member) at 13:29:09. Objects that STARTED before the edit and FINISHED after it read the old header but
got mtimes newer than it, so my own build (with `#build` held) kept them. The exe crashed at start in
`BuildWaterScene` with `this=0x5c0` - `world_scene` read at the old offset. Nothing in the code was wrong.

**Why:** `#build` serialises builds, not edits; a file lock on a header does not stop a build already running from
reading it half-way. Make compares mtimes only.

**How to apply:** after a header change, look at the object times in `build/obj/<config>/` - any finished within
seconds AFTER your header edit, by a build that was not yours, are suspect; delete them (under `#build`) and
rebuild. A crash with a nonsense `this`/member pointer right after a header change is this until shown otherwise.
To avoid it, hold the header's file lock through your own build (claim, build, release). Related:
[[lock-claim-before-build]], [[shared-build-output-coordination]].

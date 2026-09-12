---
name: tank-viewport-offset-black-half
description: "APP=Tank screenshots are black across the left half BY DESIGN - a deliberate viewport_x offset, not a render bug; do not investigate"
metadata: 
  node_type: memory
  type: project
  originSessionId: 321ef6c3-8b60-4cdc-8703-73b7f520e3b5
  modified: 2026-09-12T15:25:19.902Z
---

An `APP=Tank` screenshot comes back 1600x800 with the **left half solid black** and the scene
drawn only in the right 800x800. **This is intentional** - `ApplicationTank` renders with a
`viewport_x` offset (see `ApplicationTank.cpp` around the `main_window->Resize(1600,800)` call).
Confirmed by the user 2026-09-12.

**Why it matters:** it reads exactly like a half-failed framebuffer, a resize race, or a
screenshot captured mid-rebuild - and it cost a diagnostic detour during the Tank asset migration,
where it was briefly suspected to be a regression from the new `LoadFile` resolver. It is neither.

**How to apply:** when judging whether an `APP=Tank` change still renders correctly, look only at
the right half of the frame. A black left half is the expected picture and is not evidence of
anything. Other apps do not do this.

Related: [[asset-layout-plan]], [[shared-build-output-coordination]] (the *other* cause of odd
Tank screenshots - a concurrent build killing the running app - which does produce a genuinely
broken frame).

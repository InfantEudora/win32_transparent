---
name: chasm-saves-not-migrated
description: Chasm saves are not meant to survive rule changes - never plan a migration or list it as an open question
metadata:
  node_type: memory
  type: feedback
  originSessionId: d760ff91-74f6-4757-90f1-d044e9adbb84
  modified: 2026-10-10T13:13:11.464Z
---

Chasm saves (and recordings) are not expected to load correctly across a change to the rules or the
state. The user, 2026-10-10, when a yards rule change raised "what about yard plots in old saves": "Saves
are not meant to be working across these kinds of changes. So that's fine." Same stance as earlier
("old saves dropped, not converted").

**Why:** the game is in heavy development; converting saves costs effort for nothing the user wants.

**How to apply:** don't write save migrations, compatibility shims or "open: what about old saves"
questions into chasm plans. A one-line "old saves are let go" is enough. Related: [[chasm-game-plan]].

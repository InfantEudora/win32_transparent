---
name: chasm-test-instance-autosaves
description: "Chasm autosave slots are shared by every running copy; the user is FINE with tests overwriting them (2026-10-18) - don't back up/restore, don't rely on them; CLAUDE.md says so"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 5b3cd70d-8db0-4bb3-bbc1-b3b9cc4ab6df
  modified: 2026-10-18T10:59:06.407Z
---

Every running Chasm writes the same autosave_1..3 (oldest first): on leaving a colony (chasm_load, New game), on
quitting, and at a season's first dawn. A test instance beside the user's game overwrites their slots and vice versa.

History: 2026-10-16 two test loads overwrote slots; 2026-10-18 a blind restore by mtime destroyed two of the user's
own autosaves (their game had written them, not mine) - which is what led to the user's decision below.

**User, 2026-10-18:** "The autosaves overwriting each other is ok for now, you don't need to restore them every time.
Normally we wouldn't play more than one running instance. We should note not to rely on the autosaves during testing."
Noted in CLAUDE.md ("Don't rely on Chasm's autosaves while testing").

**Why:** only one copy normally runs; the backup/restore dance cost more than it saved, and got it wrong once.
**How to apply:** no autosave backup/restore around tests. Never use an autosave as a test fixture or expect one to
survive a test - load a NAMED save (village_year5 etc.) and save test states under a name (chasm_save). Mention in the
report only if a test copy's quit/load clearly wrote a slot while the user was playing.

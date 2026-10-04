---
name: blend-edit-in-place-ok
description: the user keeps backups of their .blend files; working on a copy is fine but not required
metadata:
  node_type: memory
  type: feedback
  originSessionId: a628fe61-5f4a-4d8b-a5c2-d9ab8076281f
  modified: 2026-10-02T13:48:34.705Z
---

When editing elfarcher_withprops.blend (2026-10-02) I worked on a copy and saved a separate file
because Blender had it open; the user said that was good but not necessary - they keep backups and
re-export themselves.

**Why:** they have their own backup (.rar beside the file) and do the export to the game asset
themselves.

**How to apply:** a separate output file is still the safe default while Blender has the file open
(their next save would overwrite mine), but no need to be elaborate about protecting the original.
Related: [[spider-leg-shape-keys]].

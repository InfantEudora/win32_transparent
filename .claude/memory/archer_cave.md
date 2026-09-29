---
name: archer-cave
description: Archer cave BUILT 2026-09-29 left of the terrain bay (apps/archer/cave_plan.md) - own floor block so the bay's bank is untouched; blocks reaching back into the bank raise it (closes the cave); next is lighting it
metadata:
  type: project
---

The cave: x -66..-40, roof 9..11, far wall, mouth lip (the old bay left wall, same index), its own floor block appended last. Terrain regions start at ARCHER_CAVE_X_MIN. Backdrop rule: a block with Back() at or behind the bank's wall face raises the columns behind it to roof_rise over its top. The waterfall's stream runs into it (x -62).

**Why:** the user wants an enclosed place to experiment with lighting ([[archer-water]] came first).

**How to apply:**
- Next step is lighting it: the camera-side fill light casts no shadow and still lights the inside; the bank's hazed materials read as distance inside a cave. Both are open, in cave_plan.md.
- Extending the bay's own floor was rejected because it re-lays the bank's column grid and moves the hand-placed vines on its crest.
- wind_test's "no jets" now measures with gusts off; gusts start at the domain's left edge, so moving that edge moved them and a one-tick sample failed.

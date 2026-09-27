---
name: archer-level-grey-blockout
description: "In archer's world level the flat grey slab running off to the right is the untextured blockout part of the level, not a rendering bug"
metadata:
  node_type: memory
  type: project
  originSessionId: b04fed7a-08ad-4acc-b3d5-ba0760d5be55
  modified: 2026-09-27T09:38:59.386Z
---

In apps/archer's main level, the left part (from the start at x -6) is the aesthetic test area -
terrain, foliage, the painted backdrop. From roughly the middle of the default view rightward the
ground is a flat grey untextured slab: that is the BLOCKOUT part of the level, deliberately left
undressed. Confirmed by the user 2026-09-27.

**Why:** I mistook it for a regression (suspected shifted material indices) and started an A/B
build to chase it; the user stopped it.

**How to apply:** judge archer visuals on the left, dressed part only; a grey slab to the right
is expected. See [[archer-app]] and [[archer-terrain-plan]].

---
name: spider-leg-shape-keys
description: spider in elfarcher_withprops got LegsSwing/LegsLift tetrapod keys 2026-10-02 via tools/blender_spider_legs.py (Mirror applied); plus Blender-scripting traps hit on the way
metadata:
  node_type: memory
  type: project
  originSessionId: a628fe61-5f4a-4d8b-a5c2-d9ab8076281f
  modified: 2026-10-02T09:24:02.268Z
---

2026-10-02: the `spider` (archer creatures, see [[archer-app]]) got two symmetric leg shape keys,
built by `tools/blender_spider_legs.py`: LegsSwing (+1 = L1 R2 L3 R4 forward) and LegsLift
(+1 = same group raised), sliders -1..+1, driven as amount*sin(phase) / amount*cos(phase). Morph
order in the glb: Walking 0 (untouched), LegsSwing 1, LegsLift 2; still 3484 verts. Mirror modifier
APPLIED (two sides out of phase cannot come from one mirrored half). Preview action SpiderWalk
(24 frames, cyclic). Saved as ../elfarcher_withprops_spiderlegs.blend, NOT over the user's file
(Blender was open on it). Known compromise: stance feet press ~0.035 below rest; a clean stance
needs a third key (lift per group). Game wiring DONE 2026-10-02: leg_mode 2 (default) in StepCreatures sets swing=sin, lift=cos via SPIDER_KEY_* defines; archer_test baseline was already stale from level changes (webs, tree, slide plan), not rewritten.

**Why:** two linear keys with a planted rest pose cannot give a flat stance - the lift key's
negative half always dips.

**How to apply:** Blender-scripting traps that each cost a run here:
- `m is not mirror` over `o.modifiers` is ALWAYS true (new wrapper per access) - compare names.
- `show_only_shape_key` (the pin) skips modifiers in evaluation - set key values instead.
- a background save drops 0-user images (4 orphans, 40 MB, in this file) - expected, not loss.
- a .blend copied elsewhere breaks `//` relative texture paths - run on the original, `--out` beside it.

---
name: archer-3d-aim-plane-lock
description: Archer aim is a 3D cone with a per-level plane lock (only the character scene unlocked); locked shots are bit-identical to the old 2D ones; a zero TurnInWorld still changes bits
metadata:
  node_type: memory
  type: project
  originSessionId: 1ad30d4f-cd7a-4f4a-b43f-e3525c17c784
  modified: 2026-09-28T22:31:12.919Z
---

BUILT 2026-09-29 at the user's request: the aim sway is a cone (Stage::AimSwaySideDeg beside
AimSwayDeg; the up half untouched, the side half squeezed into the circle), and arrows/aim are v3.
Stage::IsPlaneLocked() is a LEVEL property - every level but STAGE_LEVEL_CHARACTER. Locked: the
side sway is looks-only (the bow leans), the arrow is flattened and z/vz/yaw stay exactly 0.
Unlocked: the app writes Stage::heading_deg from CharacterHeadingDeg() (turntable + clip_yaw) before
each tick; arrows fly off in 3D, block depth (z/depth) decides hits, and an arrow stuck in the tile
rides the turntable (StuckArrow::f_turntable). The aim override now turns about her own side axis
(aim_forward x up), so it works on the turntable too (it was switched off there before).

User decisions: stuck arrows ride the tile; arrows that miss just fly off; side amplitude = the
up one; no left/right steering in the character scene (the turntable is enough).

**Why it matters:** locked levels must replay bit-identically. TurnInWorld with angle 0 still
renormalises the bone quaternion and moves the last bit, which shows up as an `objects` diff.

**How to apply:** skip a zero turn (`if (swing != 0.0f)`) whenever a replay must stay exact; and to
prove a change is "looks only", compare every trace part over the whole run, not just the first
differing one (cue_replay.py only reports the first). See [[replay-determinism-plan]] and
[[archer-app]].

---
name: archer-fall-animation
description: "Archer's air set 2026-09-29 - walk-offs no longer play Running_Jump, spent running jumps hand over to the fall, and the fall-pose overlay is OFF (PUPPET_FALL_POSE_MAX 0) so Falling_Idle plays as authored"
metadata:
  node_type: memory
  type: project
  originSessionId: dce37e32-2277-4eb4-875d-83f3141e84d9
  modified: 2026-09-29T12:41:25.767Z
---

2026-09-29, Puppet.cpp/h:
- Running_Jump is latched only for a JUMP (vel_y > 0 on takeoff, or a coyote-window rise within ARCHER_COYOTE_TICKS+1); running off an edge is the standing set's fall. Grip modes (hang/rope/climb) clear air_clip.
- A running jump still airborne when the clip has played out (run_jump_time >= clip_duration) hands over to Falling_Idle / the landing lead-in.
- The fall-pose overlay (Puppet::fall_weight, hard landing's frame 0 slerped over every bone) is switched OFF with PUPPET_FALL_POSE_MAX 0.0: the re-exported Falling_Idle is a real 0.917 s loop and the overlay froze it. Chosen by the user's go-ahead after a side-by-side at 0 / 0.5 / 1. The dial and ArcherModel::ApplyOverlay were kept on purpose - candidate tool for teeter / catch poses.

Still open: a landing while holding a direction plays no landing clip at all (settle only below PUPPET_IDLE_SPEED) - matters for "falling off completely". Also seen: falling out of the world, the forecast starts a soft Jump_FromAir lead-in ~10 ticks before the respawn (probably forecasting the reset).

**Why:** preparing edge-dependent animations (teeter, fall-and-catch, fall off completely) that another agent's edge lookup will feed.
**How to apply:** the user's bench for the fall clip on its own is the rope scene (scene_set Rope), archer_place x 23 y 100; archer_camera distance 7 follow 1 frames her close. Walk-off/run-jump bench: run right off the rope-scene floor lip at x 17 into the 15-deep pit. tools/cue_replay.py --write is a script, so the lockd hook does not guard the baselines - another agent can rewrite them under your lease. See [[archer-app]], [[replay-determinism-plan]].

---
name: farside-space-game-concept
description: "New top-down space game concept (working name farside) - land on planets in the same view; 3 decisions made 2026-10-19, nothing built"
metadata:
  node_type: memory
  type: project
  originSessionId: d1620512-3d10-4558-bc41-3a02ad5bfbc5
  modified: 2026-10-19T19:37:27.561Z
---

Started 2026-10-19: a top-down space game, Starsector's arc (roam, settle, build a fleet from a
colony) with Starcom: Unknown Space's mood and scale (tiny ship, big planets). The hook: planets are
places in the same view - land on them, curvature hides you, a defence post on the far side can't
shoot you, hide behind asteroids/moons/ships/wrecks.

Design notes are in `apps/farside/docs/README.md` (folder holds docs only, no makefile yet).

DECIDED 2026-10-19 by the user:
- the waterline model (plane cuts each body at depth d; the cap above is playable) - in principle,
  "it'll take shape once we build something";
- sensors decide what anyone knows, for the player AND the AI (AI needs search, not just chase);
- altitude is CONTINUOUS (not the three layers first proposed), height held above the ground.

Still proposals: R = 20 ship lengths, rotation/day-night, zoom following altitude.

**Why:** the user's stated first question is the SCALE, so the first build is a greybox "scale
bench" with sliders, not a game.

**How to apply:** don't build gameplay before the bench answers R, d, camera and ceiling; re-read
the README's open questions before proposing more. Reuse apps/ship's handling and asteroids.
Related: [[ship-orbit-camera-canonical]].

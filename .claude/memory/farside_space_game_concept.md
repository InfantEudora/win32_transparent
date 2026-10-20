---
name: farside-space-game-concept
description: Top-down space game concept (working name farside) - land on planets in the same view; 3 decisions made and the scale bench BUILT 2026-10-19
metadata:
  node_type: memory
  type: project
  originSessionId: d1620512-3d10-4558-bc41-3a02ad5bfbc5
  modified: 2026-10-19T20:15:41.345Z
---

Started 2026-10-19: a top-down space game, Starsector's arc (roam, settle, build a fleet from a
colony) with Starcom: Unknown Space's mood and scale (tiny ship, big planets). The hook: planets are
places in the same view - land on them, curvature hides you, a defence post on the far side can't
shoot you, hide behind asteroids/moons/ships/wrecks.

Design notes: `apps/farside/docs/README.md`.

DECIDED 2026-10-19 by the user:
- the waterline model (plane cuts each body at depth d; the cap above is playable) - in principle,
  "it'll take shape once we build something";
- sensors decide what anyone knows, for the player AND the AI (AI needs search, not just chase);
- altitude is CONTINUOUS (not the three layers first proposed), height held above the ground.

THE SCALE BENCH IS BUILT (2026-10-19, user said "free to get started"): apps/farside, USE_PHYSICS=0
-> build/farside_nophysics.exe, run on port 8770. Field.h geometry, Flight.h ship, sliders + presets
(start/low/landed/landed_near/cruise/starcom), MCP farside_status/_set/_hold. Borrows ships.glb via
an extra asset root `../../ship/assets` - NOT moved to shared_assets because baked builds pack all
of shared_assets into every app.

Still proposals / to decide by flying: R = 20 ship lengths, d, camera, ceiling, rotation/day-night,
zoom following the altitude asked for.

**Why:** the user's first question is the SCALE; the bench answers it by feel before any game.

**How to apply:** don't build gameplay before the user has flown the bench and picked numbers.
Minimised start: ImGui windows created while minimised save at a few px (the Bench panel self-heals
that). Related: [[minimized-screenshot-no-ui]], [[ship-orbit-camera-canonical]].

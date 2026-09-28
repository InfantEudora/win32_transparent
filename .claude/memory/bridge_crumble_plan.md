---
name: bridge-crumble-plan
description: "AGREED 2026-09-27 - archer rope bridge (sways, overload snap with audible warnings, zone-triggered snap) and crumbling rocks (gone for good, detour routes); apps/archer/bridge_crumble_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: b04fed7a-08ad-4acc-b3d5-ba0760d5be55
  modified: 2026-09-27T21:16:32.645Z
---

Plan at apps/archer/bridge_crumble_plan.md, written 2026-09-27. Steps 1-3 BUILT 2026-09-27 (zones +
RouteCheck, stepping stones, the chase via a trigger zone + StageCrumbleGroup); step 4 (bridge as a
surface) is next. Zones come in two uses: areas (HUD/teleport, side by side) and triggers
(f_area false, effects only, once per run).

User's decisions: overload snap WITH warnings, and the warnings audible (creak following the load,
then cracks, then the snap); the first trigger is reaching a spot (a zone); the sway is LOOKS ONLY
(balance already lives on the branches); crumbled rocks are GONE until restart, so every crumble
crossing needs a detour route that works with the rocks gone.

**Why:** both are rules-side (Stage), not rp3d, because she can only stand on what Stage knows; rp3d
only gets post-failure debris via the breakable-wall path.

**How to apply:** the zone must be cue_plan.md section 8's zone (built minimally, agreed with the
cue-plan owner) so there is one zone type; order is zone + route-check utility, crumbling, chase,
bridge surface, strain/snap, zone bridge, sway + cues. See [[plant-mechanics-plan]],
[[adaptive-music-plan]].

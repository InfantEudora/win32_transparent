---
name: chasm-stop-work-and-ice
description: "chasm 2026-10-12: workplaces stoppable from the card (f_stopped, work_stop/range_stop); collector takes over the camp's water drawer; posted cross frozen water with thaw warnings"
metadata:
  node_type: memory
  type: project
  originSessionId: 29744408-68e4-448f-b96c-c0047ed41572
  modified: 2026-10-11T23:01:34.665Z
---

Agreed with the user and built 2026-10-12, both checked on saves/camp_3.json:
- STOP WORK (docs/stopping_work_plan.md): ZoneBuilding::f_stopped via ZONE_OP_WORK_STOP (plot) / ZONE_OP_RANGE_STOP
  (cell); ZoneKindStoppable = collector, woodcutter's hut, ropewalk, builders' hut, range. Stopped = places 0.
  The user REJECTED "collector fills by itself": it still fills only while worked; stop it when water is plentiful
  in store. A collector standing in a settlement (worked or stopped) takes over the camp's drawer_places, as a hut
  takes cutter_places - so a freed carrier never draws water by hand.
- ICE (seasons_plan.md "Crossing the ice"): Walkers::SetSeason(tick) (set in Economy::Tick and Order); wet plots
  under snow are walkable for WALK_WHO_POSTED only. Orders onto water refused. Thaw = risk with a warning (user's
  choice): walk tip "over the ice, it thaws in N days" (tip now always plans the POSTED walk), card line from
  Economy::IceHome/ice_home, and "Cut off from home" in Doing once stranded.

**Why:** playtesting a 3-person outpost, a water carrier sat idle full time; winter crossings wanted for orders.
**How to apply:** walk tip wording not seen on screen (minimised = no overlay). See [[chasm-settlements]],
[[chasm-woodcutting]].

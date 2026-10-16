---
name: chasm-areas-plan
description: "Chasm areas to discover + water ambience, apps/chasm/docs/areas_plan.md, agreed and BUILT 2026-10-16: rivers/falls/biomes/plateau/forest, EVENT_DISCOVERED, debug overlay, unexplored water heard faintly"
metadata:
  node_type: memory
  type: project
  originSessionId: e532a19b-410c-4f18-b4c7-cc0b53ada9b0
  modified: 2026-10-16T15:03:51.889Z
---

User's decisions 2026-10-16: unexplored water heard FAINTLY (a lure to explore); areas = each river, each
waterfall, each biome as one area, and the starting land split by forest density (plateau / forest); only the
FIRST OF ITS KIND gets a notice; areas visible as a toggleable DEBUG overlay.

Built same day (areas_plan.md "As built"):
- Areas.h/.cpp, ChasmWorld::areas (generated, never saved; index = event amount, saved in the journal).
  Land assigned per exploration CELL via the picker (per-plot left holes). ForestData::core per plot.
- EVENT_DISCOVERED = 12 (append-only), Economy::Discovered, LookForAreas beside LookForSeams; landing's first
  look silent; re-derived from exploration after a load (demo_plan "read from the save" kind).
- Water: SignalWater -> river_*/falls_* cue parameters; core follow.gain list; loops SOUND_KEEP. A fall is
  faint until its AREA is seen (lip cell can stay unexplored on the rim edge).
- Overlay: debug `areas` layer (panel + chasm_view), chasm_areas MCP tool. Wiki page wiki/areas.md.

**Why:** the user's first good ambient sounds tied into the event system's need for discovering areas.
**How to apply:** new area kinds append AFTER AREA_DESERT's group is fine only if the index order per map stays
stable for saved journals (saves aren't migrated anyway - [[chasm-saves-not-migrated]]). Open question: the far
side of the chasm counts as "the plateau". AREA_CHASM (8, the floor) added 2026-10-16 with the music: its first
sight fires the score's reveal via the cue table's `music` action (areas_plan.md "The music"). Next: balconies.
Related: [[chasm-sound-plan]], [[chasm-statistics]], [[chasm-demo-menus]], [[chasm-test-instance-autosaves]].

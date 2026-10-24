---
name: chasm-wall-ends
description: Chasm palisade ends run on to buildings/terraces/castle/drops/corners (view only); gates against faces; test scene saves/wall_ends_test.json
metadata:
  node_type: memory
  type: project
  originSessionId: 2d34edac-a4e6-4bc0-bc3b-8a3db156fab6
  modified: 2026-10-24T09:22:38.207Z
---

BUILT 2026-10-24 (line_works_plan.md step 5, decided with the user that day): a wall's END runs on into a face (house body,
castle block, terrace a wall's height up), to short of a drop (chasm rim, its own terrace's edge) with its last 3 logs
stepping down, and to a face at a cell CORNER. Gates may have a face as a side (own jamb post), e.g. one plot of wall on
a road between two terraces. All in RoadMesh.cpp `WallJoins`/`WallEndKind`/`WallJoinAt`; nothing in rules/saves/hash.

**Why corners matter:** a line along this grid zigzags, so ends and gate sides meet faces across a cell constantly. The
rules already seal a corner touch (walkers go plot to plot by edges) - verified with a shut gate + `chasm_pick to_x/to_z`.

**How to apply:** look first at `saves/wall_ends_test.json` (flat map: every case side by side) when changing walls,
terraces or gates. Gates stay explicit by the user's choice - never make roads between terraces/houses gates automatically.
Related: [[chasm-terraces-plan]], [[chasm-defence-plan]].

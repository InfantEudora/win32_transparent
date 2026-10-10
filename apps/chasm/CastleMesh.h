#ifndef _CHASM_CASTLE_MESH_H_
#define _CHASM_CASTLE_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    THE DEBUG CASTLE, drawn (docs/ramparts_plan.md, step 2): grey stone, one fine cell at a time, a quarter for each
    corner with a piece on it - as a building's (BuildingMesh.h), so neighbouring pieces always meet.

    A piece's top is the deck its walkers stand on, and is drawn where they walk (ApplicationChasmWorkersView.cpp,
    WorkerHeight): at its plot's point the layer's height, and at the edge toward a neighbour the height where
    the two layers meet (Walkers.h, WalkLayersMeet) - so a stair is a ramp from the height below to its own, and
    blocks of one height make one level walk. Where a top meets nothing - open ground, a lower or higher piece - it
    keeps its own height there, and a block or an arch has a low parapet along that edge: a wall-walk.

      - BLOCK: solid from the ground to its top.
      - ARCH: a slab at its top on a pillar at each corner of its plot, the passage under it open.
      - STAIR: solid, its top the ramp.
*/
void BuildCastleCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

#endif

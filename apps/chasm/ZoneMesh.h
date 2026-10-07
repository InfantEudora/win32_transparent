#ifndef _CHASM_ZONE_MESH_H_
#define _CHASM_ZONE_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    THE ZONES AS TRIANGLES, one terrain chunk at a time (a chunk's zones are rebuilt when its
    ZoneState::chunk_version moves). Per fine cell, in this order:

      - ground: a garden or town plot's quarters laid on the relief, in grass or trodden earth -
        under a house nothing, the house hides it;
      - roads, along the edges between road plots (RoadMesh.h);
      - crops, for a cell whose coarse parent is a field (CropMesh.h);
      - houses (BuildingMesh.h);
      - boundaries - walls, palisades, fences - that form from what meets what (BoundaryMesh.h).

    View only: built from a ZoneState, read by nothing in a tick.
*/

//`field_stage`: per building id, the FIELD_STAGE_* a field is drawn at (CropMesh.h); NULL, or an id
//past its end, draws a field ripe.
void BuildZoneChunk(const ChasmWorld& w, const ZoneState& z, int chunk, std::vector<vertex>& out,
                    const std::vector<uint8_t>* field_stage = NULL);

//Plot v's own ground - the quarters of the cells round it - as a flat tile laid `lift` over the relief,
//appended to `out`: the play mode's ghost of a ground tool, and a lot's outline when it is highlighted.
//Nowhere below `floor`: a bridge's ghost lies on its deck over the water, not on the river bed under it.
void BuildPlotTile(const ChasmWorld& w, int v, float lift, std::vector<vertex>& out, float floor = -1e30f);

#endif

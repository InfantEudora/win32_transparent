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
      - crops, for a cell whose coarse parent is a field (CropMesh.h);
      - houses (BuildingMesh.h);
      - boundaries - walls, palisades, fences - that form from what meets what (BoundaryMesh.h).

    View only: built from a ZoneState, read by nothing in a tick.
*/

void BuildZoneChunk(const ChasmWorld& w, const ZoneState& z, int chunk, std::vector<vertex>& out);

#endif

#ifndef _CHASM_ZONE_MESH_H_
#define _CHASM_ZONE_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    THE ZONES AS TRIANGLES, one terrain chunk at a time (a chunk's zones are rebuilt when its
    ZoneState::chunk_version moves). PLACEHOLDERS until the procedural buildings and crops:

      - a house is its plot extruded, storey by storey. A plot is made of quarters - one per fine
        cell around its vertex (GridPick.h) - so each quarter is extruded on its own and walls go
        only where the neighbouring quarter in the same cell is LOWER: between two quarters of one
        building there is no wall, and a taller neighbour shows its wall above the lower roof.
        Flat roofs. Palette wall and roof colours.
      - a field is its coarse cell's four fine cells laid on the ground's own relief, a hair above
        it, in the field colour.

    View only: built from a ZoneState, read by nothing in a tick.
*/

#define ZONE_STOREY_HEIGHT  1.3f    //world units - a fine cell (about one house) is 2 across

void BuildZoneChunk(const ChasmWorld& w, const ZoneState& z, int chunk, std::vector<vertex>& out);

#endif

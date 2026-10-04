#ifndef _CHASM_CROP_MESH_H_
#define _CHASM_CROP_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    A FIELD'S CROPS, one fine cell at a time (grid_plan.md step 8). Called by BuildZoneChunk for
    every fine quad whose coarse parent is a field; the four children of one coarse cell make one
    field, so whatever is drawn has to line up across the four as if it were one surface.

    View only, deterministic from the field's coarse cell (a field always looks the same).
*/
void BuildFieldCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

#endif

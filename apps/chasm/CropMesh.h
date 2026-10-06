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

    View only, deterministic from the field's coarse cell and its STAGE (economy_plan.md, P3: the
    rows show the growth). A field with no economy state to go on is drawn ripe.
*/
#define FIELD_STAGE_BARE        0   //tilled: earth ridges and furrows - just harvested, or not yet sown
#define FIELD_STAGE_SPROUTS     1   //thin low green lines along the ridges
#define FIELD_STAGE_GREEN       2   //full rows, a dark green whatever the crop
#define FIELD_STAGE_RIPE        3   //full height, in the crop's own colour
#define FIELD_STAGE_COUNT       4

//The stage for a field's growth toward its harvest (EconomyFieldGrowth, 0..1).
int FieldStageOf(float growth);

void BuildFieldCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, int stage, std::vector<vertex>& out);

#endif

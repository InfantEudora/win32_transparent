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

/*
    What a field is drawn as, a byte a field (ZoneMesh's `field_stage`): its STAGE in the low four bits and,
    above, the crop growing on it plus one - 0 for none. A field's crop is the economy's now, rotated as it is
    sown (fields_plan.md), not the zones'; a bare stage with no crop takes the zones' starting crop.
*/
inline uint8_t FieldLook(int stage, int crop){
    return (uint8_t)((stage & 0x0F) | (((crop + 1) & 0x0F) << 4));
}

//`look` is a FieldLook (a plain FIELD_STAGE_* is one with no crop). A cell still a site (ZoneState::
//field_built) is drawn staked out whatever the look: a stake at each corner and a cord along its edges.
void BuildFieldCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, int look, std::vector<vertex>& out);

#endif

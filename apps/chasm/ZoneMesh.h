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
        under a house nothing, the house hides it; one still a site staked out instead, as a field site is;
      - roads, along the edges between road plots (RoadMesh.h);
      - crops, for a cell whose coarse parent is a field (CropMesh.h);
      - houses (BuildingMesh.h);
      - boundaries - walls, palisades, fences - that form from what meets what (BoundaryMesh.h).

    View only: built from a ZoneState, and from the economy's look of a field and of the zombies' harm, read by nothing
    in a tick.
*/

/*
    WHAT THE ZOMBIES HAVE DONE, as drawn (zombies_plan.md, step 4d): the rules' harm (Economy.h, EconomyHarm) in a few
    steps, so a chunk is rebuilt only as a step is passed - the fields' stages' way. A palisade plot's logs lean away
    from the blows, more of them down the more it is harmed, and once it is broken they lie fallen between its stumps; a
    gate's door gives and sags, and once broken hangs askew; a breached building's doors stand open on the dark.
*/
#define ZONE_HARM_STEPS     8       //a harmed wall or gate is drawn at step 1 to ZONE_HARM_STEPS - 1, broken at this one
struct ZoneHarmMark{
    uint32_t id = 0;                //a palisade's or a gate's plot, or a building
    int step = 0;
    //Where the blows came from, if known - the zombies at it when it was first seen harmed. What gives, gives away.
    bool f_from = false;
    vec2 from;
};
struct ZoneHarmLook{
    std::vector<ZoneHarmMark> walls, gates, doors;     //each by id; a door only once breached
    const ZoneHarmMark* Wall(int plot) const;
    const ZoneHarmMark* Gate(int plot) const;
    bool Breached(uint32_t building) const;
};
struct EconomyState;
//The look of `e`'s harm now. `before`, the look drawn last, keeps each mark's `from` as first seen.
void ZoneHarmLookOf(const EconomyState& e, const ZoneHarmLook* before, ZoneHarmLook& out);
//Sets `f_chunk` for every chunk whose drawing changes between looks `a` and `b`.
void ZoneHarmChunks(const ChasmWorld& w, const ZoneState& z, const ZoneHarmLook& a, const ZoneHarmLook& b,
                    std::vector<uint8_t>& f_chunk);

//`field_stage`: per building id, the FIELD_STAGE_* a field is drawn at (CropMesh.h); NULL, or an id
//past its end, draws a field ripe. `harm`: what the zombies have done (above); NULL, nothing.
void BuildZoneChunk(const ChasmWorld& w, const ZoneState& z, int chunk, std::vector<vertex>& out,
                    const std::vector<uint8_t>* field_stage = NULL, const ZoneHarmLook* harm = NULL);

//Plot v's own ground - the quarters of the cells round it - as a flat tile laid `lift` over the relief,
//appended to `out`: the play mode's ghost of a ground tool, and a lot's outline when it is highlighted.
//Nowhere below `floor`: a bridge's ghost lies on its deck over the water, not on the river bed under it.
void BuildPlotTile(const ChasmWorld& w, int v, float lift, std::vector<vertex>& out, float floor = -1e30f);

#endif

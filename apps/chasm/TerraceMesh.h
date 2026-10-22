#ifndef _CHASM_TERRACE_MESH_H_
#define _CHASM_TERRACE_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    A TERRACE, drawn (terraces_plan.md stage 2): one fine cell at a time, a quarter for each corner with a terrace on
    it - as a building's (BuildingMesh.h), so neighbouring plots always meet.

      - THE TOP, flat at the terrace's height, in its biome's grass: the ground shader lays the seasons and the snow on it
        as on the ground round it (chasm_ground.frag).
      - A RETAINING WALL up each of the quarter's two outer edges where the plot across is not a terrace as high:
        dressed stone from a little into what is below - the terrain, or a lower terrace's top - up to the top, with a
        coping along its head. Two terraces of one height meet with no wall; of two heights, the higher draws it.
      - While it is a SITE, only its outline, staked out on the ground as a garden's is (ZoneMesh.cpp).

    Built up only (ZoneCanTerrace), so the terrain never rises through its top.
*/
void BuildTerraceCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

#endif

#ifndef _CHASM_BUILDING_MESH_H_
#define _CHASM_BUILDING_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    A HOUSE, one fine cell at a time (grid_plan.md step 8). A house is its plot - the quarters of
    the cells round its vertex - raised by its storeys. Each quarter is built here, from its own
    corner's storeys and its two neighbouring corners', and nothing else: so a quarter is the same
    whichever cell asks, and neighbouring houses always meet.

      - WALLS go up a quarter's two outer edges where the neighbour is lower, with a window on every
        storey and, on some ground floors, a door.
      - THE ROOF rises from the eaves to a ridge over the plot's own vertex. Where the neighbouring
        corner is the same height the ridge carries on across the edge between them - so a row of
        houses becomes one roof, a lone house a hipped one, and nothing needs to know which.
      - A CHIMNEY on some houses, built by one of the plot's quarters.
*/

#define ZONE_STOREY_HEIGHT  1.3f    //world units - a fine cell (about one house) is 2 across

void BuildHouseCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

#endif

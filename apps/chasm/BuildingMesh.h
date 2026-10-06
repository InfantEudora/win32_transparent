#ifndef _CHASM_BUILDING_MESH_H_
#define _CHASM_BUILDING_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    A BUILDING, one fine cell at a time (grid_plan.md step 8). A building's plot is - the quarters of
    the cells round its vertex - raised by its storeys. Each quarter is built here, from its own
    corner's storeys and its two neighbouring corners', and nothing else: so a quarter is the same
    whichever cell asks, and neighbouring houses always meet.

      - WALLS go up a quarter's two outer edges where the neighbour is lower, with a window on every
        storey and, on some ground floors, a door.
      - THE ROOF rises from the eaves to a ridge over the plot's own vertex. Where the neighbouring
        corner is the same height AND THE SAME BUILDING the ridge carries on across the edge between
        them - so a house of several plots becomes one roof, a lone plot a hipped one, and two
        buildings side by side meet in a valley, each its own roof colour.
      - A CHIMNEY on some plots of a kind that has them, built by one of the plot's quarters.

    Every building kind is drawn here (docs/buildings_plan.md); a kind differs in its walls, roof,
    doors, windows and chimneys (kind_looks in BuildingMesh.cpp).
*/

#define ZONE_STOREY_HEIGHT  1.3f    //world units - a fine cell (about one house) is 2 across

void BuildHouseCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

#endif

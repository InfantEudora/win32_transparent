#ifndef _CHASM_BOUNDARY_MESH_H_
#define _CHASM_BOUNDARY_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    BOUNDARIES THAT FORM BY THEMSELVES (grid_plan.md step 8; references townscapergarden.jpg and
    titlescreenlittleage.png). Nothing here is placed: a wall, palisade or fence runs along every
    boundary segment whose two sides differ, decided by the pair it separates, so painting or
    erasing either side redraws it.

      - PLOT boundaries. A plot's outline is a chain of segments, one in each fine cell round it -
        from an edge's midpoint to the cell's centre - with one plot on either side. Where one side
        is a garden or town ground and the other is not inside the settlement at all, the garden
        gets a low stone wall with a hedge on it and the town a palisade. A house counts as inside,
        and a house's own walls already bound it, so a house side draws nothing.
      - FIELD outlines. A field is a group of coarse cells (docs/buildings_plan.md); a cell's outline
        runs along fine edges. Where the cell across is not part of the same field - another field,
        no field, or none at all at the map's edge - the field gets a fence; between two fields one.

    Posts stand at a segment's ends, so where lines turn or meet they meet at a post. One fine cell
    at a time, like the rest of the zones' view; a post on an edge's midpoint, which two cells
    share, is built by only one of them.
*/

void BuildBoundaryCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

#endif

#ifndef _CHASM_ROAD_MESH_H_
#define _CHASM_ROAD_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "ChasmWorld.h"
#include "Zones.h"

/*
    ROADS, drawn along the grid's EDGES rather than as plots (docs/roads_plan.md). A road joins a road
    plot's vertex to each neighbouring road plot's along their fine edge, and every piece of it ends
    at an edge's MIDPOINT with the same cross-section - square to the edge, a half width either side
    - so the pieces two vertices draw meet there without a seam, whoever draws them.

    Per road vertex, by how many road edges meet there:
      - TWO: one curve from the first edge's midpoint to the second's, bent toward the vertex (a
        quadratic Bezier with the vertex as its control point). Straight through it is a straight
        road; at a corner it cuts the corner. So a road painted along a staircase of grid edges - which
        is what a straight line across this grid is - reads as a road, not a zig-zag.
      - ONE, or THREE AND MORE: a straight piece out to each midpoint, and a round joint at the vertex
        - a dead end, a junction.

    A vertex is drawn by one chunk only, the one its first plot quad is in (RoadVertexOwner), so a
    road across a chunk border is neither doubled nor lost. View only, like everything ZoneMesh.h calls.
*/

//Draws the road vertices this fine cell owns.
void BuildRoadCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out);

//Whether p, standing in plot `plot`, is within `clearance` of a road's centre line - what the forest
//gives way to, so no tree stands on a road or leans over it.
bool RoadNear(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& p, float clearance);

/*
    For the road tool's drag: plots a and b sit diagonally across one fine cell, so painting both leaves
    them unjoined (a road follows edges). Returns the cell's corner between them that is nearer
    `toward` - the cursor - to paint as well, or -1 if a and b are not across a cell from each other.
*/
int RoadBridgePlot(const ChasmWorld& w, int a, int b, const vec2& toward);

#endif

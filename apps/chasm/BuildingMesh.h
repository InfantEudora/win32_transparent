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
      - A CHIMNEY on some plots of a kind that has them, built by one of the plot's quarters - and on
        EVERY HOUSE exactly one (needs_plan.md step 3, the user's 2026-10-08): through its roof or, by
        the house's hash, a stone stack up the outside of one of its walls.

    Every building kind is drawn here (docs/buildings_plan.md); a kind differs in its walls, roof,
    doors, windows and chimneys (kind_looks in BuildingMesh.cpp).
*/

#define ZONE_STOREY_HEIGHT  1.3f    //world units - a fine cell (about one house) is 2 across

/*
    WHERE A BUILDING'S WALL CROSSES the fine edge between neighbouring plots a and b, either way round (the user's,
    2026-10-18). A plot's ground is the quarters of the cells round it, in to each edge's midpoint and out to each
    cell's centre - and round a point of five or six cells that is a STAR, the midpoints drawn in past the line
    between the centres, while a site's frame (BuildSite) stands on the centres and is straight. So on an edge with
    such a point at either end a wall runs straight from centre to centre, crossing the edge where that line does,
    and the house stands in its frame's shape. Elsewhere at the midpoint: round a point of three cells the
    midpoints stand OUT past the centres' line, and straightened the house would be a small sharp triangle; on the
    fours that are most of the grid the two all but agree.

    Worked the same from either side, so the cells either side of an edge agree on its point to the bit and the
    houses either side of it meet.
*/
vec2 BuildingWallCrossing(const ChasmWorld& w, int a, int b);

/*
    What of a house the view lights up while it is warm (needs_plan.md, step 3): its chimney's top, where
    its smoke rises, and its windows' panes, which glow. BuildHouseCell reports them when asked, so they are
    where the mesh put them.
*/
struct BuildingChimney{
    uint32_t building = 0;
    vec3 top;                   //the middle of the chimney's top
};
struct BuildingPane{
    uint32_t building = 0;
    vec3 corner[4];             //on the pane, counter-clockwise seen from outside
    vec3 outward;
};
struct BuildingFixtures{
    std::vector<BuildingChimney> chimneys;
    std::vector<BuildingPane> panes;
};

/*
    A GUARD TOWER (defence_plan.md): upright logs round its junction's polygon (ZoneTowerCorners), a storey of them for
    each that stands, and on the whole of it a platform with a parapet of sharpened stakes, as the palisade's. Its
    guards stand on the platform (ApplicationChasmWorkersView.cpp) - at BuildingTowerDeck's height.
*/
float BuildingTowerDeck(const ChasmWorld& w, const ZoneState& z, int plot);
//How high a walker stands on layer `layer` of plot `plot` (Walkers.h, LAYERS) at its point: a guard tower's platform,
//or so many half storeys over the plot's ground - a castle piece's top (CastleMesh.h), a stair's middle.
float BuildingLayerHeight(const ChasmWorld& w, const ZoneState& z, int plot, int layer);
//How far its walls are from its plot's point, along `toward` - where a palisade beside it meets it (RoadMesh).
float BuildingTowerReach(const ChasmWorld& w, int plot, const vec2& toward);
//Whether `at` is on its platform.
bool BuildingOnTower(const ChasmWorld& w, int plot, const vec2& at);

/*
    A ROPEWALK's dressing (goods_plan.md), which tells it from a house: its WALK - strands of rope over T-posts,
    waist-high, along each two of its plots side by side, so a longer ropewalk has a longer walk - out past the
    wall on open ground, the side toward the camera where both are open; and its YARD on the open side most toward
    the camera: a SPINNING WHEEL, `coils` coils of rope stacked beside it and `sheaves` sheaves of hemp leant on the
    wall. One plot: the yard and no walk. Built in on every side: nothing. Not part of the zone mesh, because the
    coils and sheaves are the economy's (ApplicationChasmRopewalk.cpp).
*/
void BuildRopewalkYard(const ChasmWorld& w, const ZoneState& z, uint32_t id, int coils, int sheaves,
                       std::vector<vertex>& out);

//`fixtures`, if given, gets the houses' chimneys and panes this cell builds.
void BuildHouseCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out,
                    BuildingFixtures* fixtures = NULL);

#endif

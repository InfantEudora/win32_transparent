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

/*
    WHERE THE UPPER DOOR from building plot `b` onto the terrace on plot `u` is (ZoneUpperDoor, terraces_plan.md stage 3):
    in the wall between them, on the piece of it in the lower-numbered of the two cells either side of their edge - `quad`
    - at that piece's middle, `at`. BuildHouseCell draws it there and the walkers' figures are drawn through it. False when
    b has no door onto u.
*/
bool BuildingUpperDoorAt(const ChasmWorld& w, const ZoneState& z, int b, int u, int& quad, vec2& at);
//How far out from its wall the steps down from a door reach, its floor `drop` over level ground (DoorSteps): 0 for none.
float BuildingDoorStepsRun(float drop);

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
//or so many half storeys over the castle piece's level (ZoneCastleBase) - its top (CastleMesh.h), a stair's middle.
float BuildingLayerHeight(const ChasmWorld& w, const ZoneState& z, int plot, int layer);
//How high nodes (a, la) and (b, lb), joined at `meet` (WalkJoin), meet at `at` on the edge between them: the ground's own
//height there where one of them is the ground - a terrace's top, the hill, a stair's foot - else the meet's.
float BuildingMeetHeight(const ChasmWorld& w, const ZoneState& z, int a, int la, int b, int lb, int meet, const vec2& at);
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
/*
    A SAIL LOFT's dressing (steam_plan.md step 4), in its yard as a ropewalk's (the same open side): a TENTER FRAME
    facing the camera with a canvas stretched on it while the loft has anything to work - bare, it is out of hemp -
    `bolts` bolts of canvas stacked beside it and `sheaves` sheaves of hemp leant on the wall.
*/
void BuildSailLoftYard(const ChasmWorld& w, const ZoneState& z, uint32_t id, int bolts, int sheaves,
                       std::vector<vertex>& out);

//`fixtures`, if given, gets the houses' chimneys and panes this cell builds. `harm` (ZoneMesh.h), if given: a
//BREACHED building's doors stand open on the dark (zombies_plan.md, step 4d).
//Where a steam collector's plot's hood lets its steam go, the top of its neck (steam_plan.md): false where it has no drop.
bool SteamHoodTop(const ChasmWorld& w, int plot, vec3& top);

struct ZoneHarmLook;
void BuildHouseCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out,
                    BuildingFixtures* fixtures = NULL, const ZoneHarmLook* harm = NULL);

#endif

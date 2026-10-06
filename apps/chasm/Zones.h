#ifndef _CHASM_ZONES_H_
#define _CHASM_ZONES_H_

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"
#include "TerrainMesh.h"

/*
    WHAT THE PLAYER PAINTED: the zones (grid_plan.md section 5). Simulation state - changed only by
    a command applied on a tick (ZoneOp), so a run can be replayed from the commands it was given.

      - BUILDINGS (docs/buildings_plan.md): houses, stores, workshops, water collectors on fine
        PLOTS, fields on COARSE CELLS. A building is a group with an identity of its own - an entry
        in the building table, which every plot (`building[v]`) and field cell (`field[c]`) it
        covers names by id. A plot grows in storeys (`storeys[v]`), whatever stands on it.
      - GROUND on fine PLOTS (step 8): `ground[v]`, a garden, town ground or road. A garden's and a
        town's edge get a wall or palisade (BoundaryMesh.h), and a building may stand on either,
        hiding it until the building is gone.

    WHICH BUILDING A PLOT JOINS is the player's: A DRAG IS ONE BUILDING. Every command of a drag
    carries the same stroke number. A stroke that starts on empty ground makes a new building, one
    that starts on a building of the same kind extends that one, and the stroke goes on adding to
    its building - plots joined to it only (a diagonal step across a cell brings the corner plot
    between, so the building stays one piece); a stroke that jumps a gap, or fills a house, goes on
    with a new one. Separate strokes stay separate buildings even where they touch. Erasing a plot can
    leave a building in pieces: the largest keeps its id, each other piece becomes a building of its
    own (Zones::Apply).

    THE RULES, all checked when a command arrives, so the state is always valid:
      - A building needs its plot flat: every corner of every fine cell around it on the plot's
        level. Nothing is built half over a cliff, and nothing on the chasm floor, under the mist
        (gameplay_plan.md) - nor fields there.
      - Not on water, nor near it - except a WATER COLLECTOR, which must touch it: a corner of its
        cells under the water or at its very edge, its own vertex above it. It may stand on a bank's
        slope.
      - A field needs its coarse cell flat: all nine of its fine vertices on one level.
      - Ground needs its plot flat in the same way.
      - Buildings and ground keep off fields: neither on a plot touching a field's cell, no field on
        a cell with a building or ground on any of its vertices.
      - A road and a building never share a plot (step 10): a building hides a garden, but would cut
        a road.
      - A road is not painted over a garden or town: it stops at the edge, where it makes a gate.
      - Not too steep (biomes_plan.md step 2): the ground may rise only so much across a plot (a road's
        limit is twice a house's) or a field's cell - ZONE_RISE_* in Zones.cpp. Not on the mountain.
      - A house is at most ZONE_HOUSE_MAX_PLOTS plots, since it holds one family.
      - At most a kind's own number of storeys (ZoneKindMaxStoreys).
      - A WINCH is one plot, on the rim right above a balcony: its plot on the plateau, balcony ground
        among its cells' corners and no open chasm - the one exception to "too close to a cliff". It
        lowers to its LANDING (ZoneWinchLanding), a balcony plot nothing may be built on.

    A ZoneState is immutable once published (Zones::Publish), the way the world is, and carries the
    world it was painted on: a new map makes every index in it meaningless.
*/

#define ZONE_MAX_STOREYS        4       //the most of any kind - a house's
#define ZONE_HOUSE_MAX_PLOTS    4

//What a building is. Never renumber - saves and recordings name these by value.
#define ZONE_KIND_NONE          0       //an id no longer in use
#define ZONE_KIND_HOUSE         1
#define ZONE_KIND_STORE         2
#define ZONE_KIND_WOODCUTTER    3
#define ZONE_KIND_WATER         4       //a water collector, at a river's bank or a pool
#define ZONE_KIND_FIELD         5       //on coarse cells, not plots
#define ZONE_KIND_WINCH         6       //on the rim above a balcony; walkers ride it down (Walkers.h)
#define ZONE_KIND_COUNT         7
const char* ZoneKindName(int kind);
int ZoneKindByName(const std::string& name);    //-1 if none
int ZoneKindMaxStoreys(int kind);

//What grows on a field. Never renumber.
#define ZONE_CROP_WHEAT         0
#define ZONE_CROP_GREENS        1
#define ZONE_CROP_BEANS         2
#define ZONE_CROP_COUNT         3
const char* ZoneCropName(int crop);
int ZoneCropByName(const std::string& name);    //-1 if none

//What a plot's ground is. Never renumber - saves and recordings refer to these by value.
#define ZONE_GROUND_NONE    0
#define ZONE_GROUND_GARDEN  1       //green, walled: Townscaper's gardens
#define ZONE_GROUND_TOWN    2       //trodden earth, palisaded: A Little Age's town
#define ZONE_GROUND_ROAD    3       //step 10: drawn along the grid's edges, walked fastest (docs/roads_plan.md)
#define ZONE_GROUND_COUNT   4
const char* ZoneGroundName(int ground);

/*
    What a boundary encloses: a garden or town plot with no house on it. A road is ground but not
    enclosed - it runs up to a wall from outside, which is where a gate goes (roads_plan.md) - and a
    house is its own boundary.
*/
inline bool ZoneEnclosesGround(int ground){
    return ground == ZONE_GROUND_GARDEN || ground == ZONE_GROUND_TOWN;
}

/*
    What stands between two plots joined by a fine edge: where exactly one of them is inside (a house,
    or enclosed ground) and that one is not a house, a garden's wall or a town's palisade. One rule
    for the two that need it - BoundaryMesh draws it and a walker cannot cross it - so what is seen
    and what is walked agree.
*/
#define ZONE_BOUNDARY_NONE          0
#define ZONE_BOUNDARY_GARDEN_WALL   1
#define ZONE_BOUNDARY_PALISADE      2
struct ZoneState;
struct ChasmWorld;
int ZoneBoundaryBetween(const ZoneState& z, int a, int b);

/*
    GATES (docs/roads_plan.md): where a road runs into a wall, the wall opens. Exactly: across the edge
    a - b, one end is a road plot that is a DEAD END (one road neighbour) and the other an enclosed
    plot behind a boundary, the one most nearly straight ahead of the road coming in - and not off to
    the side (within about 70 degrees). One gate per dead end at most, so a road ending at a wall's
    corner does not open both walls. A gate is drawn (BoundaryMesh) and walked through (Walkers), so
    a walled garden is entered by its gate and only by its gate.
*/
bool ZoneGateBetween(const ChasmWorld& w, const ZoneState& z, int a, int b);
//The enclosed plot road plot `road` opens a gate into, or -1.
int ZoneGateOf(const ChasmWorld& w, const ZoneState& z, int road);

/*
    ARCHES (docs/roads_plan.md, Townscaper's): a road plot running between buildings of two storeys or
    more - two road neighbours, and every other neighbour such a building - is built over: their
    upper storeys bridge it, up to the lower of them, over a passage one storey high - where such
    plots run at most three in a row, a hole through a row of houses rather than a covered street.
    A matter of how a building is DRAWN only: the plot stays a road, walked like any other. 0 if
    `plot` is no arch.
*/
int ZoneArchStoreys(const ChasmWorld& w, const ZoneState& z, int plot);

/*
    What a command asks for. Travels in SimCommand::value[0]; the plot or cell index in subtype,
    value[1] the kind (ground, building or crop, by op), value[2] the stroke. Never renumber - the
    building ops are the house ops they grew from, so a recording from before them replays (a kind
    of 0 there means a house).
*/
enum ZoneOp{
    ZONE_OP_NONE = 0,
    ZONE_OP_BUILD_PAINT,    //plot: join the stroke's building, or start one - what a drag does
    ZONE_OP_BUILD_ADD,      //plot: as paint, and on a built plot one storey more - what a click does
    ZONE_OP_BUILD_REMOVE,   //plot: one storey less, the last one takes the plot out of its building
    ZONE_OP_BUILD_ERASE,    //plot: out of its building altogether
    ZONE_OP_FIELD_PAINT,    //coarse cell: join the stroke's field, or start one
    ZONE_OP_FIELD_ERASE,    //coarse cell: out of its field
    ZONE_OP_GROUND_PAINT,   //plot: ground of the kind in value[1] (ZONE_GROUND_*)
    ZONE_OP_GROUND_ERASE,   //plot: no ground
    ZONE_OP_FIELD_CROP,     //coarse cell: its whole field to the crop in value[1] (ZONE_CROP_*)
    ZONE_OP_COUNT
};
const char* ZoneOpName(int op);
int ZoneOpByName(const std::string& name);  //the names above, and the old house_* ones; -1 if none

struct ChasmWorld;     //ChasmWorld.h

//One building: its kind, what a field grows, and how many plots or cells it covers (0: gone).
struct ZoneBuilding{
    uint8_t kind = ZONE_KIND_NONE;
    uint8_t crop = ZONE_CROP_WHEAT;
    int size = 0;
};

struct ZoneState{
    std::shared_ptr<const ChasmWorld> world;   //what the indices below refer to
    /*
        The building table, by id. Ids are handed out in order and never reused - id 0 is none - so a
        building is named the same way in every save and every replay for as long as it stands.
    */
    std::vector<ZoneBuilding> buildings;
    std::vector<uint32_t> building;             //per fine vertex: the building on it, 0 none
    std::vector<uint8_t> storeys;               //per fine vertex: 0 with no building
    std::vector<uint8_t> ground;                //per fine vertex, ZONE_GROUND_*
    std::vector<uint32_t> field;                //per coarse quad: the field it is part of, 0 none
    //The drag in progress: its stroke number and the building it is making (0: none yet).
    uint32_t stroke = 0;
    uint32_t stroke_building = 0;
    std::vector<uint32_t> chunk_version;        //per terrain chunk: bumped when its zones change
    uint32_t version = 0;
    int built_plots = 0;
    int grounds = 0;
    int field_cells = 0;

    const ZoneBuilding* BuildingOf(int plot) const{
        uint32_t b = (plot >= 0 && plot < (int)building.size()) ? building[plot] : 0;
        return b ? &buildings[b] : NULL;
    }
    int KindOf(int plot) const{
        const ZoneBuilding* b = BuildingOf(plot);
        return b ? b->kind : ZONE_KIND_NONE;
    }
};

//A building as a save holds it: everything Zones::Restore needs to put it back as it was.
struct ZoneSavedBuilding{
    uint32_t id = 0;
    int kind = ZONE_KIND_HOUSE;
    int crop = ZONE_CROP_WHEAT;
    std::vector<std::pair<int,int>> plots;      //plot, storeys
    std::vector<int> cells;                     //a field's coarse cells
};

class Zones{
public:
    //Empty zones for `world`. Physics thread.
    void Reset(std::shared_ptr<const ChasmWorld> world);
    std::shared_ptr<const ChasmWorld> GetWorld() const { return state.world; }

    //Applies one command. True if anything changed; otherwise `last_refusal` says why not.
    //`kind` is the ground, building or crop the op takes (ZoneOp); `stroke` the drag it is part of.
    bool Apply(int op, uint32_t index, int kind = 0, uint32_t stroke = 0);

    /*
        Empty zones for `world`, then a saved set put back through the rules, each building under
        its own id - so a save can only ever put the state where the rules allow. Returns how many
        plots, cells and grounds the rules refused, which for a save made on the same world is 0.
    */
    int Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<ZoneSavedBuilding>& buildings,
                uint32_t next_id, const std::vector<std::pair<int,int>>& grounds,
                uint32_t stroke, uint32_t stroke_building);
    std::string last_refusal;

    //The current state, read-only. Physics thread.
    const ZoneState& State() const { return state; }

private:
    ZoneState state;
    std::vector<int> coarse_across;             //per coarse quad, 4: the quad across each edge, or -1
    void Touch(int fine_quad);
    uint32_t NewBuilding(int kind);
    void SplitAfterRemoval(uint32_t id, int removed, bool f_field);
};

//A state's buildings as a save holds them - the inverse of Zones::Restore. Any thread, on a published copy.
std::vector<ZoneSavedBuilding> ZoneSaveBuildings(const ZoneState& z);

//The rules as questions, for the commands and for the view's hover (red when it would be refused).
//`why` may be NULL. A building of `kind` on `plot`; it joins `joining` (an id, or 0 for a new one).
bool ZoneCanBuild(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t joining, const char** why);
bool ZoneCanField(const ChasmWorld& w, const ZoneState& z, int coarse, const char** why);
//`kind` is the ground to be painted: a road has one rule more than a garden.
bool ZoneCanGround(const ChasmWorld& w, const ZoneState& z, int plot, const char** why,
                   int kind = ZONE_GROUND_GARDEN);

/*
    THE WINCH'S LANDING: the balcony vertex a winch on `plot` lowers to - of the corners of the cells
    round the plot that are balcony ground, the nearest (the lower index on a tie). -1 where there is
    none, which is also where no winch may stand. A property of the ground alone, so it is the same
    for the rules, the walkers and the look.
*/
int ZoneWinchLanding(const ChasmWorld& w, int plot);
//Every winch as (its plot, its landing), in plot order - the walkers' links between the levels.
void ZoneWinchLinks(const ChasmWorld& w, const ZoneState& z, std::vector<std::pair<int,int>>& out);

//Plot v's neighbours along fine edges, each once, in a fixed order. Returns how many (at most `max`).
int ZonePlotNeighbours(const ChasmWorld& w, int v, int* out, int max);

//Floor area, the capacity measure (README.md: plots differ in size, so never count plots).
struct ZoneStats{
    int buildings[ZONE_KIND_COUNT] = {};        //by kind
    float floor_area[ZONE_KIND_COUNT] = {};     //plot area x storeys; a field's its cells' area
    int built_plots = 0;
    int storeys = 0;
    int field_cells = 0;
    int gardens = 0;
    int towns = 0;
    int roads = 0;
    float ground_area = 0.0f;   //gardens and town ground
};
ZoneStats ComputeZoneStats(const ChasmWorld& w, const ZoneState& z);

//One building's figures, for the panel and the tools: its plots or cells, storeys, floor area.
struct ZoneBuildingInfo{
    uint32_t id = 0;
    int kind = ZONE_KIND_NONE;
    int crop = ZONE_CROP_WHEAT;
    int size = 0;
    int storeys = 0;            //summed over its plots
    float floor_area = 0.0f;
};
ZoneBuildingInfo ZoneBuildingFigures(const ChasmWorld& w, const ZoneState& z, uint32_t id);

#ifdef DEBUG
//The rules re-checked over the whole state - what the commands should have guaranteed.
void RunZoneChecks(const ChasmWorld& w, const ZoneState& z, GridCheckReport& report);
#endif

#endif

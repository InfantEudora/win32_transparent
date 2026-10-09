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
#include "Goods.h"

/*
    WHAT THE PLAYER PAINTED: the zones (grid_plan.md section 5). Simulation state - changed only by
    a command applied on a tick (ZoneOp), so a run can be replayed from the commands it was given.

      - BUILDINGS (docs/buildings_plan.md): houses, stores, workshops, water collectors on fine
        PLOTS, fields on COARSE CELLS. A building is a group with an identity of its own - an entry
        in the building table, which every plot (`building[v]`) and field cell (`field[c]`) it
        covers names by id. A plot grows in storeys (`storeys[v]`), whatever stands on it.
      - GROUND on fine PLOTS (step 8): `ground[v]`, a garden, a lot or road. A garden's and a lot's
        edge get a wall or a low fence (BoundaryMesh.h), and a building may stand on either, hiding it
        until the building is gone. A lot is a yard: a woodcutter keeps his woodpile on one.

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
      - A road is not painted over a garden or lot: it stops at the edge, where it makes a gate.
      - YARDS (docs/yards_plan.md): a garden or lot belongs to a house, a lot to a woodcutter - or to
        nobody (ZoneState::ground_owner). Painted for a building named in the command, each plot must touch
        it or its yard (ZoneCanYard); painted for none, it goes to what it touches (ZoneAutoOwner). A
        building put beside ground nobody owns takes it. A woodcutter piles on a lot of his own beside him
        (Economy): with none he fells nothing - he may still be built, as he could not before.
      - Not too steep (biomes_plan.md step 2): the ground may rise only so much across a plot (a road's
        limit is twice a house's) or a field's cell - ZONE_RISE_* in Zones.cpp. Not on the mountain.
      - A house is at most ZONE_HOUSE_MAX_PLOTS plots, since it holds one family.
      - At most a kind's own number of storeys (ZoneKindMaxStoreys).
      - A WATER COLLECTOR stands on the DRY plot at the edge of a river's wet bank (or a pool's): its own
        vertex dry, a corner of its cells wet. Its pipe reaches over the stones to the water (BuildingMesh),
        and its carrier walks to it like to any building.
      - A BRIDGE (docs/bridge_plan.md) is placed whole, by ZONE_OP_BRIDGE from a dry plot on one bank to
        one on the other: the plots between (ZoneBridgeChain) all wet, all on one ground, the water it
        crosses at most ZONE_BRIDGE_SPAN_RIVERS times the river's usual width, clear of a fall. The one
        building on wet plots. Once a plot of it stands, walkers cross it (ZoneBridgeWalkable).
      - A WINCH is one plot, on the rim right above a balcony: its plot on the plateau, balcony ground
        among its cells' corners and no open chasm - the one exception to "too close to a cliff". It
        lowers to its LANDING (ZoneWinchLanding), a balcony plot nothing may be built on.

    CONSTRUCTION (docs/construction_plan.md): of a plot's storeys, `standing[v]` are built; the rest
    are a CONSTRUCTION SITE. A debug command builds what it paints at once - standing = storeys on the
    plots it touches. A PLAY command (Zones::Apply's f_play) only plans: the storeys are the building's,
    and the rules and the walkers treat them as there, but nothing stands until the economy's builders
    raise it, a storey at a time (ZONE_OP_BUILD_RAISE), as the wood for it comes in. What a building
    does - its room, who it houses, whether it is worked - goes by what stands (ZoneBuildingFigures).
    A garden or a lot painted in play is a site the same way (`ground_built[v]` 0) until its wood is
    brought (ZONE_OP_GROUND_RAISE); until then it has no wall and encloses nothing. A ROAD painted in
    play is a site too (docs/line_works_plan.md), costing no wood: the idle fell the trees in its way and
    lay it, a plot at a time, through the same op. Until it is laid it holds its plot - the rules see a
    road there - but nothing else does: it is walked as open ground, opens no gate, makes no arch and is
    drawn as stakes (ZoneRoadLaid). A FIELD painted in play is a site too (fields_plan.md): its cells
    (`field_built`) are staked out until its wood is brought (ZONE_OP_FIELD_RAISE, all of them at once),
    and only then can it be sown.

    A FIELD'S CROPS are two choices of the player's (fields_plan.md): its STARTING crop (`crop`, none
    until chosen - an empty field waits for it) for its first sowing, and its NEXT crop (`next_crop`) for
    every sowing after, AUTO by default. What is growing, and how well, is the economy's (EconomyField).
    WHOSE LAND it is (land_plan.md) is a third choice, `land_of`: the house the player gave it to, 0 to
    leave it to the economy, which keeps the owner (EconomyField::owner).

    WALLS (line_works_plan.md, step 3): a palisade is ground too, ZONE_GROUND_WALL, drawn as a line the
    way a road is and built the same way, plot by plot - a road with a cost in wood. A wall plot that
    STANDS is shut to walkers (Walkers::EdgeOpen, the straight walks): a chain joined along fine edges
    cannot be crossed on this grid without passing through one of its plots. One still planned is open
    ground - a half-built wall has gaps, and a planned ring does not shut its builders in. It keeps off
    buildings and stops at a garden's or lot's edge, on a road's slope limit. Where a wall and a road
    cross, the plot stays ROAD and is marked a GATE (`gate`): a wall drawn over a road leaves it and gates
    it; a road drawn over a wall takes the plot and gates it. A gate has a DOOR (defence_plan.md, step 3),
    open as it is made; SHUT by the player (ZONE_OP_GATE_SHUT), it is closed to walkers as a standing wall
    plot is, the player's own people too (ZoneShutToWalkers).

    LINES: every road and wall plot painted in play carries the LINE it was drawn in (`line`), an id from
    the buildings' own counter, so lines and buildings share one placing order - the construction queue.

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
#define ZONE_KIND_CAMP          7       //tents and fires: a house's rules, a person a tent (docs/people_plan.md)
#define ZONE_KIND_BRIDGE        8       //across a river, bank to bank (docs/bridge_plan.md)
#define ZONE_KIND_BUILDERS      9       //a builders' hut: employs builders, staffed first (docs/builders_plan.md)
#define ZONE_KIND_RANGE         10      //an archery range, on coarse cells like a field: archers train there (docs/range_plan.md)
#define ZONE_KIND_COUNT         11
const char* ZoneKindName(int kind);
int ZoneKindByName(const std::string& name);    //-1 if none
//A kind that stands on coarse cells (ZoneState::field) rather than plots: a field, or a range painted like one.
inline bool ZoneKindOnCells(int kind){
    return kind == ZONE_KIND_FIELD || kind == ZONE_KIND_RANGE;
}
int ZoneKindMaxStoreys(int kind);

//What grows on a field. Never renumber.
#define ZONE_CROP_WHEAT         0
#define ZONE_CROP_GREENS        1
#define ZONE_CROP_BEANS         2
#define ZONE_CROP_COUNT         3
//Not crops, but what a field's choices may be instead (fields_plan.md): its starting crop not chosen yet,
//and a crop left to the field - the most food of what still ripens - for what follows a harvest, or for the
//start too, the player's or after a field has waited a day for one (issues_plan.md).
#define ZONE_CROP_NONE          255
#define ZONE_CROP_AUTO          254
const char* ZoneCropName(int crop);
int ZoneCropByName(const std::string& name);    //"none" and "auto" too; -1 if neither nor a crop

//What a plot's ground is. Never renumber - saves and recordings refer to these by value.
#define ZONE_GROUND_NONE    0
#define ZONE_GROUND_GARDEN  1       //green, walled: Townscaper's gardens
#define ZONE_GROUND_LOT     2       //a yard of trodden earth behind a low fence - a woodpile's
#define ZONE_GROUND_ROAD    3       //step 10: drawn along the grid's edges, walked fastest (docs/roads_plan.md)
#define ZONE_GROUND_WALL    4       //a palisade along a chain of plots, shut to walkers where it stands (line_works_plan.md)
#define ZONE_GROUND_COUNT   5
const char* ZoneGroundName(int ground);
//In a save's ground list, with the kind: a garden, lot, road or wall planned and not yet built (a site).
#define ZONE_GROUND_SAVED_SITE  16
//...and a road plot a wall runs through: its gate (ZoneState::gate).
#define ZONE_GROUND_SAVED_GATE  32
//...with its door shut (defence_plan.md).
#define ZONE_GROUND_SAVED_SHUT  64
//What ZoneState::gate holds on a plot: no gate, a gate with its door open, or shut.
#define ZONE_GATE_NONE      0
#define ZONE_GATE_OPEN      1
#define ZONE_GATE_SHUT      2

//Ground drawn as a LINE - a drag's chain of plots, placed whole: queued as one (ZoneState::line).
inline bool ZoneLineGround(int ground){
    return ground == ZONE_GROUND_ROAD || ground == ZONE_GROUND_WALL;
}

/*
    What a boundary encloses: a garden or lot plot with no house on it. A road is ground but not
    enclosed - it runs up to a wall from outside, which is where a gate goes (roads_plan.md) - and a
    house is its own boundary. A garden or lot still a site (ZoneState::ground_built) encloses nothing
    yet: its wall is not up - see ZoneBoundaryBetween.
*/
inline bool ZoneEnclosesGround(int ground){
    return ground == ZONE_GROUND_GARDEN || ground == ZONE_GROUND_LOT;
}

/*
    What stands between two plots joined by a fine edge: where exactly one of them is inside (a house,
    or enclosed ground) and that one is not a house, a garden's wall or a lot's fence. One rule
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
    value[1] the kind (ground, building or crop, by op), value[2] the stroke, value[3] the play flag
    and the owner a garden or lot is painted for (ZoneCommandValue3). Never renumber - the building
    ops are the house ops they grew from, so a recording from before them replays (a kind of 0 there
    means a house).
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
    ZONE_OP_FIELD_CROP,     //coarse cell: its whole field to the crop in value[1] (ZONE_CROP_*, or AUTO)
    ZONE_OP_STORE_ALLOW,    //plot: its store takes the goods in value[1], a mask of GOOD_BIT (economy_plan.md)
    ZONE_OP_BUILD_RAISE,    //plot: one more of its planned storeys stands - the builders' (construction_plan.md)
    ZONE_OP_BRIDGE,         //plot: a bridge from it to the plot in value[1], across the water (bridge_plan.md)
    ZONE_OP_GROUND_RAISE,   //plot: its garden or lot site is built - the builders' (construction_plan.md)
    ZONE_OP_SITE_PRIORITY,  //plot: its building's site goes first (value[1] 1) or back in the queue (0)
    ZONE_OP_FIELD_NEXT,     //coarse cell: its field's next crop to value[1], a crop or ZONE_CROP_AUTO (fields_plan.md)
    ZONE_OP_FIELD_RAISE,    //coarse cell: its field site's cell is built - the builders' (fields_plan.md)
    ZONE_OP_FIELD_LAND,     //coarse cell: its field given to the house in value[1], 0 automatic (land_plan.md)
    ZONE_OP_FIELD_PRIORITY, //coarse cell: its field's site goes first (value[1] 1) or back in the queue (0)
    ZONE_OP_GATE_SHUT,      //plot: its gate's door shut (value[1] 1) or open (0), and the gate plots joined to it (defence_plan.md)
    ZONE_OP_RANGE_PAINT,    //coarse cell: join the stroke's range, or start one (range_plan.md) - erased, raised, put first as a field
    ZONE_OP_COUNT
};
//Whether an op names a coarse cell (a field's or a range's) rather than a plot.
inline bool ZoneOpOnCell(int op){
    return op == ZONE_OP_FIELD_PAINT || op == ZONE_OP_FIELD_ERASE || op == ZONE_OP_FIELD_CROP || op == ZONE_OP_FIELD_NEXT
        || op == ZONE_OP_FIELD_RAISE || op == ZONE_OP_FIELD_LAND || op == ZONE_OP_FIELD_PRIORITY || op == ZONE_OP_RANGE_PAINT;
}
const char* ZoneOpName(int op);
int ZoneOpByName(const std::string& name);  //the names above, and the old house_* ones; -1 if none
/*
    A zone command's value[3]: the PLAY flag in bit 0, and above it the building a garden or lot is
    painted for (yards_plan.md), 0 for whatever it touches. Every recording before yards has 0 or 1 there
    - no owner - so it means what it meant. Exact while owner * 2 stays under a float's 2^24.
*/
inline float ZoneCommandValue3(bool f_play, uint32_t owner){
    return (float)(owner * 2u + (f_play ? 1u : 0u));
}
inline bool ZoneCommandPlay(float v3){
    return ((uint32_t)v3 & 1u) != 0;
}
inline uint32_t ZoneCommandOwner(float v3){
    return (uint32_t)v3 >> 1;
}

struct ChasmWorld;     //ChasmWorld.h

/*
    One building: its kind, what a field grows, and how many plots or cells it covers (0: gone).
    `allow` is what a STORE takes when it stands on its own - the player's setting, all goods by
    default; next to a workplace it takes that workplace's goods instead (Economy.h). `f_priority` is
    the player's "build this first" on a construction site: its carriers come before any other site's,
    however far (construction_plan.md); it drops by itself when nothing of the building is left to
    build, so a storey added later is queued like any other. `split_from` is the building a piece was
    split off by an erase, for the economy to share its stock out by; not saved, since the economy has
    done that by the time anything is.
*/
struct ZoneBuilding{
    uint8_t kind = ZONE_KIND_NONE;
    uint8_t crop = ZONE_CROP_WHEAT;             //a field's starting crop; ZONE_CROP_NONE until chosen
    GoodsMask allow = GOODS_ALL;
    uint8_t f_priority = 0;
    uint8_t next_crop = ZONE_CROP_AUTO;         //a field's crop for every sowing after its first
    int size = 0;
    uint32_t split_from = 0;
    uint32_t land_of = 0;                       //a field: the house the player gave it to, 0 automatic
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
    std::vector<uint8_t> standing;              //per fine vertex: of those, how many are built - the rest a site's
    std::vector<uint8_t> ground;                //per fine vertex, ZONE_GROUND_*
    std::vector<uint8_t> ground_built;          //per fine vertex: 0 for a garden, lot, road or wall still a site
    std::vector<uint32_t> line;                 //per fine vertex: a road's or wall's line - its place in the queue, 0 none
    std::vector<uint8_t> gate;                  //per fine vertex: on a road plot a wall runs through, its door (ZONE_GATE_*)
    std::vector<uint32_t> ground_owner;         //per fine vertex: the building a garden or lot is the yard of, 0 nobody (ZoneGroundOwner)
    std::vector<uint32_t> field;                //per coarse quad: the field - or range (ZoneKindOnCells) - it is part of, 0 none
    std::vector<uint8_t> field_built;           //per coarse quad: 0 for a field's cell still a site
    //The drag in progress: its stroke number and the building it is making (0: none yet).
    uint32_t stroke = 0;
    uint32_t stroke_building = 0;
    /*
        The line being placed: its stroke and the id it was given. A line is sent whole on release, in
        one batch, so it never outlives the tick it is placed on - neither is saved.
    */
    uint32_t line_stroke = 0;
    uint32_t line_id = 0;
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
    int allow = GOODS_ALL;                      //a store's own setting
    bool f_priority = false;                    //a site the player put first
    int next_crop = ZONE_CROP_AUTO;             //a field's (fields_plan.md)
    uint32_t land_of = 0;                       //a field's, given to a house (land_plan.md)
    std::vector<std::pair<int,int>> plots;      //plot, storeys
    std::vector<int> standing;                  //per plot above, what of it stands - empty: all of it
    std::vector<int> cells;                     //a field's coarse cells
    std::vector<int> cells_built;               //per cell above, 1 if built - empty: all of them
};

//A plot's ground as a save holds it: the kind with ZONE_GROUND_SAVED_SITE / _GATE added, its line, and
//the building whose yard it is.
struct ZoneSavedGround{
    int plot = 0;
    int kind = ZONE_GROUND_NONE;
    uint32_t line = 0;
    uint32_t owner = 0;
};

class Zones{
public:
    //Empty zones for `world`. Physics thread.
    void Reset(std::shared_ptr<const ChasmWorld> world);
    std::shared_ptr<const ChasmWorld> GetWorld() const { return state.world; }

    //Applies one command. True if anything changed; otherwise `last_refusal` says why not.
    //`kind` is the ground, building or crop the op takes (ZoneOp); `stroke` the drag it is part of.
    //`f_play`: a player's command, whose building is a construction site until it is raised. `owner`: the
    //building a garden or lot is painted for (yards_plan.md), 0 for whatever it touches.
    bool Apply(int op, uint32_t index, int kind = 0, uint32_t stroke = 0, bool f_play = false, uint32_t owner = 0);

    /*
        Empty zones for `world`, then a saved set put back through the rules, each building under
        its own id - so a save can only ever put the state where the rules allow. Returns how many
        plots, cells and grounds the rules refused, which for a save made on the same world is 0.
    */
    int Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<ZoneSavedBuilding>& buildings,
                uint32_t next_id, const std::vector<ZoneSavedGround>& grounds,
                uint32_t stroke, uint32_t stroke_building);
    std::string last_refusal;

    /*
        A new colony's camp: from ZoneStartCampPlot outward, plot by plot in the order a flood reaches
        them, every plot the camp may take, until it has `tents` tents. Through the same rules as a
        player's camp. Physics thread, on empty zones - a new map, not a load.
    */
    void PlaceStartCamp(int tents);

    //The current state, read-only. Physics thread.
    const ZoneState& State() const { return state; }

private:
    ZoneState state;
    std::vector<int> coarse_across;             //per coarse quad, 4: the quad across each edge, or -1
    void Touch(int fine_quad);
    uint32_t NewBuilding(int kind);
    void SplitAfterRemoval(uint32_t id, int removed, bool f_field);
    void SettlePriorities();
};

//A state's buildings as a save holds them - the inverse of Zones::Restore. Any thread, on a published copy.
std::vector<ZoneSavedBuilding> ZoneSaveBuildings(const ZoneState& z);

//The rules as questions, for the commands and for the view's hover (red when it would be refused).
//`why` may be NULL. A building of `kind` on `plot`; it joins `joining` (an id, or 0 for a new one).
bool ZoneCanBuild(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t joining, const char** why);
/*
    YARDS (docs/yards_plan.md): the gardens and lots a building owns. A house owns gardens and lots, a
    woodcutter lots - his woodpile's; no other kind, and no road or wall, is ever owned.
*/
bool ZoneOwnsGround(int kind, int ground);              //a building of `kind` may have `ground` in its yard
inline bool ZoneKindHasYard(int kind){
    return kind == ZONE_KIND_HOUSE || kind == ZONE_KIND_WOODCUTTER;
}
//Whose yard plot v is: its owner while that building stands and may own it, else 0.
uint32_t ZoneGroundOwner(const ZoneState& z, int v);
//`ground` painted on `plot` for building `owner`: it must touch the building or its yard, be ground it may
//own, and not be another building's yard. True for owner 0, and for a road or a wall: nothing to ask.
bool ZoneCanYard(const ChasmWorld& w, const ZoneState& z, int plot, int ground, uint32_t owner, const char** why);
//Whose yard `ground` painted on `plot` for nobody in particular joins: a building it touches that may own it,
//the lowest id; else the owner of yard ground it touches; else 0.
uint32_t ZoneAutoOwner(const ChasmWorld& w, const ZoneState& z, int plot, int ground);
//How many plots of `ground` building `id`'s yard has.
int ZoneYardPlots(const ZoneState& z, uint32_t id, int ground);
//Ground that encloses and is built: a wall or fence round it, walked round (ZoneBoundaryBetween).
inline bool ZoneGroundStands(const ZoneState& z, int v){
    return ZoneEnclosesGround(z.ground[v]) && z.ground_built[v];
}
bool ZoneCanField(const ChasmWorld& w, const ZoneState& z, int coarse, const char** why);
//`kind` is the ground to be painted: a road has one rule more than a garden, and a wall a road's rules.
bool ZoneCanGround(const ChasmWorld& w, const ZoneState& z, int plot, const char** why,
                   int kind = ZONE_GROUND_GARDEN);

/*
    THE WINCH'S LANDING: the balcony vertex a winch on `plot` lowers to - of the corners of the cells
    round the plot that are balcony ground, the nearest (the lower index on a tie). -1 where there is
    none, which is also where no winch may stand. A property of the ground alone, so it is the same
    for the rules, the walkers and the look.
*/
int ZoneWinchLanding(const ChasmWorld& w, int plot);

/*
    A RANGE CELL's layout (range_plan.md), one rule for the trainees and the look: every range shoots NORTH,
    the way the camera looks - its targets at each cell's north side face the view with their rings, and its
    trainees stand on a line at the south side, their backs to it. `middle` is the plot at the cell's middle,
    where a walk to the cell ends (as a farmer's does) before the last straight step to the line.
*/
#define ZONE_RANGE_LINE         0.30f   //the shooting line, this much of the cell's width south of its middle
#define ZONE_RANGE_TARGET       0.28f   //the target, this much north of it
struct ZoneRangeCell{
    vec2 centre;
    vec2 dir;               //the way it shoots: north, (0, -1)
    float size = 0.0f;      //about the cell's width
    int middle = -1;
};
ZoneRangeCell ZoneRangeCellAt(const ChasmWorld& w, int cell);
inline vec2 ZoneRangeLine(const ZoneRangeCell& r){
    return r.centre - r.dir * (r.size * ZONE_RANGE_LINE);
}
inline vec2 ZoneRangeTarget(const ZoneRangeCell& r){
    return r.centre + r.dir * (r.size * ZONE_RANGE_TARGET);
}
//Every winch as (its plot, its landing), in plot order - the walkers' links between the levels.
void ZoneWinchLinks(const ChasmWorld& w, const ZoneState& z, std::vector<std::pair<int,int>>& out);

/*
    THE CAMP (docs/people_plan.md): the settlers' first home - tents and fires, built by a house's rules
    but one storey high and one person to a plot instead of three to a storey. A plot of a camp has a
    tent on it, or - one in ZONE_CAMP_FIRE_EVERY, by the plot's own hash - a fire. A new map starts
    with one, at ZoneStartCampPlot, just big enough for its settlers (Zones::PlaceStartCamp); more can
    be painted.
*/
#define ZONE_CAMP_FIRE_EVERY    5
bool ZoneCampFire(int plot);
//Which way a camp plot's tent faces (radians), by the plot's hash - the look and the people agree on it.
float ZoneCampYaw(int plot);
//Where someone living in that tent stands when he is home: out in front of its door. People in a camp
//are never indoors, so a camp full of them shows there are too few houses (the user, 2026-10-06).
vec2 ZoneCampDoor(const ChasmWorld& w, int plot);
//How many a camp holds: its tents.
int ZoneCampTents(const ZoneState& z, uint32_t id);
/*
    A spot a new colony might camp on, scored (Zones.cpp, WHERE A NEW COLONY CAMPS) - every one looked at,
    refused ones too, so the debug view and chasm_camp can show why the camp went where it did.
*/
#define ZONE_CAMP_SURVEY    24.0f   //the ground a spot is scored on: what a walk from it reaches within this
struct ZoneCampSpot{
    int plot = -1;
    const char* refused = NULL;     //why it may not be the camp; NULL if it may
    bool f_fallback = false;        //not by a river: none there passed
    float score = 0.0f;             //the parts below, weighted
    float flat = 0.0f;              //0-1: the house share and the field share, averaged
    float trees = 0.0f;             //0-1: 1 with the best share wooded
    float middle = 0.0f;            //0-1: 1 level with the map's middle
    float house_share = 0.0f;       //of a whole survey disc, the share a house may stand on
    float field_share = 0.0f;       //...and a field lie on
    float tree_share = 0.0f;        //of the survey's plots, the share with a tree on
};
void ZoneCampSpots(const ChasmWorld& w, int tents, std::vector<ZoneCampSpot>& out);
//Where a new colony of `tents` camps: the best of ZoneCampSpots. -1 if nowhere.
int ZoneStartCampPlot(const ChasmWorld& w, int tents);

/*
    BRIDGES (docs/bridge_plan.md). The plots a bridge from `from` to `to` would take - a chain along fine
    edges, each step the neighbour nearest `to` that stays near the straight line - and whether it may
    stand there: both ends dry, everything between wet, one ground throughout, nothing built on it, the
    water crossed (the span less the two banks' wet margins) at most ZONE_BRIDGE_SPAN_RIVERS times the
    usual width of the river it crosses, and clear of a fall. `plots` is filled either way, for the view.
*/
#define ZONE_BRIDGE_SPAN_RIVERS 2.0f        //the user's: up to twice a river's usual width
#define ZONE_BRIDGE_DECK        0.30f       //the deck over its level's ground
bool ZoneBridgeChain(const ChasmWorld& w, const ZoneState& z, int from, int to, std::vector<int>& plots, const char** why);
//A bridge plot that stands: walked across, wet as it is.
inline bool ZoneBridgeWalkable(const ZoneState& z, int v){
    return z.KindOf(v) == ZONE_KIND_BRIDGE && z.standing[v] > 0;
}
//A road plot that is LAID - not one still a site, planned in play (line_works_plan.md). Gates and arches.
inline bool ZoneRoadLaid(const ZoneState& z, int v){
    return z.ground[v] == ZONE_GROUND_ROAD && z.ground_built[v];
}
/*
    A ROAD THAT STANDS, for walking and for a road's drawn shape: a laid road plot - or a standing
    bridge's, which is a road over the water (bridge_plan.md), so a road runs onto it unbroken. Not for
    the rules that mean "painted as road" (no house on one, no road twice): those test the ground
    itself, and see a planned road as much as a laid one.
*/
inline bool ZoneRoadStands(const ZoneState& z, int v){
    return ZoneRoadLaid(z,v) || ZoneBridgeWalkable(z,v);
}
//A road plot still to be laid: painted in play, its trees perhaps still standing.
inline bool ZoneRoadPlanned(const ZoneState& z, int v){
    return z.ground[v] == ZONE_GROUND_ROAD && !z.ground_built[v];
}
//A wall plot that STANDS: shut to walkers (Zones.h, WALLS).
inline bool ZoneWallStands(const ZoneState& z, int v){
    return z.ground[v] == ZONE_GROUND_WALL && z.ground_built[v];
}
//A wall plot still to build: open ground, staked out.
inline bool ZoneWallPlanned(const ZoneState& z, int v){
    return z.ground[v] == ZONE_GROUND_WALL && !z.ground_built[v];
}
//A gate with its door SHUT (defence_plan.md).
inline bool ZoneGateShut(const ZoneState& z, int v){
    return z.gate[v] == ZONE_GATE_SHUT;
}
//Closed to walkers - never walked onto, nor through: a standing wall plot, or a shut gate (Walkers, the
//economy's straight walks).
inline bool ZoneShutToWalkers(const ZoneState& z, int v){
    return ZoneWallStands(z,v) || ZoneGateShut(z,v);
}

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
    int lots = 0;
    int roads = 0;
    int walls = 0;
    float ground_area = 0.0f;   //gardens and lots
};
ZoneStats ComputeZoneStats(const ChasmWorld& w, const ZoneState& z);

//One building's figures, for the panel and the tools: its plots or cells, storeys, floor area.
struct ZoneBuildingInfo{
    uint32_t id = 0;
    int kind = ZONE_KIND_NONE;
    int crop = ZONE_CROP_WHEAT;
    int allow = GOODS_ALL;
    int size = 0;
    int storeys = 0;            //summed over its plots: standing ones
    float floor_area = 0.0f;    //standing - what it holds and houses
    int site_storeys = 0;       //planned and not yet standing: a construction site's
    float site_area = 0.0f;
    int site_cells = 0;         //a field's cells still a site
    bool f_priority = false;    //the site goes first
    int next_crop = ZONE_CROP_AUTO;
};
ZoneBuildingInfo ZoneBuildingFigures(const ChasmWorld& w, const ZoneState& z, uint32_t id);

#ifdef DEBUG
//The rules re-checked over the whole state - what the commands should have guaranteed.
void RunZoneChecks(const ChasmWorld& w, const ZoneState& z, GridCheckReport& report);
#endif

#endif

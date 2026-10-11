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
    with a new one. Separate strokes stay separate buildings even where they touch. A building NAMED in
    the command (the selected one - its card's Extend, the user's 2026-10-10) is extended the way a yard
    is painted: each plot must join it (ZoneCanExtend), or is refused, never a new building. Erasing a
    plot can leave a building in pieces: the largest keeps its id, each other piece becomes a building of
    its own (Zones::Apply).

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
      - A house is at most ZONE_HOUSE_MAX_PLOTS plots, since it holds one family; a ropewalk at most
        ZONE_ROPEWALK_MAX_PLOTS, past which a longer one makes no more rope (ZoneKindMaxPlots).
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
      - A GUARD TOWER (docs/defence_plan.md) is one plot, painted whole - both its storeys at once - by a
        building's rules, and also IN A WALL'S LINE: painted on a wall plot, planned or standing, it takes
        that plot over (the wall's ground goes), the palisade ending against it as against a gate's post.
        A building stops walkers, so the wall stays shut. Never on a road, so never on a gate. It needs a
        side its LADDER can stand on (ZoneTowerLadderOk), and nothing is built, nor a wall drawn, on the plot
        a tower's ladder stands toward - as on a winch's landing.

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
#define ZONE_ROPEWALK_MAX_PLOTS 4       //goods_plan.md: it works from 2, and a 3rd and a 4th make more rope

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
#define ZONE_KIND_TOWER         11      //a guard tower: one plot, two storeys, its guards on top (docs/defence_plan.md)
#define ZONE_KIND_ROPEWALK      12      //a long low shed: a ropemaker makes rope from hemp (docs/goods_plan.md)
#define ZONE_KIND_COUNT         13
const char* ZoneKindName(int kind);
int ZoneKindByName(const std::string& name);    //-1 if none
//The most plots a building of `kind` may have, 0 for no limit: a house's one family, a ropewalk's length.
inline int ZoneKindMaxPlots(int kind){
    return (kind == ZONE_KIND_HOUSE) ? ZONE_HOUSE_MAX_PLOTS : (kind == ZONE_KIND_ROPEWALK) ? ZONE_ROPEWALK_MAX_PLOTS : 0;
}
//A kind that stands on coarse cells (ZoneState::field) rather than plots: a field, or a range painted like one.
inline bool ZoneKindOnCells(int kind){
    return kind == ZONE_KIND_FIELD || kind == ZONE_KIND_RANGE;
}
//A workplace the player may STOP (stopping_work_plan.md, the user's 2026-10-12): its places go while it is, so its
//people are free for other work. Not a field (a family's land), a guard tower (its guard count) nor a camp.
inline bool ZoneKindStoppable(int kind){
    return kind == ZONE_KIND_WATER || kind == ZONE_KIND_WOODCUTTER || kind == ZONE_KIND_ROPEWALK
           || kind == ZONE_KIND_BUILDERS || kind == ZONE_KIND_RANGE;
}
//A kind drawn as a HOUSE BODY (BuildingMesh.cpp, BuildHouseCell): walls and a roof, storeys high - not a winch, a
//camp, a bridge or a guard tower, each drawn apart, nor a kind on cells. Only these are bridged by an ARCH.
inline bool ZoneKindHasBody(int kind){
    return kind == ZONE_KIND_HOUSE || kind == ZONE_KIND_STORE || kind == ZONE_KIND_WOODCUTTER
           || kind == ZONE_KIND_WATER || kind == ZONE_KIND_BUILDERS || kind == ZONE_KIND_ROPEWALK;
}
int ZoneKindMaxStoreys(int kind);
//The storeys a plot of `kind` is painted with: one, a click adding more - but a GUARD TOWER whole, all its storeys
//at once, since it is only ever that high (defence_plan.md).
inline int ZoneKindPaintStoreys(int kind){
    return (kind == ZONE_KIND_TOWER) ? ZoneKindMaxStoreys(kind) : 1;
}

//What grows on a field. Never renumber. Its good is GoodOfCrop's (Goods.h).
#define ZONE_CROP_WHEAT         0
#define ZONE_CROP_GREENS        1
#define ZONE_CROP_BEANS         2
#define ZONE_CROP_HEMP          3       //not food: spun into rope (goods_plan.md) - never AUTO's pick
#define ZONE_CROP_COUNT         4
//Not crops, but what a field's choices may be instead (fields_plan.md): its starting crop not chosen yet,
//and a crop left to the field - the most food of what still ripens, so never hemp - for what follows a
//harvest, or for the start too, the player's or after a field has waited a day for one (issues_plan.md).
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
    ARCHES (docs/roads_plan.md, Townscaper's; docs/arches_plan.md): a road plot running between buildings of two
    storeys or more - two road neighbours, and every other neighbour such a building, ALL OF ONE KIND and a kind with
    a house body (ZoneKindHasBody: never a guard tower) - is built over: their upper storeys bridge it, up to the
    lower of them, over a passage one storey high - where such plots run at most three in a row, a hole through a
    row of houses rather than a covered street; an arch with a floor built (ZoneState::arch_built) stays whatever its
    run grows to (step 4). Never over a gate: its posts and door are taller than the passage.
    The plot stays a road, walked like any other. 0 if `plot` is no arch.
*/
int ZoneArchStoreys(const ChasmWorld& w, const ZoneState& z, int plot);
/*
    The building an arch is PART OF (arches_plan.md, the user's): the one beside it on both sides if it is one;
    otherwise the one with the most plots beside it, the older (lower id) on a tie. Drawn in its look, picked and
    outlined with it. 0 if `plot` is no arch.
*/
uint32_t ZoneArchOwner(const ChasmWorld& w, const ZoneState& z, int plot);
//An arch's FLOOR (arches_plan.md step 2): its storeys over the passage, one less than it rises to - its owner's,
//counted in with its own (ZoneBuildingFigures; the economy's plot-storeys).
inline int ZoneArchFloors(int arch_storeys){
    return (arch_storeys > 1) ? arch_storeys - 1 : 0;
}

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
    ZONE_OP_TOWER_LADDER,   //plot: its guard tower's ladder toward the neighbour plot in value[1], -1 the next open side round (defence_plan.md)
    ZONE_OP_CASTLE,         //plot: the debug castle's piece in value[1] (ZoneCastlePiece), 0 to take it off (ramparts_plan.md)
    ZONE_OP_WORK_STOP,      //plot: its workplace stopped (value[1] 1) or working again (0) (ZoneKindStoppable, stopping_work_plan.md)
    ZONE_OP_RANGE_STOP,     //coarse cell: the same for its range, which stands on cells
    ZONE_OP_COUNT
};
//Whether an op names a coarse cell (a field's or a range's) rather than a plot.
inline bool ZoneOpOnCell(int op){
    return op == ZONE_OP_FIELD_PAINT || op == ZONE_OP_FIELD_ERASE || op == ZONE_OP_FIELD_CROP || op == ZONE_OP_FIELD_NEXT
        || op == ZONE_OP_FIELD_RAISE || op == ZONE_OP_FIELD_LAND || op == ZONE_OP_FIELD_PRIORITY || op == ZONE_OP_RANGE_PAINT
        || op == ZONE_OP_RANGE_STOP;
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
    done that by the time anything is. `f_stopped` is the player's "stop work" on a workplace
    (ZoneKindStoppable): it has no places while set, so its worker goes free (stopping_work_plan.md).
*/
struct ZoneBuilding{
    uint8_t kind = ZONE_KIND_NONE;
    uint8_t crop = ZONE_CROP_WHEAT;             //a field's starting crop; ZONE_CROP_NONE until chosen
    GoodsMask allow = GOODS_ALL;
    uint8_t f_priority = 0;
    uint8_t f_stopped = 0;
    uint8_t next_crop = ZONE_CROP_AUTO;         //a field's crop for every sowing after its first
    int size = 0;
    uint32_t split_from = 0;
    uint32_t land_of = 0;                       //a field: the house the player gave it to, 0 automatic
    int ladder = -1;                            //a guard tower: the neighbour plot its ladder stands toward, its door
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
    /*
        ARCHES (arches_plan.md), per fine vertex. Worked out from the above after every change (Zones::RefreshArches),
        never saved: the storeys an arch may rise to (ZoneArchStoreys, 0 none) and the building it is part of
        (ZoneArchOwner). Kept, saved and hashed (step 3): its floors BUILT over the passage - up to one less than it
        may rise to; what is planned and not built is its owner's site, with scaffolding over the road. Apart from
        `storeys` and `standing`, which say what is built on the ground. Empty in a state made for drawing one
        building alone - no arches there but what it is given.
    */
    std::vector<uint8_t> arch;
    std::vector<uint32_t> arch_owner;
    std::vector<uint8_t> arch_built;
    std::vector<uint8_t> castle;                //per fine vertex: the DEBUG CASTLE's piece on it (ZoneCastlePiece), 0 none
    std::vector<uint8_t> castle_over;           //...and the piece over that one (ZoneCastlePiece): an arch, or a stair on an arch
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
    //An arch's storeys over `plot`, and the building it is part of: 0 for none, or with no arches worked out.
    int ArchOf(int plot) const{
        return (plot >= 0 && plot < (int)arch.size()) ? arch[plot] : 0;
    }
    uint32_t ArchOwnerOf(int plot) const{
        return (plot >= 0 && plot < (int)arch_owner.size()) ? arch_owner[plot] : 0;
    }
    //Its floors built, and the storeys that built part rises to - the passage and those floors, 0 with none built.
    int ArchBuiltOf(int plot) const{
        return (plot >= 0 && plot < (int)arch_built.size()) ? arch_built[plot] : 0;
    }
    int ArchStandingOf(int plot) const{
        return ArchBuiltOf(plot) ? 1 + ArchBuiltOf(plot) : 0;
    }
};

//A building as a save holds it: everything Zones::Restore needs to put it back as it was.
struct ZoneSavedBuilding{
    uint32_t id = 0;
    int kind = ZONE_KIND_HOUSE;
    int crop = ZONE_CROP_WHEAT;
    int allow = GOODS_ALL;                      //a store's own setting
    bool f_priority = false;                    //a site the player put first
    bool f_stopped = false;                     //a workplace the player stopped
    int next_crop = ZONE_CROP_AUTO;             //a field's (fields_plan.md)
    uint32_t land_of = 0;                       //a field's, given to a house (land_plan.md)
    int ladder = -1;                            //a guard tower's (defence_plan.md); -1 for one from before: its default
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
    int arch = 0;                               //a road plot's arch: its floors built (arches_plan.md step 3)
};

class Zones{
public:
    //Empty zones for `world`. Physics thread.
    void Reset(std::shared_ptr<const ChasmWorld> world);
    std::shared_ptr<const ChasmWorld> GetWorld() const { return state.world; }

    //Applies one command. True if anything changed; otherwise `last_refusal` says why not.
    //`kind` is the ground, building or crop the op takes (ZoneOp); `stroke` the drag it is part of.
    //`f_play`: a player's command, whose building is a construction site until it is raised. `owner`: the
    //building a garden or lot is painted for (yards_plan.md), or a building painted to extend; 0 for neither.
    bool Apply(int op, uint32_t index, int kind = 0, uint32_t stroke = 0, bool f_play = false, uint32_t owner = 0);
    /*
        A TENT PITCHED (docs/settlements_plan.md, Economy::PitchSpot): a camp plot on `plot`, standing at once - it was
        packed, not built - joining camp `camp`, or a camp of its own with 0. The stroke in progress is kept, so a
        drag the player is making goes on as it was. False, and last_refusal says why, if the rules refuse it.
    */
    bool PitchTent(int plot, uint32_t camp);

    /*
        Empty zones for `world`, then a saved set put back through the rules, each building under
        its own id - so a save can only ever put the state where the rules allow. Returns how many
        plots, cells and grounds the rules refused, which for a save made on the same world is 0.
    */
    int Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<ZoneSavedBuilding>& buildings,
                uint32_t next_id, const std::vector<ZoneSavedGround>& grounds,
                uint32_t stroke, uint32_t stroke_building);
    //The debug castle's pieces back (ramparts_plan.md), as (plot, piece) painted in order - an arch over a piece
    //after it - through the rules; after Restore. Returns how many were refused.
    int RestoreCastle(const std::vector<std::pair<int,int>>& pieces);
    std::string last_refusal;
    //The arch floors that came down with the last command (arches_plan.md step 4): a player's is told so.
    int last_arch_lost = 0;

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
    /*
        ZoneState::arch and arch_owner worked out again over every road plot - after a command, a load, a new map -
        and arch_built kept to what may stand. `f_stand`: a debug placement's, where what it makes stands at once -
        an arch's new floors with it; otherwise they are its owner's site. Returns the built floors that came down.
    */
    int RefreshArches(bool f_stand);
};

//A state's buildings as a save holds them - the inverse of Zones::Restore. Any thread, on a published copy.
std::vector<ZoneSavedBuilding> ZoneSaveBuildings(const ZoneState& z);

//The rules as questions, for the commands and for the view's hover (red when it would be refused).
//`why` may be NULL. A building of `kind` on `plot`; it joins `joining` (an id, or 0 for a new one).
bool ZoneCanBuild(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t joining, const char** why);
//The plot at a cell's corner between `plot` and building `joining` across that cell, free and buildable for
//it - brought in first so a diagonal step keeps the building one piece. -1 if there is none.
int ZoneJoinCorner(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t joining);
//A building of `kind` painted on `plot` to extend building `owner`: it stands and is of `kind`, `plot` joins it
//(beside it, or through ZoneJoinCorner) and a house has room for what that adds. True for owner 0, and on a
//built plot: a click there is a storey of whatever stands on it.
bool ZoneCanExtend(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t owner, const char** why);
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

/*
    A GUARD TOWER (docs/defence_plan.md, tuned 2026-10-10) takes its JUNCTION's shape, as any building takes its
    plot's: a polygon through the middles of the cells round its plot's point, pulled in toward it
    (ZONE_TOWER_INSET), a side facing each neighbour along a fine edge. The junction decides its GUARDS - its edges
    less two: one on a 3-junction, two on the 4s that are nearly all of the grid, three on a 5, four on a 6. Each
    has a PLACE on its platform toward a corner, spread round it (ZONE_TOWER_STAND of the way). Its LADDER is its
    DOOR (ZoneBuilding::ladder): walked onto and off only from the neighbour it stands toward (Walkers, the
    straight walks) - when placed, the open side nearest the camp, which is usually a wall's inside; moved by
    ZONE_OP_TOWER_LADDER. One rule for the walkers, the economy and the look.
*/
#define ZONE_TOWER_INSET        0.6f    //its walls' corners, this much of the way to the cells' middles
#define ZONE_TOWER_STAND        0.5f    //a guard's place, this much of the way from its middle to a corner
int ZoneTowerGuards(const ChasmWorld& w, int plot);
//Its walls' corners round `plot`, by angle.
void ZoneTowerCorners(const ChasmWorld& w, int plot, std::vector<vec2>& out);
//Where the guard of `rank` (from 0, in id order) stands on it: the middle for a lone one.
vec2 ZoneTowerPlace(const ChasmWorld& w, int plot, int rank);
//Whether its ladder may stand toward neighbour `to`: walkable from the tower - one ground, dry, not too steep - and
//with nothing on it a walker cannot cross or that could shut: no building, wall or gate.
bool ZoneTowerLadderOk(const ChasmWorld& w, const ZoneState& z, int tower, int to);
//Where its ladder goes when placed: the open neighbour nearest the camp - with none, the lowest. -1 none open.
int ZoneTowerDefaultLadder(const ChasmWorld& w, const ZoneState& z, int tower);
//The plot the ladder of a tower on `plot` stands toward, -1 for no tower.
inline int ZoneTowerLadder(const ZoneState& z, int plot){
    const ZoneBuilding* b = z.BuildingOf(plot);
    return (b && b->kind == ZONE_KIND_TOWER) ? b->ladder : -1;
}
//A tower's ladder stands toward `plot`: nothing is built or walled there.
bool ZoneIsLadderPlot(const ChasmWorld& w, const ZoneState& z, int plot);
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
    but one storey high and one person to a plot instead of three to a storey. Every plot of a camp has a
    tent on it - a tent is a tent, never a fire in its place (the user's, 2026-10-11). Its FIRES are props
    round the tents: in a cell at the camp's edge with a standing tent on one corner and open ground on the
    other three, one such cell in ZONE_CAMP_FIRE_EVERY by the cell's hash. A new map starts with one, at
    ZoneStartCampPlot, just big enough for its settlers (Zones::PlaceStartCamp); more can be painted.
*/
#define ZONE_CAMP_FIRE_EVERY    5
//The tent plot whose fire fine cell `fine_quad` holds, or -1 for none; and where in the cell it burns.
int ZoneCampFireTent(const ChasmWorld& w, const ZoneState& z, int fine_quad);
vec2 ZoneCampFireAt(const ChasmWorld& w, int fine_quad, int tent);
//Which way a camp plot's tent faces (radians), by the plot's hash - the look and the people agree on it.
float ZoneCampYaw(int plot);
//Where someone living in that tent stands when he is home: out in front of its door. People in a camp
//are never indoors, so a camp full of them shows there are too few houses (the user, 2026-10-06).
vec2 ZoneCampDoor(const ChasmWorld& w, int plot);
//How many a camp holds: its tents, a plot each.
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

/*
    THE DEBUG CASTLE (docs/ramparts_plan.md, step 2): a PROTOTYPE, to find out whether walls people walk on read from
    the game's camera. Grey stone pieces painted in debug - refused in play - one a plot, each a SHAPE and a height in
    storeys, kept apart from the buildings and the ground: nothing builds one or pays for it, and nothing but the
    walkers and the drawing reads it.
      - BLOCK: solid stone, a deck on top. Its ground shut.
      - ARCH: a deck on pillars over an open passage - a road runs on under it. Two storeys at least, so it clears a
        gate's posts.
      - STAIR: rises a storey to its height, from the one below - from the ground, for a first storey. Its ground
        shut: the stair is the way.
    And a piece OVER the plot's piece (`castle_over`, the user's, 2026-10-10), painted on it:
      - an ARCH at least ZONE_CASTLE_HEADROOM storeys over the piece's top, its pillars standing on it - a second
        deck on the plot, so a wall-walk passes under another;
      - a STAIR ON AN ARCH, a storey over it: it rises from the arch's top to its own, over the open passage, and
        covers the arch's deck - so a stair climbs over a road that runs on under it.
    A piece painted where one stands goes on it when it is one of those, and takes its place when not; a piece under
    another is only ever swapped for one that still carries it. Taken off from the top down.
    Their decks are walked by the posted only (Walkers.h, LAYERS).
*/
#define ZONE_CASTLE_NONE        0
#define ZONE_CASTLE_BLOCK       1
#define ZONE_CASTLE_ARCH        2
#define ZONE_CASTLE_STAIR       3
#define ZONE_CASTLE_SHAPES      4
#define ZONE_CASTLE_MAX_STOREYS 4
#define ZONE_CASTLE_ARCH_LOWEST 2
#define ZONE_CASTLE_HEADROOM    2       //storeys from a piece's top to an arch over it: a walker under it clears its slab
//A piece as `castle` holds it, and a ZONE_OP_CASTLE's value[1]: its shape and storeys.
inline uint8_t ZoneCastlePiece(int shape, int storeys){
    return (uint8_t)((shape << 4) | (storeys & 15));
}
inline int ZoneCastleShape(const ZoneState& z, int v){
    return (v >= 0 && v < (int)z.castle.size()) ? z.castle[v] >> 4 : ZONE_CASTLE_NONE;
}
inline int ZoneCastleStoreys(const ZoneState& z, int v){
    return (v >= 0 && v < (int)z.castle.size()) ? z.castle[v] & 15 : 0;
}
//Its deck's layer (Walkers.h, LAYERS): a block's or an arch's top, a stair's middle - 0 with none.
inline int ZoneCastleLayer(const ZoneState& z, int v){
    int shape = ZoneCastleShape(z,v);
    int storeys = ZoneCastleStoreys(z,v);
    return (shape == ZONE_CASTLE_NONE) ? 0 : (shape == ZONE_CASTLE_STAIR) ? 2 * storeys - 1 : 2 * storeys;
}
//Whether its ground is shut to walkers: a block's or a stair's.
inline bool ZoneCastleShuts(const ZoneState& z, int v){
    int shape = ZoneCastleShape(z,v);
    return shape == ZONE_CASTLE_BLOCK || shape == ZONE_CASTLE_STAIR;
}
//The piece over its piece (`castle_over`): its shape and storeys, NONE and 0 with none.
inline int ZoneCastleOverShape(const ZoneState& z, int v){
    return (v >= 0 && v < (int)z.castle_over.size()) ? z.castle_over[v] >> 4 : ZONE_CASTLE_NONE;
}
inline int ZoneCastleOverStoreys(const ZoneState& z, int v){
    return (v >= 0 && v < (int)z.castle_over.size()) ? z.castle_over[v] & 15 : 0;
}
//Its deck's layer: an arch's top, a stair's middle - 0 with none.
inline int ZoneCastleOverLayer(const ZoneState& z, int v){
    int shape = ZoneCastleOverShape(z,v);
    int storeys = ZoneCastleOverStoreys(z,v);
    return (shape == ZONE_CASTLE_NONE) ? 0 : (shape == ZONE_CASTLE_STAIR) ? 2 * storeys - 1 : 2 * storeys;
}
//Whether the piece over it SITS on it - a stair on an arch - and so covers its deck.
inline bool ZoneCastleOverSits(const ZoneState& z, int v){
    return ZoneCastleOverShape(z,v) == ZONE_CASTLE_STAIR;
}
const char* ZoneCastleShapeName(int shape);
int ZoneCastleShapeByName(const std::string& name);    //-1 for none
//Whether painting a piece on `plot` puts it OVER the one there - an arch with headroom over it, or a stair a storey
//over an arch - rather than in its place.
bool ZoneCastleGoesOver(const ZoneState& z, int plot, int shape, int storeys);
/*
    Whether a piece may be painted on `plot`: a plot of the debug's rules for a road, nothing built on it, and no ground
    but a road under an arch; not a tower's ladder. Over a piece, as ZoneCastleGoesOver says; in its place, only one
    that still carries the piece over it. `why` says why not.
*/
bool ZoneCanCastle(const ChasmWorld& w, const ZoneState& z, int plot, int shape, int storeys, const char** why);

//Closed to walkers - never walked onto, nor through: a standing wall plot, a shut gate, or a castle's solid stone
//(Walkers, the economy's straight walks).
inline bool ZoneShutToWalkers(const ZoneState& z, int v){
    return ZoneWallStands(z,v) || ZoneGateShut(z,v) || ZoneCastleShuts(z,v);
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
    bool f_stopped = false;     //its work stopped by the player
    int next_crop = ZONE_CROP_AUTO;
};
ZoneBuildingInfo ZoneBuildingFigures(const ChasmWorld& w, const ZoneState& z, uint32_t id);

#ifdef DEBUG
//The rules re-checked over the whole state - what the commands should have guaranteed.
void RunZoneChecks(const ChasmWorld& w, const ZoneState& z, GridCheckReport& report);
#endif

#endif

#ifndef _CHASM_ECONOMY_H_
#define _CHASM_ECONOMY_H_

#include <array>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "type_vec2.h"
#include "Goods.h"
#include "Zones.h"

class Walkers;

/*
    THE ECONOMY (docs/economy_plan.md, P2 + P3): goods in stores, and the first production - the
    woodcutter, fields, the water collector - each worked by a WORKER who carries what he makes to the
    nearest store that takes it.

    Simulation state, the physics thread's alone, like the zones and the walkers: changed only by the
    tick (the zones' commands change what stands; this follows), published as an immutable copy after
    each tick for the view, the panel and the tools. Saved, and hashed as the trace part `economy`.

    --- STORES ---------------------------------------------------------------------------------------
    Every building has a stock (`stock`, by building id) - a store's is what it holds, a workplace's
    what it has made and not yet carried off (a field's harvest, a collector's water). A store's ROOM
    is its floor area (plot area x storeys) x ECONOMY_STORE_ROOM, in units of any good.
    What a store TAKES: next to a workplace - a store plot sharing a fine edge with the workplace's
    plots, or a corner with a field's cells - exactly that workplace's goods, fixed while they touch;
    on its own, the player's setting (ZoneBuilding::allow, a zone command). Worked out again when the
    zones change (`accepts`, derived, not saved).
    A store split by an erase shares its stock out by floor area: the largest piece keeps its id and
    the rest of it (Zones::SplitAfterRemoval names each new piece's parent, ZoneBuilding::split_from).
    A store pulled down loses what it held.

    --- WORKERS --------------------------------------------------------------------------------------
    One per workplace (a woodcutter, a field, a water collector) - a STAND-IN until P4 brings people
    from houses: he belongs to the building, waits at it and works from it, and goes when it goes.
    He walks a ROUTE of points: the grid's A* (Walkers::PlanRoute) where he must, and STRAIGHT where
    the line is clear (EconomyStraightClear) - in the open he need not follow the grid (the user's
    note). Walking, he re-plans when the zones change.
      - The WOODCUTTER takes the nearest standing tree within ECONOMY_WOODCUTTER_REACH of his hut, or a
        felled log lying there if that is nearer; walks to it, fells it (it leaves a log and a stump),
        picks the log up and carries it, as ECONOMY_WOOD_PER_TREE wood, to the nearest store that takes
        wood. No tree in reach, he waits.
      - The FARMER waits at his field while it grows. A field grows whenever it is not winter and not
        under snow (Calendar.h), for its crop's days, and then yields by its area into its own stock;
        the farmer carries it off in loads of ECONOMY_LOAD. Then it grows again.
      - The COLLECTOR'S worker carries its water off in loads, as it fills (not under snow).
    A worker with nothing that takes his load waits where he is, holding it.

    --- THE FOREST ----------------------------------------------------------------------------------
    `prop_state`, one byte per prop of the world's forest (ForestData::props, whose order is fixed for
    a world): standing, felled with its log lying, or a stump with the log taken. Saved and hashed.
*/

#define ECONOMY_STORE_ROOM          5.0f    //units of goods per unit of floor area: about 20 a plot-storey
#define ECONOMY_LOAD                5       //what a farmer or a water carrier carries at once
#define ECONOMY_WOOD_PER_TREE       4
#define ECONOMY_HUT_PILE            24      //wood piled outside the hut when no store takes it: 6 logs
#define ECONOMY_WOODCUTTER_REACH    40.0f   //world units from the hut
#define ECONOMY_FELL_SECONDS        6.0f
#define ECONOMY_PICK_SECONDS        1.0f    //picking up a log, putting a load down
#define ECONOMY_WATER_SECONDS       4.0f    //a collector fills one unit in this long
#define ECONOMY_WATER_HOLD          10      //and holds at most this, uncarried
#define ECONOMY_STRAIGHT_STEP       0.5f    //world units between the samples of a straight walk
#define ECONOMY_WORKER_SPEED        1.6f    //walking straight: WALKER_SPEED_GROUND

//A prop of the forest. Never renumber: saves hold these.
#define PROP_STATE_STANDING     0
#define PROP_STATE_LOG          1       //felled; its log lies there
#define PROP_STATE_STUMP        2       //felled, and the log taken away

//What a worker does. Never renumber.
#define WORKER_JOB_WOODCUTTER   0
#define WORKER_JOB_FARMER       1
#define WORKER_JOB_WATER        2

//Where a worker is in his round. Never renumber.
#define WORKER_AT_HOME          0       //waiting at his workplace for something to do
#define WORKER_TO_WORK          1       //walking to a tree or a log
#define WORKER_WORKING          2       //felling, or picking up (timer counts down)
#define WORKER_TO_STORE         3       //carrying
#define WORKER_UNLOADING        4       //putting the load down (timer)
#define WORKER_TO_HOME          5       //walking back to his workplace
#define WORKER_HOLDING          6       //carrying, and neither a store nor his pile will take it: waiting

struct EconomyWorker{
    uint32_t building = 0;      //his workplace
    int job = WORKER_JOB_WOODCUTTER;
    int state = WORKER_AT_HOME;
    int prop = -1;              //the tree or log he is after
    uint32_t store = 0;         //the store he is carrying to
    int carry_good = -1;
    int carry = 0;
    int timer = 0;              //ticks left of WORKING or UNLOADING
    /*
        The route: points on the ground plane (x, world z), walked from route[seg] to route[seg + 1],
        `along` the distance down that leg, each leg at its speed (world units a second). The last
        point is where he is going; with seg == route.size() - 1 he is there.
    */
    std::vector<vec2> route;
    std::vector<float> speed;   //per leg
    int seg = 0;
    float along = 0.0f;
    vec2 pos;                   //where he stands now - kept, so a re-plan starts from exactly here
};

struct EconomyField{
    uint32_t building = 0;
    int crop = -1;              //what is growing - a new crop is sown from the start
    int grown_ticks = 0;        //of its crop's growing time
};

struct EconomyState{
    std::shared_ptr<const ChasmWorld> world;
    std::vector<std::array<int,GOOD_COUNT>> stock;     //by building id
    std::vector<uint8_t> prop_state;                    //by prop index (ForestData::props)
    std::vector<EconomyWorker> workers;                 //in their buildings' id order
    std::vector<EconomyField> fields;                   //in id order
    //Derived from the zones, not saved: what each building takes (0 for none - not a store), and room.
    std::vector<uint8_t> accepts;
    std::vector<uint8_t> attached;                      //the goods of the workplaces a store is next to
    std::vector<int> room;
    uint32_t zones_version = 0xFFFFFFFFu;               //the zones `accepts` was worked out for
    uint32_t felled_version = 0;                        //bumped when a prop's state changes - the view
    uint32_t version = 0;                               //bumped on any change
};

//As a save holds it.
struct EconomySaved{
    std::vector<std::pair<uint32_t,std::array<int,GOOD_COUNT>>> stocks;    //non-empty ones only
    std::vector<std::pair<int,int>> props;              //felled props: index, state
    std::vector<EconomyWorker> workers;
    std::vector<EconomyField> fields;
};

class Economy{
public:
    void Reset(std::shared_ptr<const ChasmWorld> world);
    std::shared_ptr<const ChasmWorld> GetWorld() const { return state.world; }

    //One tick: follow the zones (new workplaces get a worker, gone ones lose theirs, split stores
    //share their stock), grow and fill, and move every worker one tick on. `walkers` plans the routes.
    void Tick(const ZoneState& z, Walkers& walkers, uint64_t calendar_tick, float dt);

    void Restore(std::shared_ptr<const ChasmWorld> world, const EconomySaved& saved, const ZoneState& z);
    EconomySaved Save() const;          //EconomySaveState of the live state
    const EconomyState& State() const { return state; }

private:
    EconomyState state;
    //Derived, not saved - from the world (the trees) or the zones (the rest), worked out again on load.
    std::vector<int> home_plot;                 //by building id: where its worker waits, -1 none
    std::vector<std::vector<int>> plots_of;     //by building id: a plot building's plots, in index order
    float tree_cell = 16.0f;                    //the trees in buckets of this many world units
    int tree_nx = 0, tree_nz = 0;
    vec2 tree_origin;
    std::vector<int> tree_start;                //per bucket + 1, into tree_items
    std::vector<int> tree_items;                //prop indices, in index order within a bucket
    void BuildTrees();
    //What follows from the zones: stocks sized (and a split store's shared out, if f_share), what
    //each store takes and its room, where each building's worker stands; workers and fields kept in
    //step with the buildings.
    void Derive(const ZoneState& z, bool f_share);
    void Replan(const ZoneState& z, Walkers& walkers);
    void TickWorker(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t calendar_tick, float dt);
    bool RouteTo(EconomyWorker& k, const ZoneState& z, Walkers& walkers, int to_plot, const vec2& to_point);
    bool FindTree(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    bool FindStore(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void GoHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void Walk(EconomyWorker& k, float dt);
};

//A published state as a save holds it - any thread.
EconomySaved EconomySaveState(const EconomyState& e);

//The goods a store takes and its room - for the panel and the tools, on a published state.
uint8_t EconomyAccepts(const EconomyState& e, uint32_t id);
int EconomyStock(const EconomyState& e, uint32_t id, int good);
int EconomyStockTotal(const EconomyState& e, uint32_t id);
//The colony's totals, in its stores only (not what lies at workplaces or is being carried).
std::array<int,GOOD_COUNT> EconomyStoredTotals(const EconomyState& e, const ZoneState& z);

//Whether a worker may walk straight from a to b: on one level, over open ground and roads, not across
//water, the mountain, a building (but the plots at either end), a field, or a wall or fence.
bool EconomyStraightClear(const ChasmWorld& w, const ZoneState& z, const vec2& a, const vec2& b,
                          int end_plot_a, int end_plot_b);

//Which way a worker faces, from his route. His position is EconomyWorker::pos.
vec2 EconomyWorkerFacing(const EconomyWorker& k);

const char* EconomyWorkerStateName(int state);
const char* EconomyJobName(int job);
//How far a field has grown toward its harvest, 0..1 - the look's cue too.
float EconomyFieldGrowth(const EconomyState& e, const ZoneState& z, uint32_t id);

#endif

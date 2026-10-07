#ifndef _CHASM_ECONOMY_H_
#define _CHASM_ECONOMY_H_

#include <array>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "type_vec2.h"
#include "Calendar.h"
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
    A HOUSE has room too, smaller (ECONOMY_HOUSE_ROOM), for what its family keeps at home - food, water
    and firewood (ECONOMY_HOUSE_KEEPS, `keeps`) - shared out on a split like a store's. Its family eats
    and drinks from it (NEEDS, below). It is not in `accepts`: no carrier delivers to a house - its own
    family fetches (needs_plan.md, step 2).

    --- PEOPLE (docs/people_plan.md, P4) ---------------------------------------------------------------
    The colony is the SETTLERS it starts with - families of adults, each with base skills (SKILL_*,
    1-10, random at the start) - and nobody else until births come. They start in the CAMP, a building
    of tents the zones place on a new map (Zones.h, THE CAMP), a person to a tent, any families together,
    and move out as soon as a house has room for a whole family: a house holds one family, as many as its
    floor area allows (ECONOMY_PERSON_AREA each, at most ECONOMY_HOUSE_MOST). A home pulled down sends its
    family to a camp with room, or to the camp's spot with nowhere to live.
    JOBS are given out automatically: a workplace with nobody takes the free housed adult with the best
    score - his skill for the job (ECONOMY_SKILL_DISTANCE world units a point) less the walk from his
    house - so where the player paints a house is what decides who works where. One worker a workplace.
    A worker's skill sets how fast he works (EconomySkillFactor).

    A worker walks a ROUTE of points: the grid's A* (Walkers::PlanRoute) where he must, and STRAIGHT where
    the line is clear (EconomyStraightClear) - in the open he need not follow the grid (the user's note).
    Walking, he re-plans when the zones change. EVERY ROUND STARTS AND ENDS AT HIS HOUSE (the user's
    choice: so a house beside the workplace and its store is what makes a worker quick):
      - The WOODCUTTER takes the nearest standing tree within ECONOMY_WOODCUTTER_REACH of his hut, or a
        felled log lying there if that is nearer; walks to it, fells it (it leaves a log and a stump),
        picks the log up and carries it, as ECONOMY_WOOD_PER_TREE wood, to the nearest store that takes
        wood - or, with none, to his WOODPILE (ECONOMY_HUT_PILE) - and goes home. From home the pile goes
        first: whenever a store takes wood he fetches from the pile before he fells again. The pile is on
        a LOT beside his hut (`pile_plot`, ZoneLotBeside - the zones make a new hut need one): with no lot
        standing there he has nowhere to put wood, and fells nothing. A lot is fenced, and the HUT IS ITS
        DOOR: wood is put on the pile and taken off it from the hut plot beside it (`pile_door`), the way
        he always came home to his hut - nobody walks into the yard.
      - A FIELD grows while it has a farmer, whenever it is not winter and not under snow (Calendar.h),
        for its crop's days, at its farmer's pace, and then yields by its area into its own stock; the
        farmer fetches it, a load (ECONOMY_LOAD) at a time, to a store that takes it. Then it grows again.
      - A COLLECTOR fills while it has a carrier (not under snow); he fetches its water in loads.
    A load is only picked up when a store takes it; one nobody takes after all is held, waiting.

    --- CONSTRUCTION (docs/construction_plan.md) ------------------------------------------------------
    A building a player places is a SITE (Zones.h, CONSTRUCTION): its storeys are planned, and stand
    only when built. WOOD is all a storey takes (the user, 2026-10-06): ECONOMY_BUILD_WOOD_PER_AREA of
    its floor area, more or less by kind. IDLE PEOPLE - anyone without a job - carry it: from his home
    to the nearest wood (a store's, the camp's supplies, a woodcutter's pile), a load to the site, and
    a while building with it there (ECONOMY_BUILD_SECONDS at his pace). What a site has been brought
    is `site`, by building id. When a builder finishes, every storey the site's wood pays for stands -
    the lowest first, so a building rises level by level - through ZONE_OP_BUILD_RAISE, which the app
    applies on the same tick (Economy::Raises). Until a storey stands it does nothing: no room, no
    family, no worker. A site pulled down loses the wood it was brought.
    A GARDEN or LOT painted in play is a site too, a plot at a time (ZoneState::ground_built): a little
    wood (ECONOMY_GROUND_WOOD_PER_AREA), its builders the same idle people. Such a site has no building
    id, so a worker's `site` names it as ECONOMY_SITE_GROUND | plot; its wood is `ground_site`, by plot,
    and it goes up through ZONE_OP_GROUND_RAISE (Economy::GroundRaises). It is built FROM OUTSIDE: a
    builder stands on an open plot beside it (GroundSiteApproach), so the fence never goes up round him.
    A new colony's camp holds the SUPPLIES the settlers brought (ECONOMY_START_*), so the first
    buildings can go up before there is a woodcutter.

    --- ROAD WORKS (docs/line_works_plan.md) ----------------------------------------------------------
    A ROAD painted in play is a site as well (ZoneRoadPlanned), and costs no wood - only work. When an idle
    person finds no wood to carry, he goes out to the nearest plot of a planned road nobody else is on: a
    standing tree in its way first (RoadTrees), FELLED at his pace - strength, a woodcutter's skill - into
    a log left lying where it fell, for a woodcutter to fetch; then, nothing of it standing, he LAYS the
    plot (ECONOMY_LAY_SECONDS at his pace), which goes down through ZONE_OP_GROUND_RAISE like a garden's
    wall. His `site` names the plot as a ground site does. Done with a piece, he takes the next one along
    the line within ECONOMY_ROAD_REACH of him, up to ECONOMY_ROAD_STINT pieces a trip, and goes home.

    --- NEEDS (docs/needs_plan.md, P5) -----------------------------------------------------------------
    A person has HEALTH and WATER, each counted in ticks of life left, one gone every tick: full health
    (NEEDS_HEALTH_FULL) is five days without food, a full water bar (NEEDS_WATER_FULL) three. HEALTH IS
    THE FOOD METER - there is no hunger of its own (the user's): food comes in whole units, so what a
    food is worth is the health it gives (EconomyFoodWorth) - wheat a day, greens half a day, beans two.
    With the water bar empty he is DRY: health falls three times as fast and food no longer raises it,
    so eating cannot outrun thirst. He eats and drinks AT HOME and nowhere else, from his house's stock
    or his camp's supplies, and only a whole unit that fits under full, so nothing is wasted - of the
    foods that fit, the one that keeps worst first. Health at nothing, he dies: struck from the colony
    and written into `dead`. Nobody leaves and nobody arrives, so a colony that cannot feed itself dies
    out - the first way to lose.

    --- THE FOREST ----------------------------------------------------------------------------------
    `prop_state`, one byte per prop of the world's forest (ForestData::props, whose order is fixed for
    a world): standing, felled with its log lying, or a stump with the log taken. Saved and hashed.
*/

#define ECONOMY_STORE_ROOM          5.0f    //units of goods per unit of floor area: about 20 a plot-storey
#define ECONOMY_HOUSE_ROOM          1.5f    //a house's, for its own food, water and firewood: about 6 a plot-storey
#define ECONOMY_HOUSE_KEEPS         (GOODS_FOOD | GOOD_BIT(GOOD_WOOD) | GOOD_BIT(GOOD_WATER))
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
#define ECONOMY_PERSON_AREA         1.3f    //floor area a person needs: a one-plot, one-storey house holds 3
#define ECONOMY_HOUSE_MOST          6       //the largest family a house holds, however big
#define ECONOMY_SKILL_DISTANCE      4.0f    //what a point of skill is worth against the walk to work
#define ECONOMY_BUILD_WOOD_PER_AREA 2.0f    //wood a storey takes per unit of its floor area: about 8 a plot
#define ECONOMY_BUILD_SECONDS       5.0f    //building with a load, at the builder's pace
#define ECONOMY_GROUND_WOOD_PER_AREA 0.5f   //a garden's wall or a lot's fence: about 2 a plot
#define ECONOMY_LAY_SECONDS         4.0f    //laying a road plot, at the worker's pace
#define ECONOMY_ROAD_STINT          8       //pieces of road work - a tree felled, a plot laid - a trip
#define ECONOMY_ROAD_REACH          10.0f   //how far from him the next piece of a stint may be
//A worker's `site` naming a garden or lot plot rather than a building (the plot in the low bits).
#define ECONOMY_SITE_GROUND         0x80000000u
inline bool EconomyIsGroundSite(uint32_t site){ return (site & ECONOMY_SITE_GROUND) != 0; }
inline int EconomyGroundSitePlot(uint32_t site){ return (int)(site & ~ECONOMY_SITE_GROUND); }
//What the settlers bring (in the camp): enough wood for the first few buildings.
#define ECONOMY_START_WOOD          40
#define ECONOMY_START_WHEAT         20
#define ECONOMY_START_BEANS         10
#define ECONOMY_START_WATER         20

//Needs (needs_plan.md), in ticks of life left - first numbers, to be tuned once a colony runs on them.
#define NEEDS_HEALTH_FULL           (5 * CALENDAR_DAY_TICKS)    //five days without food, from full
#define NEEDS_WATER_FULL            (3 * CALENDAR_DAY_TICKS)    //three days without water
#define NEEDS_WATER_WORTH           CALENDAR_DAY_TICKS          //a unit of water: a day of the bar
#define NEEDS_DRY_DRAIN             2                           //health a tick more, dry: three times as fast
//The health a unit of food gives: wheat a day, greens half a day, beans two (the user's); 0 for no food.
int EconomyFoodWorth(int good);
//What a person died of. Never renumber: saves hold these.
#define NEEDS_DIED_HUNGER           0
#define NEEDS_DIED_THIRST           1       //dry when his health ran out
const char* EconomyDeathCause(int cause);

//The base skills (gameplay_plan.md, "People" - the proposal; names may change). Never renumber.
#define SKILL_STRENGTH          0       //woodcutter, carrier, builder
#define SKILL_PRECISION         1       //archer, toolmaker
#define SKILL_HUSBANDRY         2       //farmer, herder, cook
#define SKILL_INGENUITY         3       //collector, boiler, machines
#define SKILL_COUNT             4
const char* SkillName(int skill);

//A prop of the forest. Never renumber: saves hold these.
#define PROP_STATE_STANDING     0
#define PROP_STATE_LOG          1       //felled; its log lies there
#define PROP_STATE_STUMP        2       //felled, and the log taken away

//What a worker does. Never renumber.
#define WORKER_JOB_NONE         -1      //no workplace (or no house to go to work from)
#define WORKER_JOB_WOODCUTTER   0
#define WORKER_JOB_FARMER       1
#define WORKER_JOB_WATER        2

//Where a worker is in his round. Never renumber.
#define WORKER_AT_HOME          0       //at his house (or the camp), waiting for something to do
#define WORKER_TO_WORK          1       //walking to a tree or a log, or to his workplace to fetch its goods
#define WORKER_WORKING          2       //felling, or picking up (timer counts down)
#define WORKER_TO_STORE         3       //carrying - to a store, or (store 0) to his hut's pile
#define WORKER_UNLOADING        4       //putting the load down (timer)
#define WORKER_TO_HOME          5       //walking back to his house (or the camp)
#define WORKER_HOLDING          6       //carrying, and neither a store nor his pile will take it: waiting
#define WORKER_TO_FETCH         7       //idle, walking to wood (`store`) for a construction (`site`)
#define WORKER_FETCHING         8       //picking it up (timer)
#define WORKER_TO_SITE          9       //carrying it to the site
#define WORKER_BUILDING         10      //building with it (timer)
#define WORKER_TO_ROAD          11      //idle, walking to a planned road (`site`): to a tree in its way (`prop`), or to lay it
#define WORKER_CLEARING         12      //felling that tree (timer); it is left lying as a log
#define WORKER_LAYING           13      //laying the plot (timer)

/*
    A PERSON, and what he is doing. Who he is: an id (from 1, never reused), a family, the house the
    family lives in (0: at the camp), an age, his base skills. What he does: his job and its workplace,
    and where he is in his round.
*/
struct EconomyWorker{
    uint32_t id = 0;
    uint32_t family = 0;
    uint32_t house = 0;         //a building id, 0 at the camp
    int age = 0;                //years
    bool f_female = false;      //picks his - or her - figure and first name; births will want it too
    uint8_t skills[SKILL_COUNT] = {};
    uint32_t building = 0;      //his workplace, 0 none
    int job = WORKER_JOB_NONE;
    int state = WORKER_AT_HOME;
    int prop = -1;              //the tree or log he is after
    uint32_t store = 0;         //the store he is carrying to - or, for a construction, fetching from
    uint32_t site = 0;          //the construction he is carrying for, 0 none
    int carry_good = -1;
    int carry = 0;
    int timer = 0;              //ticks left of WORKING or UNLOADING
    int stint = 0;              //road work: the pieces done on this trip (ECONOMY_ROAD_STINT)
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
    bool f_indoors = false;     //in his house (or tent): arrived home, and not set out since
    int health = NEEDS_HEALTH_FULL;     //ticks of life left (NEEDS, above)
    int water = NEEDS_WATER_FULL;       //ticks until he is dry
};

//Dry: his water bar empty - losing health three times as fast, and food doing him no good.
inline bool EconomyDry(const EconomyWorker& k){
    return k.water <= 0;
}

//Someone who died, and of what: the colony's record of its dead, in the order they died.
struct EconomyDeath{
    uint32_t id = 0;
    uint32_t family = 0;
    bool f_female = false;      //with the id and family, his name (EconomyDeathName)
    int age = 0;
    uint64_t tick = 0;          //the calendar's
    int cause = NEEDS_DIED_HUNGER;
};

struct EconomyField{
    uint32_t building = 0;
    int crop = -1;              //what is growing - a new crop is sown from the start
    int grown = 0;              //of its crop's growing time, in hundredths of a tick (a farmer's pace)
};

struct EconomyState{
    std::shared_ptr<const ChasmWorld> world;
    std::vector<std::array<int,GOOD_COUNT>> stock;     //by building id
    std::vector<uint8_t> prop_state;                    //by prop index (ForestData::props)
    std::vector<EconomyWorker> workers;                 //THE PEOPLE, in id order
    uint32_t next_person = 1;
    std::vector<EconomyDeath> dead;                     //in the order they died
    std::vector<EconomyField> fields;                   //in id order
    std::vector<int> site;                              //by building id: wood brought to its construction
    std::vector<int> ground_site;                       //by plot: wood brought to a garden's or lot's
    //Derived from the zones, not saved: what each building takes (0 for none - not a store), and room.
    std::vector<uint8_t> accepts;
    std::vector<uint8_t> attached;                      //the goods of the workplaces a store is next to
    std::vector<uint8_t> keeps;                         //what a building may hold: a store's goods, a house's own
    std::vector<int> room;                              //a store's, and a house's (ECONOMY_HOUSE_ROOM)
    std::vector<int> pile_plot;                         //a woodcutter's woodpile: a lot beside him, -1 none
    std::vector<int> capacity;                          //a house's: how many it holds
    int camp_plot = -1;                                 //derived from the world: where the unhoused wait
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
    std::vector<std::pair<uint32_t,int>> sites;         //sites with wood brought: building, wood
    std::vector<std::pair<int,int>> ground_sites;       //gardens and lots with wood brought: plot, wood
    uint32_t next_person = 1;
    std::vector<EconomyDeath> dead;
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
    //The plots whose next storey stands after this tick - the app raises them (ZONE_OP_BUILD_RAISE).
    const std::vector<int>& Raises() const { return raises; }
    //The garden and lot plots built this tick - the app raises them (ZONE_OP_GROUND_RAISE).
    const std::vector<int>& GroundRaises() const { return ground_raises; }

private:
    EconomyState state;
    std::vector<int> raises;                    //this tick's, in the order built
    std::vector<int> ground_raises;
    std::vector<int> ground_sites;              //the garden and lot plots still to build, in index order (derived)
    std::vector<int> road_sites;                //the road plots still to lay, in index order (derived)
    std::vector<int> pile_door;                 //by building id: the hut plot beside its woodpile, -1 none (derived)
    bool f_new_colony = false;                  //Reset, not Restore: the camp gets its supplies
    std::vector<uint8_t> stands;                //by building id: something of it is built (derived)
    std::vector<int> site_storeys;              //by building id: storeys planned and not standing (derived)
    //Derived, not saved - from the world (the trees) or the zones (the rest), worked out again on load.
    std::vector<int> home_plot;                 //by building id: where its worker waits, -1 none
    std::vector<std::vector<int>> plots_of;     //by building id: a plot building's plots, in index order
    std::vector<std::vector<int>> tents_of;     //by building id: a camp's tent plots
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
    //The people: the settlers a new colony starts with; families into houses; workers into workplaces.
    void FindCamp();
    void MakeSettlers();
    void HouseFamilies(const ZoneState& z, Walkers& walkers);
    void AssignJobs(const ZoneState& z, Walkers& walkers);
    int HomePlot(const EconomyWorker& k) const;
    bool AtHome(const EconomyWorker& k) const;
    bool CanWalk(const ZoneState& z, Walkers& walkers, const vec2& from, int to);
    bool FetchFromWorkplace(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //Needs: every person's tick of health and water, a meal and a drink at home, and the dead struck off.
    void Needs(const ZoneState& z, Walkers& walkers, uint64_t calendar_tick);
    void EatAndDrink(EconomyWorker& k, const ZoneState& z);
    //Construction: what a site still wants, carrying for one, and building with what it has. A site is
    //a building id or a garden or lot plot (ECONOMY_SITE_GROUND).
    int SiteNeed(const ZoneState& z, uint32_t id) const;
    int SiteBrought(uint32_t id) const;
    bool SiteAlive(const ZoneState& z, uint32_t id) const;
    //Where a building's wood is fetched from: a woodcutter's pile's door, else its plot nearest `to`.
    int SourcePlot(uint32_t id, const vec2& to) const;
    //Where a builder stands to build a garden or lot plot: an open plot beside it, else the plot itself.
    int GroundSiteApproach(const ZoneState& z, int v) const;
    int SiteLeft(const ZoneState& z, uint32_t id, const EconomyWorker* except) const;
    int WoodFree(uint32_t id) const;
    int NearestPlot(uint32_t id, const vec2& to, bool f_dry = false) const;
    bool FindSiteWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void Build(const ZoneState& z, uint32_t id);
    //Road works: the standing trees in a planned road plot's way, the next piece to do (from home, or
    //on a stint - near where he stands), and what he does when a piece is done.
    void RoadTrees(const ZoneState& z, int v, std::vector<int>& out) const;
    bool FindRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_stint);
    void NextRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
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
//The skill a job leans on, and how fast a worker with `skill` of it works: 1 at 5, 1.4 at 10, 0.68 at 1.
int EconomyJobSkill(int job);
float EconomySkillFactor(int skill);
//How many settlers a new colony starts with - its camp has a tent for each.
int EconomySettlerCount();
//A person's name, from his id and his family's: for the panel and the tools.
std::string EconomyPersonName(const EconomyWorker& k);
std::string EconomyDeathName(const EconomyDeath& d);
//Inside his house or tent, so not drawn.
inline bool EconomyIndoors(const EconomyWorker& k){
    return k.f_indoors && k.house != 0 && k.state == WORKER_AT_HOME;
}
//Construction: the wood one storey on `plot` takes, and all a building's planned storeys still take.
int EconomyStoreyWood(const ChasmWorld& w, const ZoneState& z, int plot);
int EconomySiteWood(const ChasmWorld& w, const ZoneState& z, uint32_t id);
int EconomySiteBrought(const EconomyState& e, uint32_t id);
//A garden's or lot's plot: the wood it takes to build, and what has been brought to it.
int EconomyGroundWood(const ChasmWorld& w, int plot);
int EconomyGroundBrought(const EconomyState& e, int plot);
//How far a field has grown toward its harvest, 0..1 - the look's cue too.
float EconomyFieldGrowth(const EconomyState& e, const ZoneState& z, uint32_t id);

#endif

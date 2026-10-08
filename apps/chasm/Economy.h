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
    JOBS are given out automatically: an empty place at a workplace takes the free housed adult with the
    best score - his skill for the job (ECONOMY_SKILL_DISTANCE world units a point) less the walk from his
    house - so where the player paints a house is what decides who works where. A workplace has one place
    once it stands; a BUILDERS' HUT one per plot-storey standing, and one while it is still a site, to build
    it (docs/builders_plan.md). Places are filled IN ORDER - the huts first, then the rest as they were
    placed - so with fewer people than places the newest workplaces stand empty, and a new hut takes its
    builders from them (`places`). A builder does what anyone without a job does: carries wood to sites
    and lays roads. A worker's skill sets how fast he works (EconomySkillFactor).
    LAND (docs/land_plan.md): a field employs nobody itself. Every field that stands belongs to a house -
    its LAND - or, while no house is lived in, to the camp (`EconomyField::owner`, SettleLand): the
    player's choice (ZoneBuilding::land_of) if that house is lived in, else the house it has, else the
    nearest lived-in house, else the camp - so the camp's land passes to a house as soon as there is one.
    The land is its owner's workplace: shared out in field order, a farmer to each ECONOMY_FARMER_CELLS
    cells or more of whole fields (LandPlaces), and only its own family - for the camp, whoever lives
    there - fills those places, in the house's place in the order. A field whose share has no farmer
    LIES FALLOW (`farmer_of` 0).

    A worker walks a ROUTE of points: the grid's A* (Walkers::PlanRoute) where he must, and STRAIGHT where
    the line is clear (EconomyStraightClear) - in the open he need not follow the grid (the user's note).
    Walking, he re-plans when the zones change. EVERY ROUND STARTS AND ENDS AT HIS HOUSE (the user's
    choice: so a house beside the workplace and its store is what makes a worker quick):
      - The WOODCUTTER takes, within ECONOMY_WOODCUTTER_REACH of his hut, the nearest felled log lying
        there - LOGS FIRST, the road builders' (the user's, 2026-10-08) - else the nearest standing tree;
        walks to it, fells it if it stands (it leaves a log and a stump), picks the log up - and, with
        another log lying within ECONOMY_LOG_PAIR_REACH and room for both where they go, that one too
        (ECONOMY_WOODCUTTER_LOGS) - and carries them, ECONOMY_WOOD_PER_TREE wood a log, to the nearest store
        that takes wood - or, with none, to his WOODPILE (ECONOMY_HUT_PILE) - and goes home. From home the pile goes
        first: whenever a store takes wood he fetches from the pile before he fells again. The pile is on
        a LOT beside his hut (`pile_plot`, ZoneLotBeside - the zones make a new hut need one): with no lot
        standing there he has nowhere to put wood, and fells nothing. A lot is fenced, and the HUT IS ITS
        DOOR: wood is put on the pile and taken off it from the hut plot beside it (`pile_door`), the way
        he always came home to his hut - nobody walks into the yard.
      - A FIELD (fields_plan.md) is EMPTY until a farmer of its land SOWS it - with its starting crop,
        chosen by the player, then with its next crop, AUTO by default (EconomyFieldSowCrop) - a while a
        cell at his pace (ECONOMY_SOW_SECONDS); sowing comes before anything else he does - but never in
        winter or under snow, and only a crop that ripens before the FROST (EconomyFieldSowNow). Sown, it grows
        while its share of the land has a farmer, whenever it is not winter and not under snow (Calendar.h),
        for its crop's days at that farmer's pace - what is still growing on winter's first day the frost
        takes, the field empty again - and then yields its built area x its crop's own yield x
        the YIELD it was sown at - 100% the first time, after that by what grew before it (EconomyCropYield,
        the user's follows-what table) - into its own stock, and is empty again. Its land's farmers fetch
        the harvest, a load (ECONOMY_LOAD) at a time: HOME FIRST (land_plan.md) - into his house while it is
        short of food (HomeShort) and not ahead of the colony (FAIR SHARES: less than ECONOMY_FAIR_DAYS a
        head over the average at home), all of it into the camp's supplies for the camp's land - and
        otherwise to a store that takes it, where the other houses fetch it.
      - A COLLECTOR fills while it has a carrier (not under snow); he fetches its water in loads.
    A load is only picked up when a store (or, for a harvest, home) takes it; one nobody takes after all is
    held, waiting.

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
    A FIELD painted in play is a site by its building id like a building (fields_plan.md):
    ECONOMY_FIELD_WOOD_PER_CELL for each cell still a site, brought to its middle vertex (home_plot - it
    has no plots of its own), and all its cells built at once when it has it (Economy::FieldRaises).
    WHICH SITE FIRST: nearest, by the walk - so a colony is built close - but of the errands within
    ECONOMY_QUEUE_SLACK of the shortest, the building placed first (the lowest id); and a site the player
    PUT FIRST (ZoneBuilding::f_priority) before every other, however far (Economy::FindSiteWork).
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
    A WALL (step 3) is a road with a cost: its trees are felled the same way, and then - only then - it is
    a ground site like a garden's, ECONOMY_WALL_WOOD a plot carried to it and raised when it is all there.
    THE QUEUE: carrying and line work are ONE ranking (FindSiteWork) - every errand by its walk, and within
    ECONOMY_QUEUE_SLACK of the nearest, by its place in the placing order: a building's id, a road's or
    wall's line (ZoneState::line), from one counter. So a road drawn before a house, about as near, is
    cleared and laid first. A line is never put first; gardens and lots have no place, and keep their walk's.

    --- NEEDS (docs/needs_plan.md, P5) -----------------------------------------------------------------
    A person has HEALTH and WATER, each counted in ticks of life left, one gone every tick: full health
    (NEEDS_HEALTH_FULL) is how long he lasts without food, a full water bar (NEEDS_WATER_FULL) how long
    without water. HEALTH IS THE FOOD METER - there is no hunger of its own (the user's): food comes in
    whole units, so what a food is worth is the health it gives (EconomyFoodWorth) - beans most, greens
    least. With the water bar empty he is DRY: health falls faster (NEEDS_DRY_DRAIN) and food no longer
    raises it, so eating cannot outrun thirst. He eats and drinks AT HOME, from his house's stock or his
    camp's supplies, and only a whole unit that fits under full, so nothing is wasted - of the foods that
    fit, the one that keeps worst first. Every figure here is TUNING, not a rule (needs_plan.md, "Current
    tuning"). A PRESSING NEED (EconomyPressing: water or health below NEEDS_PRESSING_*)
    that his home cannot meet stops him taking on more work: he finishes what he is doing and goes to the
    nearest place that has it - a store, the camp's supplies, a collector or a field's harvest, never
    another family's house - and eats and drinks there as he would at home (the user's: you would not
    necessarily drink at home). Health at nothing, he dies: struck from the colony and written into
    `dead`. Nobody leaves and nobody arrives, so a colony that cannot feed itself dies out - the first
    way to lose.
    FETCHING HOME (step 2) is what keeps him from those trips: a HOUSE holding less than ECONOMY_HOME_DAYS
    of what its family drinks or eats - the scarcer first, counted in days - sends one of them for a load
    from the nearest store or camp with some to spare (the camp's supplies are the colony's, the user's).
    An idle member goes first; one with a job only between rounds, with no jobless member home to go
    instead; one errand a house at a time, and before wood for a site. Someone eating at a store or the
    camp on a pressing need brings a load home with him. A camp's own families do not fetch - its supplies
    are what the settlers brought, and running short of them is the reason to build houses.
    HEAT (step 3, the user's 2026-10-08): at each dawn every INHABITED home under the snow - a house or a camp
    - burns its firewood, ECONOMY_FIREWOOD_BURN a plot-storey, from its own stock, and is WARM that day
    (`heat`, saved); with too little it burns none and its family is COLD that day: health falls faster
    (NEEDS_COLD_DRAIN) while food still counts, so cold shows as hunger first. Nobody home, nothing burns;
    the south, never under the snow, never burns. Someone with no home is cold under the snow at the camp's
    spot (heat[0]). A house keeps its firewood apart from its food and water, in a WOODSHED of
    ECONOMY_WOODSHED_DAYS of its burning. Its family FETCHES FIREWOOD like food - below ECONOMY_HOME_DAYS of
    burning, up to a day more - but only from autumn's first day, through winter and while the snow lies on it
    in spring, and only a house the full winter's snow reaches (`snowed`); and never past its FAIR SHARE: the
    colony's firewood at home, shared evenly over every such house's burning, and ECONOMY_FAIR_DAYS more (as
    food's, at home only - the user's). No wood is kept back for building sites: by autumn the building is
    meant to be done.

    --- THE FOREST ----------------------------------------------------------------------------------
    `prop_state`, one byte per prop of the world's forest (ForestData::props, whose order is fixed for
    a world): standing, felled with its log lying, or a stump with the log taken. Saved and hashed.
*/

#define ECONOMY_STORE_ROOM          7.5f    //units of goods per unit of floor area: about 30 a plot-storey
#define ECONOMY_HOUSE_ROOM          4.0f    //a house's, for its own food, water and firewood: about 16 a plot-storey (needs_plan.md)
#define ECONOMY_HOUSE_KEEPS         (GOODS_FOOD | GOOD_BIT(GOOD_WOOD) | GOOD_BIT(GOOD_WATER))
#define ECONOMY_LOAD                5       //what a farmer or a water carrier carries at once
#define ECONOMY_WOOD_PER_TREE       4
#define ECONOMY_HUT_PILE            24      //wood piled outside the hut when no store takes it: 6 logs
#define ECONOMY_WOODCUTTER_REACH    40.0f   //world units from the hut
#define ECONOMY_WOODCUTTER_LOGS     2       //logs he carries off at once, a second lying near the first (the user's)
#define ECONOMY_LOG_PAIR_REACH      12.0f   //...within this of him where he picked the first up: about six plots
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
#define ECONOMY_WALL_WOOD           4       //a palisade plot, flat: one felled tree's logs (the user's, 2026-10-08)
#define ECONOMY_FIELD_WOOD_PER_CELL 2       //a field site's, a coarse cell (fields_plan.md, the user's 2)
#define ECONOMY_SOW_SECONDS         3.0f    //sowing a cell of a field, at the farmer's pace
#define ECONOMY_FARMER_CELLS        6       //a farmer's share of his land: whole fields up to this many cells or more (land_plan.md)
#define ECONOMY_FAIR_DAYS           1       //a house keeps its land's food only while less than this a head ahead of the colony's average
#define ECONOMY_QUEUE_SLACK         8.0f    //errands this much longer than the shortest still go in placing order: ~4 plots
#define ECONOMY_LAY_SECONDS         4.0f    //laying a road plot, at the worker's pace
#define ECONOMY_ROAD_STINT          8       //pieces of road work - a tree felled, a plot laid - a trip
#define ECONOMY_ROAD_REACH          10.0f   //how far from him the next piece of a stint may be
//A worker's `site` naming a garden or lot plot rather than a building (the plot in the low bits).
#define ECONOMY_SITE_GROUND         0x80000000u
inline bool EconomyIsGroundSite(uint32_t site){ return (site & ECONOMY_SITE_GROUND) != 0; }
inline int EconomyGroundSitePlot(uint32_t site){ return (int)(site & ~ECONOMY_SITE_GROUND); }
//What the settlers bring (in the camp): wood for the first few buildings, food and water for the first days.
//Tuning, like the needs below.
#define ECONOMY_START_WOOD          40
#define ECONOMY_START_WHEAT         40
#define ECONOMY_START_BEANS         30
#define ECONOMY_START_WATER         40

/*
    Needs (needs_plan.md), in ticks of life left. TUNING, NOT RULES: these move with playtesting and new
    features, and a difficulty setting may pick other sets later (the user, 2026-10-08). The comments say
    what each one is; the numbers are only here, and in needs_plan.md's "Current tuning" table.
*/
#define NEEDS_HEALTH_FULL           (7 * CALENDAR_DAY_TICKS)    //a full bar: how long without food, from full
#define NEEDS_WATER_FULL            (4 * CALENDAR_DAY_TICKS)    //a full water bar: how long until dry
#define NEEDS_WATER_WORTH           (2 * CALENDAR_DAY_TICKS)    //what a unit of water puts back on the bar
#define NEEDS_DRY_DRAIN             2                           //health lost a tick more while dry
#define NEEDS_PRESSING_WATER        (NEEDS_WATER_FULL / 3)      //below this he goes for water
#define NEEDS_PRESSING_HEALTH       (NEEDS_HEALTH_FULL / 4)     //...and for food
#define ECONOMY_MEAL_SECONDS        2.0f                        //eating and drinking away from home
#define ECONOMY_HOME_DAYS           2                           //a house fetches below this many days of its family's water, food or firewood
#define ECONOMY_FIREWOOD_BURN       1                           //wood a home under the snow burns at dawn, a plot-storey
#define ECONOMY_WOODSHED_DAYS       4                           //a house's room for firewood, apart from its food and water's: days of burning
#define NEEDS_COLD_DRAIN            1                           //health lost a tick more while cold
//The health a unit of food gives - beans most, then wheat, greens least (the user's order; the amounts are
//tuning); 0 for no food.
int EconomyFoodWorth(int good);
//What a person died of. Never renumber: saves hold these.
#define NEEDS_DIED_HUNGER           0
#define NEEDS_DIED_THIRST           1       //dry when his health ran out
#define NEEDS_DIED_COLD             2       //cold (and not dry) when it did
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
#define WORKER_JOB_BUILDER      3       //at a builders' hut: carries wood to sites and builds, lays roads
#define WORKER_JOB_COUNT        4

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
#define WORKER_TO_SUPPLY        14      //a pressing need: walking to food or water (`store`), still holding any load
#define WORKER_SUPPLYING        15      //eating and drinking there (timer)
#define WORKER_TO_GET           16      //fetching home: walking to a store or the camp (`store`) for a kind (`carry_good`, carry 0)
#define WORKER_GETTING          17      //picking a load of it up there (timer)
#define WORKER_TO_LARDER        18      //carrying it home, into his house's own stock
#define WORKER_TO_SOW           19      //a farmer walking out to sow his field (fields_plan.md)
#define WORKER_SOWING           20      //sowing it (timer)

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
    uint32_t building = 0;      //his workplace, 0 none - a farmer's is his house (or camp), whose land he farms
    int job = WORKER_JOB_NONE;
    int state = WORKER_AT_HOME;
    int prop = -1;              //the tree or log he is after
    uint32_t store = 0;         //the store he is carrying to - or, for a construction, fetching from; a farmer's field out there
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

//Dry: his water bar empty - losing health faster (NEEDS_DRY_DRAIN), and food doing him no good.
inline bool EconomyDry(const EconomyWorker& k){
    return k.water <= 0;
}

//What he must have before he takes on more work: water, below NEEDS_PRESSING_WATER; food, with health below
//NEEDS_PRESSING_HEALTH - unless he is dry, when food does him no good and would only send him from store to store.
inline uint8_t EconomyPressing(const EconomyWorker& k){
    uint8_t want = 0;
    if (k.water < NEEDS_PRESSING_WATER){
        want |= GOOD_BIT(GOOD_WATER);
    }
    if (k.health < NEEDS_PRESSING_HEALTH && !EconomyDry(k)){
        want |= GOODS_FOOD;
    }
    return want;
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

/*
    A field's growing (fields_plan.md): EMPTY until its farmer sows it, then its crop grows; at harvest it is
    empty again, that crop remembered as the LAST, which decides the next one's yield (EconomyCropYield).
*/
struct EconomyField{
    uint32_t building = 0;
    int crop = -1;              //what is growing, -1 none
    int grown = 0;              //of its crop's growing time, in hundredths of a tick (a farmer's pace)
    bool f_sown = false;
    int yield = 100;            //percent of its crop's own: set when sown, from what grew before
    int last = -1;              //the crop last harvested, -1 none yet
    uint32_t owner = 0;         //whose land it is: a house or the camp, 0 none yet (land_plan.md)
    int frost = -1;             //the crop the last frost took (EconomyGrowingTicksLeft), -1 none - until it is sown again
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
    std::vector<uint8_t> heat;                          //by building id: a home's HEAT_* since dawn; [0] the homeless
    //Derived from the zones, not saved: what each building takes (0 for none - not a store), and room.
    std::vector<uint8_t> accepts;
    std::vector<uint8_t> attached;                      //the goods of the workplaces a store is next to
    std::vector<uint8_t> keeps;                         //what a building may hold: a store's goods, a house's own
    std::vector<int> room;                              //a store's, and a house's (ECONOMY_HOUSE_ROOM)
    std::vector<int> pile_plot;                         //a woodcutter's woodpile: a lot beside him, -1 none
    std::vector<int> capacity;                          //a house's: how many it holds
    std::vector<int> places;                            //a workplace's: how many it employs (0: not one) - a house's or camp's, its land's farmers
    std::vector<uint32_t> farmer_of;                    //by building id: a field's farmer, the person its share falls to - 0 fallow
    std::vector<int> burn;                              //a house's or camp's firewood a dawn under the snow
    std::vector<int> woodshed;                          //a house's room for firewood
    std::vector<uint8_t> snowed;                        //a house the full winter's snow reaches: it fetches firewood
    int camp_plot = -1;                                 //derived from the world: where the unhoused wait
    uint32_t zones_version = 0xFFFFFFFFu;               //the zones `accepts` was worked out for
    uint32_t felled_version = 0;                        //bumped when a prop's state changes - the view
    uint32_t version = 0;                               //bumped on any change
};

//A home's heat since dawn (HEAT, above). Never renumber: saves hold these.
#define HEAT_NONE   0       //no fire wanted: no snow on it at dawn, or nobody home
#define HEAT_WARM   1       //its firewood burned
#define HEAT_COLD   2       //under the snow with too little to burn
//Whether he is cold today: his home's heat, or the homeless's.
inline bool EconomyCold(const EconomyState& e, const EconomyWorker& k){
    return k.house < e.heat.size() && e.heat[k.house] == HEAT_COLD;
}

//As a save holds it.
struct EconomySaved{
    std::vector<std::pair<uint32_t,std::array<int,GOOD_COUNT>>> stocks;    //non-empty ones only
    std::vector<std::pair<int,int>> props;              //felled props: index, state
    std::vector<EconomyWorker> workers;
    std::vector<EconomyField> fields;
    std::vector<std::pair<uint32_t,int>> sites;         //sites with wood brought: building, wood
    std::vector<std::pair<int,int>> ground_sites;       //gardens and lots with wood brought: plot, wood
    std::vector<std::pair<uint32_t,int>> heats;         //homes not HEAT_NONE: building (0 the homeless), heat
    uint32_t next_person = 1;
    std::vector<EconomyDeath> dead;
    bool f_before_land = false;         //read from a save older than land (land_plan.md): its fields have no owner
};

class Economy{
public:
    void Reset(std::shared_ptr<const ChasmWorld> world);
    std::shared_ptr<const ChasmWorld> GetWorld() const { return state.world; }

    //One tick: follow the zones (new workplaces get a worker, gone ones lose theirs, split stores
    //share their stock), grow and fill, and move every worker one tick on. `walkers` plans the routes.
    void Tick(const ZoneState& z, Walkers& walkers, uint64_t calendar_tick, float dt);

    void Restore(std::shared_ptr<const ChasmWorld> world, const EconomySaved& saved, const ZoneState& z);
    //A debug edit (the panel's villager list, CHASM_CMD_NEEDS): a person's health and water in ticks, each
    //held to its bar; -1 leaves one as it is. Applied before the tick's needs, so someone at home with food
    //and water eats and drinks straight back up; health 0 kills only someone who cannot. False with no such person.
    bool SetNeeds(uint32_t person, int health, int water);
    EconomySaved Save() const;          //EconomySaveState of the live state
    const EconomyState& State() const { return state; }
    //The plots whose next storey stands after this tick - the app raises them (ZONE_OP_BUILD_RAISE).
    const std::vector<int>& Raises() const { return raises; }
    //The garden and lot plots built this tick - the app raises them (ZONE_OP_GROUND_RAISE).
    const std::vector<int>& GroundRaises() const { return ground_raises; }
    //The field cells built this tick - a field site's, all at once - the app raises them (ZONE_OP_FIELD_RAISE).
    const std::vector<int>& FieldRaises() const { return field_raises; }
    //The standing trees in a planned road's or wall's way, to be felled before it is laid or built. Physics
    //thread - or a tool between ticks (Scene::AtTickBoundary), which is how chasm_economy lists them.
    void RoadTrees(const ZoneState& z, int v, std::vector<int>& out) const;

private:
    EconomyState state;
    std::vector<int> raises;                    //this tick's, in the order built
    std::vector<int> ground_raises;
    std::vector<int> field_raises;
    std::vector<std::vector<int>> cells_of;     //by building id: a field's coarse cells, in index order (derived)
    std::vector<int> site_cells;                //by building id: a field's cells still a site (derived)
    std::vector<int> ground_sites;              //the garden, lot and wall plots still to build, in index order (derived)
    std::vector<int> road_sites;                //the road plots still to lay, in index order (derived)
    std::vector<int> wall_sites;                //the wall plots still to build - their trees to fell first (derived)
    std::vector<int> pile_door;                 //by building id: the hut plot beside its woodpile, -1 none (derived)
    bool f_new_colony = false;                  //Reset, not Restore: the camp gets its supplies
    std::vector<uint8_t> stands;                //by building id: something of it is built (derived)
    std::vector<int> site_storeys;              //by building id: storeys planned and not standing (derived)
    //Derived, not saved - from the world (the trees) or the zones (the rest), worked out again on load.
    std::vector<int> home_plot;                 //by building id: where its worker waits, -1 none
    std::vector<std::vector<int>> plots_of;     //by building id: a plot building's plots, in index order
    std::vector<std::vector<int>> tents_of;     //by building id: a camp's tent plots
    std::vector<int> share_of;                  //by building id: a field's share of its owner's land, -1 none (LandPlaces)
    std::vector<float> field_pace;              //by building id: its farmer's pace, 0 fallow (Tending)
    bool f_resettle = false;                    //a loaded save's land not yet settled (an older save's)
    uint64_t now = 0;                           //the tick being run, for the rules that ask the date inside it
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
    //The wood a woodcutter could go for near `at`, logs first (f_logs: only logs), and out to the first of it he
    //can reach, of the first `tries`.
    void WoodNear(const EconomyWorker& k, const ZoneState& z, const vec2& at, float reach, bool f_logs,
                  std::vector<std::pair<std::pair<int,float>,int>>& out) const;
    bool GoForWood(EconomyWorker& k, const ZoneState& z, Walkers& walkers,
                   const std::vector<std::pair<std::pair<int,float>,int>>& found, size_t tries);
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
    //A field of his land built, empty, its crop decided and its share farmed: out to sow it (fields_plan.md,
    //land_plan.md). True if he set off.
    bool GoSow(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t calendar_tick);
    //A harvest waiting on his land, with home or a store to take it: out to fetch a load. True if he set off.
    bool FetchHarvest(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //Picking up the load at the field he came to (`store`): home first, else a store. False if nothing went.
    bool PickHarvest(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //FAIR SHARES (land_plan.md): the food a head, in ticks, a family may keep from its own land - the colony's
    //average at home and ECONOMY_FAIR_DAYS more - and whether his house has that much already.
    int64_t FairShare(const ZoneState& z) const;
    bool HomeAhead(const EconomyWorker& k, const ZoneState& z) const;
    EconomyField* FieldOf(uint32_t id);
    //LAND (land_plan.md): every standing field's owner settled, then the land's places and its shares; and
    //who farms each share.
    void SettleLand(const ZoneState& z, Walkers& walkers);
    void LandPlaces(const ZoneState& z);
    void Tending();
    //Needs: every person's tick of health and water, a meal and a drink at home, and the dead struck off.
    void Needs(const ZoneState& z, Walkers& walkers, uint64_t calendar_tick);
    void EatAndDrink(EconomyWorker& k, const ZoneState& z);
    void Consume(EconomyWorker& k, std::array<int,GOOD_COUNT>& from);
    //A pressing need his home cannot meet: off to the nearest place that can. True if he set off. With
    //f_home false he cannot get home, and what it holds is no help.
    bool SeekSupply(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_home = true);
    //Fetching home (needs_plan.md, step 2): what his house is short of; off to fetch it; a load of it picked
    //up at `from` and carried home; and the way home itself.
    uint8_t HomeShort(const EconomyWorker& k, const ZoneState& z, uint8_t* first) const;
    //HEAT (above): every home's fire at dawn; what house h has room for of `good` (its woodshed for wood, the
    //rest of its room for food and water); whether it fetches firewood now; and its fair share of it.
    void Heat(const ZoneState& z, uint64_t calendar_tick);
    int HouseRoom(uint32_t h, int good) const;
    bool FirewoodNow(uint32_t h) const;
    int FirewoodFair(const ZoneState& z, uint32_t h) const;
    bool FetchHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    bool TakeHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint32_t from, uint8_t goods);
    bool RouteHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
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
    //Road works: the next piece to do (from home, or on a stint - near where he stands), and what he does when a
    //piece is done. The trees in a plot's way are RoadTrees, public.
    bool FindRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_stint);
    void NextRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //A piece of line work he could take: a tree to fell on `plot`'s way (`tree`), or the road plot to lay
    //(`tree` -1) - where he walks to, and how far that is from him.
    struct LineJob{
        float walk;
        int plot, tree, to_plot;
        vec2 to;
    };
    //The nearest few pieces nobody else is on, nearest first - what FindRoadWork and FindSiteWork choose from.
    void LineJobs(const EconomyWorker& k, const ZoneState& z, bool f_stint, std::vector<LineJob>& out) const;
    bool StartLineJob(EconomyWorker& k, const ZoneState& z, Walkers& walkers, const LineJob& j, bool f_stint);
    //A wall plot with its wood all there and nothing of its way standing goes up (ground_raises).
    void RaiseWall(const ZoneState& z, int v);
};

//A published state as a save holds it - any thread.
EconomySaved EconomySaveState(const EconomyState& e);

//The goods a store takes and its room - for the panel and the tools, on a published state.
uint8_t EconomyAccepts(const EconomyState& e, uint32_t id);
int EconomyStock(const EconomyState& e, uint32_t id, int good);
int EconomyStockTotal(const EconomyState& e, uint32_t id);
//The colony's totals, in its stores only (not what lies at workplaces or is being carried).
std::array<int,GOOD_COUNT> EconomyStoredTotals(const EconomyState& e, const ZoneState& z);

//A prop the painting has covered - a building on its plot, a garden or lot on it, a field over its cell: the view
//no longer draws it (ApplicationChasm::PropHidden, the same rule less the road's), so nobody fells it or waits on it.
bool EconomyPropCovered(const ZoneState& z, int plot, int coarse);

//Whether a worker may walk straight from a to b: on one level, over open ground and roads, not across
//water, the mountain, a building (but the plots at either end), a field, or a wall or fence.
bool EconomyStraightClear(const ChasmWorld& w, const ZoneState& z, const vec2& a, const vec2& b,
                          int end_plot_a, int end_plot_b);

//Which way a worker faces, from his route. His position is EconomyWorker::pos.
vec2 EconomyWorkerFacing(const EconomyWorker& k);

const char* EconomyWorkerStateName(int state);
const char* EconomyJobName(int job);
//The job a kind of building employs, WORKER_JOB_NONE for one that employs nobody. A house and the camp employ
//their land's farmers; a field nobody (land_plan.md).
int EconomyJobOf(int kind);
//The colony's jobs at a glance - the overview's and the tool's: per job, who works it and its places.
struct EconomyJobCount{
    int working = 0;
    int places = 0;
};
void EconomyJobCounts(const EconomyState& e, const ZoneState& z, EconomyJobCount out[WORKER_JOB_COUNT], int* jobless);
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
//What a garden's, lot's or wall's plot takes to build: a wall ECONOMY_WALL_WOOD, the rest by area; a road 0.
int EconomyGroundCost(const ChasmWorld& w, const ZoneState& z, int plot);
int EconomyGroundBrought(const EconomyState& e, int plot);
//How far a field has grown toward its harvest, 0..1 - the look's cue too.
float EconomyFieldGrowth(const EconomyState& e, const ZoneState& z, uint32_t id);
/*
    FIELDS (fields_plan.md). The yield, in percent of a crop's own, it is sown at after `last` was harvested
    - the user's follows-what table; -1 for a field's first crop, which is 100.
*/
int EconomyCropYield(int last, int crop);
//What AUTO sows after `last`: the most food per growing day at the yield it would get (fields_plan.md,
//"AUTO by food"), the earlier crop (ZONE_CROP_*) on a tie.
int EconomyAutoCrop(int last);
//What a field will be sown with next: its starting crop until its first harvest, then its next crop (AUTO
//worked out). -1 while its starting crop is not chosen.
int EconomyFieldSowCrop(const EconomyField& f, const ZoneBuilding& b);
/*
    THE FROST (fields_plan.md, the user's 2026-10-08): whatever still grows on a field on winter's first day
    is lost, and nothing is sown in winter or under the snow. So a farmer sows only a crop that ripens before
    then, at his pace, in the growing weather the field has left - until winter, or until the snow reaches it
    first, which in the north it does over autumn's last days (Calendar.h). AUTO takes the most food of
    the crops that still do; a crop the player chose that no longer can waits for spring.
*/
//Ticks of growing weather a field at `p` has left before the frost, from `tick`: 0 in winter or under snow.
int EconomyGrowingTicksLeft(const ChasmWorld& w, const vec2& p, uint64_t tick);
//...and in a whole year there: from the snow leaving it in the spring after `tick`, to the frost.
int EconomyGrowingSeasonTicks(const ChasmWorld& w, const vec2& p, uint64_t tick);
//Ticks `crop` takes to grow from `grown` (EconomyField::grown) to ripe at a farmer's `pace`; a lot at pace 0.
int EconomyRipenTicks(int crop, int grown, float pace);
//Whether building `id` stands under the snow at `tick`, at its first plot - where the rule that a water
//collector fills only off the snow reads it - so a card can say so.
bool EconomyFrozen(const ChasmWorld& w, const ZoneState& z, uint32_t id, uint64_t tick);
//What a field is to be sown with NOW, with `ticks_left` of growing weather at `pace`: EconomyFieldSowCrop's
//if it ripens in time - AUTO the most food of those that do - else -1, and it waits for spring.
int EconomyFieldSowNow(const EconomyField& f, const ZoneBuilding& b, float pace, int ticks_left);
//A field's farmer's pace (EconomySkillFactor of his husbandry), 0 with nobody to farm it - from the state alone.
float EconomyFieldPace(const EconomyState& e, uint32_t id);
//A field's growing record, NULL for none.
const EconomyField* EconomyFieldOf(const EconomyState& e, uint32_t id);

#endif

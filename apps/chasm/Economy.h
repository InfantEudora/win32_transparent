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
    WHOSE IT IS (the user's, 2026-10-09): a home's own goods are its family's alone while anyone lives
    there, and anyone's - a store's, in effect - the moment nobody does (Economy::PrivateStock). A CAMP
    holds two stocks: its SUPPLIES (`stock`), the settlers' and the colony's whoever lives there, and its
    PANTRY (`pantry`), what its families fetch home, theirs alone as a house's is - with a house's room
    and woodshed. Its people eat, drink and burn from the pantry first, then the supplies. When the last
    of them leaves, the pantry joins the supplies at once (Economy::OpenPantries); a camp pulled down
    loses both.

    --- PEOPLE (docs/people_plan.md, P4) ---------------------------------------------------------------
    The colony is the SETTLERS it starts with - three families, each a couple with grown children and some
    with a child (LIFELINES, below), each person with base skills (SKILL_*, 1-10, random at the start) - and
    nobody else until births come. They start in the CAMP, a building
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
        a LOT of his own yard beside his hut (`pile_plot`, yards_plan.md - a hut may be built without one):
        with no lot standing there he has nowhere to put wood, and fells nothing. A lot is fenced, and the HUT IS ITS
        DOOR: wood is put on the pile and taken off it from the hut plot beside it (`pile_door`), the way
        he always came home to his hut - nobody walks into the yard.
      - A FIELD (fields_plan.md) is EMPTY until a farmer of its land SOWS it - with its starting crop,
        chosen by the player (AUTO once it has stood a day without one: ECONOMY_CROP_GRACE, issues_plan.md),
        then with its next crop, AUTO by default (EconomyFieldSowCrop) - a while a
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
    only when built. A storey takes WOOD - ECONOMY_BUILD_WOOD_PER_AREA of its floor area, more or less by
    kind - and, by kind, other goods (EconomyStoreyCost: SITE COSTS PER GOOD, defence_plan.md step 4; no
    kind takes any yet). IDLE PEOPLE - anyone without a job - carry them: from his home to the nearest
    of a good the site still wants (a store's, the camp's supplies, a house nobody lives in, a
    woodcutter's pile), a load to the site, and a while building with it there (ECONOMY_BUILD_SECONDS at
    his pace), whatever the good. What a site has been brought is `site`, by building id and good. When a
    builder finishes, every storey the site's goods pay for stands - all it takes of every good, the
    lowest first, so a building rises level by level - through ZONE_OP_BUILD_RAISE, which the app
    applies on the same tick (Economy::Raises); what it took is used up. Until a storey stands it does
    nothing: no room, no family, no worker. A site pulled down loses what it was brought.
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
    camp's pantry and then its supplies, and only a whole unit that fits under full, so nothing is wasted - of the foods that
    fit, the one that keeps worst first. Every figure here is TUNING, not a rule (needs_plan.md, "Current
    tuning"). A PRESSING NEED (EconomyPressing: water or health below NEEDS_PRESSING_*)
    that his home cannot meet stops him taking on more work: he finishes what he is doing and goes to the
    nearest place that has it - a store, the camp's supplies, a collector, a field's harvest or a house
    nobody lives in, never a lived-in house - and eats and drinks there as he would at home (the user's: you would not
    necessarily drink at home). Health at nothing, he dies: struck from the colony and written into
    `dead`. Nobody leaves and nobody arrives, so a colony that cannot feed itself dies out - the first
    way to lose.
    FETCHING HOME (step 2) is what keeps him from those trips: a HOUSE holding less than ECONOMY_HOME_DAYS
    of what its family drinks or eats - the scarcer first, counted in days - sends one of them for a load
    from the nearest store or camp with some to spare (the camp's supplies are the colony's, the user's).
    An idle member goes first; one with a job only between rounds, with no jobless member home to go
    instead; one errand a house at a time, and before wood for a site. Someone eating at a store or the
    camp on a pressing need brings a load home with him. A CAMP's families fetch for it the same way (the
    user's, 2026-10-09; before, they did not, and a baby born there went dry with the settlers' supplies) -
    into its PANTRY (STORES, above), with a room and a woodshed by its floor area - but never from its own
    supplies, which count as what it holds. A house nobody lives in is fetched from like a store.
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

    --- POSTS (docs/defence_plan.md) -------------------------------------------------------------------
    An ORDER sends a person to stand on a plot - his POST, which is his job while it lasts. He drops what he
    was doing for it on the spot (a load on him is put down as a CRATE, below; a storey he was putting up
    stands with what it has) and walks there; he does not go home, run errands or build. His needs
    are anyone's: a pressing one sends him to the nearest supply - his own house one of them, by its distance
    - and then back to the post. Cut off from it, he waits where he is and tries again, as someone cut off
    from home does. A job he had is KEPT: its place is his and stands idle - a collector does not fill, his
    share of a field does not grow - and released, he goes back to it; a jobless one is given none until
    then. Posted people are out of the staffing altogether (AssignJobs). Children cannot be sent.
    His PACK is what he takes along to live on: water and food, apart from any load (NEEDS_PACK_*). Filled
    from his home's pantry when he sets out from home, and topped up at every supply trip; eaten from as he
    would eat at home, a whole unit whenever it fits, so his bars stay up while it lasts and a pressing need
    comes only once it is empty. Home again after he is released, it goes back into the pantry.
    PROVISIONS (GOOD_PROVISIONS, step 5) are a soldier's food, worth a wheat and a beans together, and OUTSIDE
    GOODS_FOOD (Goods.h): nobody fetches them home and no home holds them. A posted person's food is any food,
    provisions too - the nearest place first, provisions first into his pack. A villager's is the crops; with
    none in any place he can reach he looks again with provisions, his LAST RESORT; and wherever anyone eats,
    provisions go last. MADE by the CARETAKERS - the builders - and only as many as are WANTED: what sites
    and towers still lack of them, less what lies made in stores, the camp, crates and the builders' huts, and
    what is being made (ProvisionsLacking). A builder, before his building work: with
    wheat and beans at his hut, makes them there (ECONOMY_PROVISION_SECONDS each at his pace, up to a load a
    round); with too few on hand and on their way for what is wanted, fetches a load of the scarcer from a
    store, the camp or a crate. The sites' carriers take them from the hut as from a store. What the hut holds
    that nobody wants any more goes to a store that takes it. A hut's stock is for this alone: nobody eats from it.
    A GUARD TOWER (ZONE_KIND_TOWER, step 6) is a post as a workplace: its junction's places (ZoneTowerGuards, 1 to
    4) once it stands whole, filled like any other - by ARCHERY first, the range's training, then precision
    (range_plan.md). A GUARD (WORKER_JOB_GUARD) lives as the posted do - out to his tower from home with his pack,
    never home again while he has the job - and his post is his place on its platform (EconomyPostPoint). THE
    TOWER IS HIS PANTRY: on it he eats and drinks from its stock as at home, so his pack waits while it has any.
    Its stock is water and provisions, and it is BUILT WITH THEM: it costs so much a GUARD PLACE (ECONOMY_TOWER_*,
    a share a storey), whatever its shape, and what a storey takes of water and provisions it keeps, as its stock,
    not used up - one placed in debug stands full. Only posted people draw on it: SeekSupply passes towers by for
    villagers, and no carrier, family or caretaker takes from one.
    KEPT FULL by the caretakers (step 7), the FIRST of a builder's errands, ahead of provisions and building
    (KeepTowers): a tower that stands whole and holds less than its full stock is to him a site that wants water
    and provisions - fetched as for a site from wherever is nearest, put into its stock, never more than it
    lacks less what is on its way (TowerLeft). Provisions nobody has are made for it (ProvisionsLacking counts
    it). Builders only: the jobless never carry to a tower that stands, so a colony with no builders' hut has
    towers nobody keeps.
    GUARDS BY HAND (step 8): a tower takes as many guards as its GUARD COUNT (EconomyState::guards, the player's,
    ECONOMY_TOWER_GUARDS_PLACED when placed), never more than its junction's places - its `places` are the count.
    A right click posts someone there BY HAND (PostGuard, EconomyWorker::f_chosen): kept like the posted, never
    swapped out by the auto-fill nor taken by a shortage of people; with every place taken he displaces an
    automatic guard. Released (PostGuard to 0), he goes home jobless and the count drops, so nobody follows him up.

    --- CRATES (docs/crates_plan.md) -------------------------------------------------------------------
    A CRATE is goods lying on the ground: a store villagers only take from, gone when empty. A posted
    person's load is put down as one the moment the order lands (Order, DropLoad) - into a store instead if he
    stands at one that takes it; else where he stands, or on the nearest open plot round it. A crate lies only
    on OPEN GROUND (EconomyCrateGround), ONE TO A PLOT - a load put down where one lies goes into it - and one
    whose plot is built over is lost. The debug tools put crates down too (SpawnCrate). Saved and hashed.
    TAKEN FROM wherever a store is (step 2): goods for a building site, what a house fetches, a pressing need -
    a worker's `store` then names the crate (ECONOMY_STORE_CRATE), and he stands beside it to take. Nothing is
    ever delivered to one. MOVED by the caretakers - the builders, never the jobless - as the LAST of their
    errands, a load at a time, to a store that takes it whole (MoveCrate).

    --- ROPE (docs/goods_plan.md) -------------------------------------------------------------------
    A ROPEWALK (ZONE_KIND_ROPEWALK) works from ECONOMY_ROPEWALK_MIN_PLOTS standing plots: its one place, a
    ROPEMAKER (WORKER_JOB_ROPEMAKER, by precision). From home he takes a load of rope waiting there to a store
    that takes it (FetchFromWorkplace, rope only); else, with hemp at the ropewalk, makes rope of it there -
    ECONOMY_ROPE_HEMP a rope, ECONOMY_ROPE_SECONDS each at his pace, faster on a longer ropewalk
    (EconomyRopewalkPace), up to a load a round; else fetches hemp from a store, the camp or a crate (MakeRope,
    AtRopewalk - the builders' provisions errand's states). He makes none past ECONOMY_ROPEWALK_PILE waiting
    there. No count of what is wanted, unlike provisions: hemp has no other use. The rope there is anyone's -
    sites' carriers take it as from a woodpile - and its hemp the ropemaker's alone.
    A WINCH is built with it, ECONOMY_WINCH_ROPE a storey (StoreyGoods), and the colony starts with none: a winch
    site stands staked out, its wood brought, until a hemp field and a ropewalk have made its rope.

    --- THE FOREST ----------------------------------------------------------------------------------
    `prop_state`, one byte per prop of the world's forest (ForestData::props, whose order is fixed for
    a world): standing, felled with its log lying, or a stump with the log taken. Saved and hashed.
*/

//Per unit of floor area, not per plot: plots differ in size, and what one holds is wiki/cells.md's.
#define ECONOMY_STORE_ROOM          7.5f    //units of goods per unit of floor area
#define ECONOMY_HOUSE_ROOM          4.0f    //a house's, for its own food, water and firewood, per unit of floor area (needs_plan.md)
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
#define ECONOMY_PERSON_AREA         1.3f    //floor area a person needs: a house holds its floor area over this, whole people
#define ECONOMY_HOUSE_MOST          6       //the largest family a house holds, however big
#define ECONOMY_SKILL_DISTANCE      4.0f    //what a point of skill is worth against the walk to work
#define ECONOMY_BUILD_WOOD_PER_AREA 2.0f    //wood a storey takes per unit of its floor area
#define ECONOMY_BUILD_SECONDS       5.0f    //building with a load, at the builder's pace
#define ECONOMY_GROUND_WOOD_PER_AREA 0.5f   //a garden's wall or a lot's fence, per unit of the plot's area
#define ECONOMY_WALL_WOOD           4       //a palisade plot, flat: one felled tree's logs (the user's, 2026-10-08)
#define ECONOMY_FIELD_WOOD_PER_CELL 2       //a field site's, a coarse cell (fields_plan.md, the user's 2)
#define ECONOMY_RANGE_WOOD_PER_CELL 12      //an archery range site's, a cell: a field's and the targets' stands (range_plan.md) (4x, the user's 2026-10-09: a range is not an early building)
#define ECONOMY_RANGE_WHEAT_PER_CELL 8      //...and its wheat, the bales' straw (the user's: wheat, once sites took it)
/*
    TRAINING (range_plan.md): a trainee at a range trains a stint on its line, then goes home and out again.
    ARCHERY grows by ARCHERY_PER_TICK a tick trained at his precision's pace, up to his precision's cap - a
    tenth of ARCHERY_FULL a point. Tuning: an average trainee (precision 5) reaches his cap in about a season.
*/
#define ECONOMY_TRAIN_SECONDS       40.0f   //a stint on the line
#define ARCHERY_FULL                1000000 //archery's 100%
#define ARCHERY_PER_TICK            36      //what a tick of training adds, at pace 1
#define ECONOMY_SOW_SECONDS         3.0f    //sowing a cell of a field, at the farmer's pace
#define ECONOMY_PROVISION_SECONDS   3.0f    //making a provision at the builders' hut, at the builder's pace (defence_plan.md)
#define ECONOMY_ROPE_HEMP           2       //hemp a rope takes (goods_plan.md)
#define ECONOMY_ROPE_SECONDS        8.0f    //making one on a ropewalk of ECONOMY_ROPEWALK_MIN_PLOTS, at the ropemaker's pace
#define ECONOMY_ROPEWALK_MIN_PLOTS  2       //standing plots a ropewalk works from: with one it is placed, and idle
#define ECONOMY_ROPEWALK_PILE       12      //rope waiting at the ropewalk at most: he makes no more past it
#define ECONOMY_WINCH_ROPE          4       //rope a winch's storey takes, with its wood (goods_plan.md): the colony starts with none
#define ECONOMY_FARMER_CELLS        6       //a farmer's share of his land: whole fields up to this many cells or more (land_plan.md)
#define ECONOMY_CROP_GRACE          (CALENDAR_DAY_TICKS)    //a field built with no crop waits this long for one, then starts with AUTO (issues_plan.md)
#define ECONOMY_FAIR_DAYS           1       //a house keeps its land's food only while less than this a head ahead of the colony's average
#define ECONOMY_QUEUE_SLACK         8.0f    //errands this much longer than the shortest still go in placing order: ~4 plots
#define ECONOMY_LAY_SECONDS         4.0f    //laying a road plot, at the worker's pace
#define ECONOMY_ROAD_STINT          8       //pieces of road work - a tree felled, a plot laid - a trip
#define ECONOMY_ROAD_REACH          10.0f   //how far from him the next piece of a stint may be
#define ECONOMY_POST_REACH          0.5f    //within this of his post's point he stands on it (defence_plan.md)
#define ECONOMY_TOWER_WOOD          10      //a guard tower, a GUARD PLACE (POSTS): its wood...
#define ECONOMY_TOWER_WATER         6       //...its water - its stock full, and what it is built with: 12 days for the guard
#define ECONOMY_TOWER_PROVISIONS    4       //...and its provisions, at 3 water to 2 provisions so both run out together
#define ECONOMY_TOWER_GUARDS_PLACED 1       //a new tower's guard count (step 8, the user's): the player adds more
//A worker's `site` naming a garden or lot plot rather than a building (the plot in the low bits).
#define ECONOMY_SITE_GROUND         0x80000000u
inline bool EconomyIsGroundSite(uint32_t site){ return (site & ECONOMY_SITE_GROUND) != 0; }
inline int EconomyGroundSitePlot(uint32_t site){ return (int)(site & ~ECONOMY_SITE_GROUND); }
//A worker's `store` naming a CRATE he is taking from rather than a building (the crate's id in the low bits).
#define ECONOMY_STORE_CRATE         0x40000000u
inline bool EconomyIsCrateStore(uint32_t store){ return (store & ECONOMY_STORE_CRATE) != 0; }
inline uint32_t EconomyCrateOfStore(uint32_t store){ return store & ~ECONOMY_STORE_CRATE; }
//What the settlers bring (in the camp): wood for the first few buildings, food and water for the first days.
//Tuning, like the needs below.
#define ECONOMY_START_WOOD          40
#define ECONOMY_START_WHEAT         50
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
#define NEEDS_PACK_WATER            3                           //a posted person's PACK (POSTS): units of water
#define NEEDS_PACK_FOOD             (3 * CALENDAR_DAY_TICKS)    //...and food, by the health it gives: a provision's worth
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
#define WORKER_JOB_ARCHER       4       //a trainee at an archery range: trains there, full time (range_plan.md)
#define WORKER_JOB_GUARD        5       //on a guard tower: posted there, living on its stock (defence_plan.md)
#define WORKER_JOB_ROPEMAKER    6       //at a ropewalk: rope from hemp (goods_plan.md)
#define WORKER_JOB_COUNT        7
//Below WORKER_JOB_NONE, what a stage of life gives instead of a workplace (LIFELINES): never given a place.
#define WORKER_JOB_PLAYING      -2      //a child: at home, out to play now and then - no work, errands or building
#define WORKER_JOB_BABY         -3      //a baby, its first year: indoors in its mother's care, doing nothing at all
#define WORKER_JOB_CARING       -4      //its mother, that year: at home but for the house's errands
inline bool EconomyStageJob(int job){
    return job < WORKER_JOB_NONE;
}

//Where a worker is in his round. Never renumber.
#define WORKER_AT_HOME          0       //at his house (or the camp), waiting for something to do
#define WORKER_TO_WORK          1       //walking to a tree or a log, or to his workplace to fetch its goods
#define WORKER_WORKING          2       //felling, or picking up (timer counts down)
#define WORKER_TO_STORE         3       //carrying - to a store, or (store 0) to his hut's pile
#define WORKER_UNLOADING        4       //putting the load down (timer)
#define WORKER_TO_HOME          5       //walking back to his house (or the camp)
#define WORKER_HOLDING          6       //carrying, and neither a store nor his pile will take it: waiting
#define WORKER_TO_FETCH         7       //idle, walking to goods (`store`; the kind `carry_good`, carry 0) for a construction (`site`)
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
#define WORKER_TO_PANTRY        18      //carrying it home, into his house's own stock
#define WORKER_TO_SOW           19      //a farmer walking out to sow his field (fields_plan.md)
#define WORKER_SOWING           20      //sowing it (timer)
#define WORKER_TO_PLAY          21      //a child walking out to play, a plot or two from home
#define WORKER_PLAYING          22      //playing there (timer), then home
#define WORKER_TO_POST          23      //posted (defence_plan.md): walking to his post
#define WORKER_ON_POST          24      //standing on it - or cut off from it, waiting where he is for a way
#define WORKER_TO_RANGE         25      //a trainee walking to his place on the range's line (range_plan.md)
#define WORKER_TRAINING         26      //training there (timer), then home
#define WORKER_TO_CRATE         27      //a caretaker walking to a crate (`store`) to move it to a storehouse (crates_plan.md)
#define WORKER_LIFTING          28      //picking a load of it up (timer), then off to the store with it
#define WORKER_TO_INPUT         29      //a builder walking to wheat or beans (`store`, `carry_good`; `stint` the load) for provisions
#define WORKER_TAKING_INPUT     30      //picking it up (timer)
#define WORKER_TO_WORKSHOP      31      //to his builders' hut, with it or to make provisions there (defence_plan.md)
#define WORKER_MAKING           32      //making one there (timer; its wheat and beans taken at the start)

/*
    LIFELINES (docs/lifeline_plan.md, step 1). A person is BORN on a calendar tick - a settler before the
    colony's first, so `born` is signed - and is a year older on that day every year: his AGE, in game years.
    How long ago decides his STAGE: a BABY for a year, then a CHILD whose job is PLAYING (WORKER_JOB_PLAYING),
    then a YOUNG ADULT, who works like anyone, then an ADULT. A child is free for a job's place the tick he
    grows up (Economy::Tick). Everyone keeps a MOTHER and a FATHER, and so do the dead, so the tree outlives
    the people in it. The stages' lengths are the user's; tuning like the rest (lifeline_plan.md).
*/
#define LIFE_BABY_TICKS         (CALENDAR_YEAR_TICKS)              //a baby for a year
#define LIFE_CHILD_TICKS        (CALENDAR_YEAR_TICKS * 5 / 2)      //a child until 2 1/2: a year and a half of playing
#define LIFE_YOUNG_TICKS        (CALENDAR_YEAR_TICKS * 9 / 2)      //a young adult two years more, then an adult
#define LIFE_PLAY_LOOKS         60      //a child at home goes out to play on one look in this many: every 30 s or so
#define LIFE_PLAY_SECONDS       4.0f    //and plays there this long before going home
//The stages, worked out from `born` and never saved.
#define LIFE_BABY               0
#define LIFE_CHILD              1
#define LIFE_YOUNG              2       //a young adult
#define LIFE_ADULT              3
//Age in whole years at `tick` (0 before birth), the stage then and its name, and the birthday's day of the year.
int EconomyAgeAt(int64_t born, uint64_t tick);
int EconomyStageAt(int64_t born, uint64_t tick);
const char* EconomyStageName(int stage);
int EconomyBirthday(int64_t born);
std::string EconomyBirthdayName(int64_t born);      //"summer 4"

/*
    A SHARE (the user's, 2026-10-10): what someone eats and drinks, in quarters of an adult's - a baby half, a
    child three quarters, a young adult on a whole. His bars fall on only that many ticks in four (Needs), so he
    eats and drinks that much less of his home's stock; and what a home fetches, keeps and counts as its fair
    share is by its mouths at their shares, not its heads. Its room is by heads: a baby still takes a place.
*/
#define LIFE_SHARE_BABY         2       //quarters of an adult's
#define LIFE_SHARE_CHILD        3
#define LIFE_SHARE_FULL         4
int EconomyShare(int64_t born, uint64_t tick);

/*
    BIRTHS (step 2). A COUPLE (`partner`; the settlers' couples from the start) has one chance a year of a
    child, at dawn on the couple's own day of the year (EconomyCoupleDay) - in a house of their own, warm that
    day, both fed, both adults and neither old, she neither expecting nor caring, and her children so far, the
    dead too, fewer than LIFE_CHILDREN_MOST. A house with a place to spare makes it likelier (the user's). A
    chance that comes up makes her EXPECTING (`due`); she works on, and the child is born the first time she
    is home after it is due - a BABY (WORKER_JOB_BABY), indoors in its mother's care for its first year, while
    she is CARING (WORKER_JOB_CARING): her place goes to someone else at once, and she stays home but for the
    house's errands, which she runs like anyone without a job. On its first birthday it is a child, and she is
    free for a place again - as she is if it dies. Tuning, like the stages (lifeline_plan.md).
*/
#define LIFE_EXPECTING_TICKS    (CALENDAR_YEAR_TICKS / 2)          //from the chance to the birth (the user's)
#define LIFE_CHILDREN_MOST      4                                   //a mother's children, a settler's counted
#define LIFE_OLD_TICKS          (CALENDAR_YEAR_TICKS * 36)         //no more once either is this old (old age itself: step 4)
#define LIFE_BIRTH_ROOM         50      //percent a year, with a place to spare in the house
#define LIFE_BIRTH_FULL         25      //...and with none
//A couple's day of the year for its chance (0 - 39), the same whichever of them is asked.
int EconomyCoupleDay(uint32_t a, uint32_t b);

/*
    COUPLES (step 3). A couple forms when two grown people move into a free house together (HouseFamilies): an
    adult man and an adult woman, of different families, not related (EconomyRelated), neither in a couple - a
    widow stays single, her husband still hers. They found a NEW FAMILY there, each keeping their own SURNAME
    (`surname`, given at birth), and their children take one of the two (EconomyCoupleSurname). The families
    that need a house take theirs first; the couples what is still free - the eldest unpaired adult first, with
    the eldest who can pair with them. So a new house is a new couple, and a new couple brings children.
*/
#define LIFE_COUPLE_ROOM        2       //what a house must hold for a couple to move in

/*
    A PERSON, and what he is doing. Who he is: an id (from 1, never reused), a family, the house the
    family lives in (0: at the camp), when he was born and to whom, his base skills. What he does: his job
    and its workplace, and where he is in his round.
*/
struct EconomyWorker{
    uint32_t id = 0;
    uint32_t family = 0;
    uint32_t house = 0;         //a building id, 0 at the camp
    int64_t born = 0;           //the calendar tick he was born on (LIFELINES)
    uint32_t mother = 0;        //person ids - 0 for a settler couple's own parents, who never came
    uint32_t father = 0;
    uint32_t partner = 0;       //his wife or her husband (BIRTHS), 0 none
    uint64_t due = 0;           //expecting: the tick her child is due, 0 not
    uint8_t surname = 0;        //his family name, given at birth and kept, in a couple too (COUPLES)
    bool f_female = false;      //picks his - or her - figure and first name, and which of a couple has the child
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
    std::vector<uint8_t> route_layer;   //per point, the layer it is on (Walkers.h, LAYERS) - empty: all of it the ground
    int seg = 0;
    float along = 0.0f;
    vec2 pos;                   //where he stands now - kept, so a re-plan starts from exactly here
    bool f_indoors = false;     //in his house (or tent): arrived home, and not set out since
    int health = NEEDS_HEALTH_FULL;     //ticks of life left (NEEDS, above)
    int water = NEEDS_WATER_FULL;       //ticks until he is dry
    int post = -1;                      //his POST (above): the plot he was sent to stand on, -1 none
    std::array<int,GOOD_COUNT> pack = {};   //his PACK (POSTS): water and food he carries for himself, by good
    int archery = 0;                    //ARCHERY (TRAINING, above): experience, of ARCHERY_FULL - kept when he stops
    bool f_chosen = false;              //a guard posted BY HAND (POSTS, step 8): kept ahead of the auto-fill
};

//The most archery he can reach: a tenth of ARCHERY_FULL a point of precision (range_plan.md).
inline int EconomyArcheryCap(const EconomyWorker& k){
    return (int)k.skills[SKILL_PRECISION] * (ARCHERY_FULL / 10);
}

//POSTED (POSTS): living on a post rather than at home - under an order, or a GUARD while he has the job. An order
//comes first: it suspends the guard's job like any other (EconomyPostPlot).
inline bool EconomyPosted(const EconomyWorker& k){
    return k.post >= 0 || k.job == WORKER_JOB_GUARD;
}
//Old enough to be sent to a post: a young adult or older (LIFELINES).
bool EconomyCanOrder(const EconomyWorker& k, uint64_t tick);
//The food in his pack, by the health it gives (ticks).
int EconomyPackFood(const EconomyWorker& k);

//Dry: his water bar empty - losing health faster (NEEDS_DRY_DRAIN), and food doing him no good.
inline bool EconomyDry(const EconomyWorker& k){
    return k.water <= 0;
}

//What he must have before he takes on more work: water, below NEEDS_PRESSING_WATER; food, with health below
//NEEDS_PRESSING_HEALTH - unless he is dry, when food does him no good and would only send him from store to store.
inline GoodsMask EconomyPressing(const EconomyWorker& k){
    GoodsMask want = 0;
    if (k.water < NEEDS_PRESSING_WATER){
        want |= GOOD_BIT(GOOD_WATER);
    }
    if (k.health < NEEDS_PRESSING_HEALTH && !EconomyDry(k)){
        want |= GOODS_EATEN;    //any food: whether provisions do is the seeker's to say (SeekSupply)
    }
    return want;
}

//Someone who died, and of what: the colony's record of its dead, in the order they died.
struct EconomyDeath{
    uint32_t id = 0;
    uint32_t family = 0;
    bool f_female = false;      //with the id and surname, his name (EconomyDeathName)
    uint8_t surname = 0;
    int64_t born = 0;           //his age at death is from this and `tick`
    uint32_t mother = 0;
    uint32_t father = 0;
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
    int wait = 0;               //ticks it has stood built and never sown with no starting crop, 0 not waiting (ECONOMY_CROP_GRACE)
};

//Where a crate came from (CRATES). Never renumber: saves hold these.
#define CRATE_DROPPED           0       //a load someone put down
#define CRATE_FOUND             1       //lying on the map - a debug spawn for now; finds from the seed later
#define ECONOMY_CRATE_RINGS     6       //rings of plots round him searched for open ground to put a load down on: past a camp's tents
#define ECONOMY_CRATE_STAND     0.5f    //a crate is taken from beside it: this far short of it, on the side he comes from

//A CRATE (above): what lies where, and where it came from.
struct EconomyCrate{
    uint32_t id = 0;            //from 1, never reused
    int plot = -1;              //the plot it lies on
    vec2 pos;                   //where on it (x, world z)
    std::array<int,GOOD_COUNT> stock = {};
    int origin = CRATE_DROPPED;
    uint32_t by = 0;            //who put it down, a person's id - 0 for a find
    uint64_t tick = 0;          //the calendar tick it was put down or placed
};

struct EconomyState{
    std::shared_ptr<const ChasmWorld> world;
    std::vector<std::array<int,GOOD_COUNT>> stock;     //by building id - a camp's: its SUPPLIES
    std::vector<std::array<int,GOOD_COUNT>> pantry;    //by building id: a camp's PANTRY, its families' own (STORES)
    std::vector<uint8_t> prop_state;                   //by prop index (ForestData::props)
    std::vector<EconomyWorker> workers;                 //THE PEOPLE, in id order
    uint32_t next_person = 1;
    std::vector<EconomyDeath> dead;                     //in the order they died
    std::vector<EconomyField> fields;                   //in id order
    std::vector<std::array<int,GOOD_COUNT>> site;       //by building id: goods brought to its construction
    std::vector<int> ground_site;                       //by plot: wood brought to a garden's or lot's
    std::vector<uint8_t> heat;                          //by building id: a home's HEAT_* since dawn; [0] the homeless
    std::vector<EconomyCrate> crates;                   //CRATES, in id order
    uint32_t next_crate = 1;
    std::vector<int> guards;                            //by building id: a tower's GUARD COUNT (POSTS, step 8), -1 not a tower
    //Derived from the zones, not saved: what each building takes (0 for none - not a store), and room.
    std::vector<GoodsMask> accepts;
    std::vector<GoodsMask> attached;                      //the goods of the workplaces a store is next to
    std::vector<GoodsMask> keeps;                         //what a building may hold: a store's goods, a house's own
    std::vector<int> room;                              //a store's, and a house's (ECONOMY_HOUSE_ROOM)
    std::vector<int> pile_plot;                         //a woodcutter's woodpile: a lot beside him, -1 none
    std::vector<int> capacity;                          //a house's: how many it holds
    std::vector<int> places;                            //a workplace's: how many it employs (0: not one) - a house's or camp's, its land's farmers
    std::vector<uint32_t> farmer_of;                    //by building id: a field's farmer, the person its share falls to - 0 fallow
    std::vector<int> burn;                              //a house's or camp's firewood a dawn under the snow
    std::vector<int> woodshed;                          //a house's room for firewood
    std::vector<uint8_t> snowed;                        //a house the full winter's snow reaches: it fetches firewood
    std::vector<uint8_t> shut;                          //by building id: SHUT IN, no door (EconomyShutIn)
    std::vector<int> door;                              //by building id: the plot a walk to it ends on, -1 none
    int camp_plot = -1;                                 //derived from the world: where the unhoused wait
    uint64_t tick = 0;                                  //the calendar's at its last tick or load: what ages are read at
    uint32_t zones_version = 0xFFFFFFFFu;               //the zones `accepts` was worked out for
    uint32_t felled_version = 0;                        //bumped when a prop's state changes - the view
    uint32_t crates_version = 0;                        //bumped when a crate changes - the view
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

//The plot of his post (POSTS): an order's, else a guard's tower - -1 for neither.
int EconomyPostPlot(const EconomyState& e, const EconomyWorker& k);
//Where on it he stands: an order's plot's point; on a tower, his place on its platform by his rank among its guards
//(ZoneTowerPlace).
vec2 EconomyPostPoint(const EconomyState& e, const EconomyWorker& k);
//A guard tower's full stock of `good` - what it is built with and keeps: so much a guard place (ECONOMY_TOWER_*).
int EconomyTowerStock(const ChasmWorld& w, int plot, int good);
//A guard tower standing whole on `plot` - its platform up: what takes guards and is kept full. Its `places` cannot
//say it, as they are its guard count (step 8), which may be 0.
inline bool EconomyTowerUp(const ZoneState& z, int plot){
    return plot >= 0 && plot < (int)z.storeys.size() && z.KindOf(plot) == ZONE_KIND_TOWER && z.standing[plot] > 0
           && z.standing[plot] == z.storeys[plot];
}
//What tower `id` on `plot` lacks of its full stock, by good - water and provisions; nothing on the way counted.
std::array<int,GOOD_COUNT> EconomyTowerShort(const EconomyState& e, const ChasmWorld& w, uint32_t id, int plot);
//What is on its way to site or tower `id`, by good: a load for each carrier going to fetch for it, what each
//carries there. `except` is left out.
std::array<int,GOOD_COUNT> EconomyComing(const EconomyState& e, uint32_t id, const EconomyWorker* except = NULL);
//Standing on his post (ECONOMY_POST_REACH), rather than on his way to it or cut off from it.
bool EconomyAtPost(const EconomyState& e, const EconomyWorker& k);
//The tower whose platform he stands on as its guard, 0 none - where he lives on its stock (POSTS).
uint32_t EconomyOnTower(const EconomyState& e, const ZoneState& z, const EconomyWorker& k);

/*
    SHUT IN: none of a building's plots can be walked onto from open ground - it is closed in on every side by
    other buildings, walls or water - so it has no door (Economy::Derive, Walkers::OpenFromOutside). A house
    shut in is UNINHABITABLE: no family moves in, and one already living there cannot get out until a plot
    beside it is cleared. Worked out from the zones, never saved; set for every kind of building, so a
    workplace or a store can ask it too.
*/
inline bool EconomyShutIn(const EconomyState& e, uint32_t id){
    return id < e.shut.size() && e.shut[id] != 0;
}

//A mother's children so far (BIRTHS), the living and the dead.
int EconomyChildrenOf(const EconomyState& e, uint32_t mother);
//Too close kin for a couple (COUPLES): one the other's parent or grandparent, or a parent or grandparent shared.
bool EconomyRelated(const EconomyState& e, const EconomyWorker& a, const EconomyWorker& b);
//The surname a couple's children take: the mother's or the father's, by a hash of the two - the dead's kept too.
int EconomyCoupleSurname(const EconomyState& e, uint32_t mother, uint32_t father);

//OPEN GROUND a crate may lie on (CRATES): dry, off the mountain, nothing built on the plot - no building (a
//camp's tents and a bridge count), garden, lot or wall, planned or standing - and no field over the cell at `at`.
//A road is open.
bool EconomyCrateGround(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& at);
//The crate lying on `plot`, NULL for none.
const EconomyCrate* EconomyCrateOn(const EconomyState& e, int plot);
//Whether zone op `op` (ZoneOp) on `index` - a plot, or a coarse cell for a cell op - would cover a crate: a building
//on its plot, ground other than road there (`value`, ZONE_GROUND_*, for ZONE_OP_GROUND_PAINT), a field or a range
//over its cell. Play mode refuses it (the user's): the crate has to go first.
bool EconomyCrateInTheWay(const EconomyState& e, const ChasmWorld& w, int op, int index, int value);
const char* EconomyCrateOriginName(int origin);

//As a save holds it.
struct EconomySaved{
    std::vector<std::pair<uint32_t,std::array<int,GOOD_COUNT>>> stocks;    //non-empty ones only
    std::vector<std::pair<uint32_t,std::array<int,GOOD_COUNT>>> pantries;   //camps' (STORES), non-empty ones only
    std::vector<std::pair<int,int>> props;              //felled props: index, state
    std::vector<EconomyWorker> workers;
    std::vector<EconomyField> fields;
    std::vector<std::pair<uint32_t,std::array<int,GOOD_COUNT>>> sites;     //sites with goods brought: building, goods
    std::vector<std::pair<int,int>> ground_sites;       //gardens and lots with wood brought: plot, wood
    std::vector<std::pair<uint32_t,int>> heats;         //homes not HEAT_NONE: building (0 the homeless), heat
    std::vector<EconomyCrate> crates;
    uint32_t next_crate = 1;
    std::vector<std::pair<uint32_t,int>> guards;        //towers' guard counts: building, count
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

    //`calendar_tick` the save's, which its people's ages are read against; `walkers` finds the doors.
    void Restore(std::shared_ptr<const ChasmWorld> world, const EconomySaved& saved, const ZoneState& z,
                 Walkers& walkers, uint64_t calendar_tick);
    //A debug edit (the panel's villager list, CHASM_CMD_NEEDS): a person's health and water in ticks, each
    //held to its bar; -1 leaves one as it is. Applied before the tick's needs, so someone at home with food
    //and water eats and drinks straight back up; health 0 kills only someone who cannot. False with no such person.
    bool SetNeeds(uint32_t person, int health, int water);
    //A debug edit (CHASM_CMD_STOCK, the tool's set_stock): building `id`'s stock of `good` set to `amount` - a
    //camp's supplies, or with f_pantry its pantry. For putting provisions in a store, or emptying one, without
    //waiting for it. False if none.
    bool SetStock(uint32_t id, int good, int amount, bool f_pantry = false);
    //PROVISIONS (POSTS): how many are still wanted beyond what is made and being made - below 0, made and not
    //wanted. The rules' own figure: call it on the simulation thread, between ticks.
    int ProvisionsLacking(const ZoneState& z) const;
    /*
        An ORDER (POSTS, CHASM_CMD_ORDER): person `person` sent to stand on `plot`, or released from his post
        with -1. Applied between ticks, like SetNeeds; what he was doing he drops on the tick that follows. False,
        and `why` says it, if refused: nobody by that id, a child, nothing open to stand on, no way there.
    */
    bool Order(uint32_t person, int plot, const ZoneState& z, Walkers& walkers, std::string* why);
    /*
        GUARDS BY HAND (POSTS, step 8; CHASM_CMD_GUARD): person `person` made a guard of tower `tower` by the
        player - a right click - over an automatic pick; or with `tower` 0 released from his tower, its count
        dropping so nobody takes his place. Applied between ticks; he goes on the tick that follows, as for an
        order. False, and `why` says it, if refused.
    */
    bool PostGuard(uint32_t person, uint32_t tower, const ZoneState& z, Walkers& walkers, std::string* why);
    //A tower's GUARD COUNT (CHASM_CMD_GUARDS): how many guards it takes, 0 up to its places. False if no tower.
    bool SetGuards(uint32_t tower, int count, const ZoneState& z);
    /*
        A CRATE put down by the debug tools (CHASM_CMD_CRATE): `amount` of `good` on `plot` - into the crate lying
        there if there is one, so a crate of several goods is several of these. A find (CRATE_FOUND). Applied
        between ticks, like SetNeeds. False, and `why` says it, on ground a crate may not lie on.
    */
    bool SpawnCrate(int plot, int good, int amount, const ZoneState& z, std::string* why);
    EconomySaved Save() const;          //EconomySaveState of the live state
    const EconomyState& State() const { return state; }
    //The plots whose next storey stands after this tick - the app raises them (ZONE_OP_BUILD_RAISE).
    const std::vector<int>& Raises() const { return raises; }
    //The garden and lot plots built this tick - the app raises them (ZONE_OP_GROUND_RAISE).
    const std::vector<int>& GroundRaises() const { return ground_raises; }
    //The field cells built this tick - a field site's, all at once - the app raises them (ZONE_OP_FIELD_RAISE).
    const std::vector<int>& FieldRaises() const { return field_raises; }
    //A cell of each field that waited ECONOMY_CROP_GRACE for a crop this tick - the app starts it with AUTO.
    const std::vector<int>& CropDefaults() const { return crop_defaults; }
    //The standing trees in a planned road's or wall's way, to be felled before it is laid or built. Physics
    //thread - or a tool between ticks (Scene::AtTickBoundary), which is how chasm_economy lists them.
    void RoadTrees(const ZoneState& z, int v, std::vector<int>& out) const;

private:
    EconomyState state;
    std::vector<int> raises;                    //this tick's, in the order built
    std::vector<int> ground_raises;
    std::vector<int> field_raises;
    std::vector<int> crop_defaults;
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
    std::vector<std::vector<int>> arches_of;    //by building id: the road plots its ARCHES stand over (arches_plan.md), in index order
    std::vector<std::vector<int>> tents_of;     //by building id: a camp's tent plots
    std::vector<int> share_of;                  //by building id: a field's share of its owner's land, -1 none (LandPlaces)
    std::vector<float> field_pace;              //by building id: its farmer's pace, 0 fallow (Tending)
    bool f_resettle = false;                    //a loaded save's land not yet settled (an older save's)
    bool f_restaff = false;                     //an order given or ended since the last tick: staffing again
    uint64_t now = 0;                           //the tick being run, for the rules that ask the date inside it
    float tree_cell = 16.0f;                    //the trees in buckets of this many world units
    int tree_nx = 0, tree_nz = 0;
    vec2 tree_origin;
    std::vector<int> tree_start;                //per bucket + 1, into tree_items
    std::vector<int> tree_items;                //prop indices, in index order within a bucket
    void BuildTrees();
    //What follows from the zones: stocks sized (and a split store's shared out, if f_share), what
    //each store takes and its room, each building's door (where its worker stands); workers and fields
    //kept in step with the buildings.
    void Derive(const ZoneState& z, Walkers& walkers, bool f_share);
    void KeepAtDoors();                         //anyone indoors moved to his house's door, wherever Derive put it
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
    //POSTS: what he was doing dropped for his post; and the way there - with none, waiting where he is.
    void TakePost(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void GoToPost(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //His pack filled from a stock, up to NEEDS_PACK_*; and, home again, emptied back into his pantry.
    void FillPack(EconomyWorker& k, std::array<int,GOOD_COUNT>& from);
    void Unpack(EconomyWorker& k, const ZoneState& z);
    //CRATES: his load put down - into a store he stands at that takes it, else as a crate where he stands or on
    //open ground near it; `amount` of `good` into the crate on `plot` (a new one at `at` if none lies there); and
    //the crates whose ground has been built over, lost.
    void DropLoad(EconomyWorker& k, const ZoneState& z);
    void AddToCrate(int plot, const vec2& at, int good, int amount, int origin, uint32_t by);
    void LoseCoveredCrates(const ZoneState& z);
    //...and as a place goods are taken from (step 2): the stock behind a worker's `store` - a building's, or a
    //crate's (ECONOMY_STORE_CRATE) - NULL if there is none or it is gone; a crate by its id; a crate touched,
    //taken off the map if that emptied it; and where a person stands to take from one, on the side he comes from.
    std::array<int,GOOD_COUNT>* SourceStock(uint32_t id);
    const std::array<int,GOOD_COUNT>* SourceStock(uint32_t id) const;
    const EconomyCrate* CrateById(uint32_t id) const;
    void CrateTouched(uint32_t store);
    vec2 CrateStand(const EconomyCrate& c, const vec2& from) const;
    //Where he goes to take from `id` at `plot`: beside a crate, else the plot's point.
    vec2 SourcePoint(uint32_t id, int plot, const vec2& from) const;
    //A caretaker's last errand: a crate's goods off to a storehouse. True if he set off.
    bool MoveCrate(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //A caretaker's first errand (POSTS, step 7): water or provisions to a guard tower short of them. True if he set off.
    bool KeepTowers(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //PROVISIONS (POSTS): a builder's errand for them, from home (true if he set off); what he does at his hut.
    bool MakeProvisions(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void AtWorkshop(EconomyWorker& k, const ZoneState& z, Walkers& walkers, float pace);
    //ROPE (goods_plan.md): a ropemaker's errand, from home (true if he set off); what he does at his ropewalk.
    bool MakeRope(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void AtRopewalk(EconomyWorker& k, const ZoneState& z, Walkers& walkers, float pace);
    void Walk(EconomyWorker& k, float dt);
    //The people: the settlers a new colony starts with; families into houses; workers into workplaces.
    void FindCamp();
    void MakeSettlers();
    //A child at home: out to a plot or two away to play, now and then. True if he set off.
    bool GoPlay(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t tick);
    //LIFELINES, a tick of them: the stages moving on and the births. True when the places want sharing out again;
    //`f_house` when someone came of age to pair (COUPLES), so the free houses want sharing out too.
    bool Lifelines(uint64_t tick, bool& f_house);
    //BIRTHS, at dawn after the fires are lit: the couples whose day it is, and their chance of a child.
    void ChancesOfChildren(const ZoneState& z, uint64_t tick);
    void HouseFamilies(const ZoneState& z, Walkers& walkers);
    void AssignJobs(const ZoneState& z, Walkers& walkers);
    int HomePlot(const EconomyWorker& k) const;
    bool AtHome(const EconomyWorker& k) const;
    bool CanWalk(const ZoneState& z, Walkers& walkers, const vec2& from, int to);
    //Can he get to plot `to` from where he stands - on a deck, perhaps - as one posted there would go (ramparts_plan.md).
    bool CanWalkPosted(const ZoneState& z, Walkers& walkers, const EconomyWorker& k, int to);
    bool FetchFromWorkplace(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //A field of his land built, empty, its crop decided and its share farmed: out to sow it (fields_plan.md,
    //land_plan.md). True if he set off.
    bool GoSow(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t calendar_tick);
    //A trainee at home: out to his place on his range's line (range_plan.md). True if he set off.
    bool GoTrain(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
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
    bool SeekSupplyOf(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_home, GoodsMask want);
    //Fetching home (needs_plan.md, step 2): what his house is short of; off to fetch it; a load of it picked
    //up at `from` and carried home; and the way home itself.
    GoodsMask HomeShort(const EconomyWorker& k, const ZoneState& z, GoodsMask* first) const;
    //HEAT (above): every home's fire at dawn; what house h has room for of `good` (its woodshed for wood, the
    //rest of its room for food and water); whether it fetches firewood now; and its fair share of it.
    void Heat(const ZoneState& z, uint64_t calendar_tick);
    int HouseRoom(const ZoneState& z, uint32_t h, int good) const;
    //WHOSE IT IS (STORES, above): building `id`'s stock a family's own - a house somebody lives in - so nobody
    //else may take from it. A camp's stock is its supplies, never private; its families' own is its pantry.
    bool PrivateStock(const ZoneState& z, uint32_t id) const;
    //A home's own goods, where its fetching goes - a house's stock, a camp's pantry; and all it holds for its
    //family to live on - a camp's pantry and supplies together.
    std::array<int,GOOD_COUNT>& HomePantry(const ZoneState& z, uint32_t h);
    std::array<int,GOOD_COUNT> HomeHeld(const ZoneState& z, uint32_t h) const;
    //A camp the last of its people has left: its pantry into its supplies, at once.
    void OpenPantries(const ZoneState& z);
    bool FirewoodNow(uint32_t h) const;
    int FirewoodFair(const ZoneState& z, uint32_t h) const;
    bool FetchHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //Where a home may fetch from (the user's, 2026-10-09): a store, a camp's supplies, a house nobody lives in, a
    //water collector, a woodcutter's pile, or its own land's field within its fair share - never its own home,
    //another family's house or another family's field.
    bool FetchFrom(const EconomyWorker& k, const ZoneState& z, uint32_t id) const;
    bool TakeHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint32_t from, GoodsMask goods);
    bool RouteHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //Construction: what a site still wants, carrying for one, and building with what it has. A site is
    //a building id or a garden or lot plot (ECONOMY_SITE_GROUND); each by good (CONSTRUCTION).
    std::array<int,GOOD_COUNT> SiteNeed(const ZoneState& z, uint32_t id) const;
    std::array<int,GOOD_COUNT> SiteBrought(uint32_t id) const;
    bool SiteAlive(const ZoneState& z, uint32_t id) const;
    //Where a building's goods are fetched from: a woodcutter's pile's door, else its plot nearest `to`.
    int SourcePlot(uint32_t id, const vec2& to) const;
    //Where a builder stands to build a garden or lot plot: an open plot beside it, else the plot itself.
    int GroundSiteApproach(const ZoneState& z, int v) const;
    std::array<int,GOOD_COUNT> SiteLeft(const ZoneState& z, uint32_t id, const EconomyWorker* except) const;
    //A guard tower that stands whole - kept full, no longer a site - and what it still wants brought to be full.
    bool TowerWhole(const ZoneState& z, uint32_t id) const;
    std::array<int,GOOD_COUNT> TowerLeft(const ZoneState& z, uint32_t id, const EconomyWorker* except) const;
    int GoodFree(uint32_t id, int good) const;
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
GoodsMask EconomyAccepts(const EconomyState& e, uint32_t id);
int EconomyStock(const EconomyState& e, uint32_t id, int good);
int EconomyStockTotal(const EconomyState& e, uint32_t id);
//WHOSE IT IS (STORES): building `id`'s stock a family's own - a house somebody lives in - so nobody else takes from
//it. Economy::PrivateStock is this; the cards ask it too.
bool EconomyPrivateStock(const EconomyState& e, const ZoneState& z, uint32_t id);
//RESERVED: of building (or crate) `id`'s goods, what somebody already on his way counts on taking - a site
//carrier's load, a builder's for provisions, a load fetched home - as the rules count them (GoodFree, FetchHome).
//A meal there is a unit, not a load, and only those after one count it (SeekSupply): not here. For the cards.
std::array<int,GOOD_COUNT> EconomyReserved(const EconomyState& e, uint32_t id);
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
//The layer of point i of his route (ramparts_plan.md), and the one he is on now: the leg's start's until half way
//along it - the edge between two plots - and its end's after.
int EconomyRouteLayer(const EconomyWorker& k, int i);
int EconomyWorkerLayer(const EconomyWorker& k);

const char* EconomyWorkerStateName(int state);
const char* EconomyJobName(int job);
//The job a kind of building employs, WORKER_JOB_NONE for one that employs nobody. A house and the camp employ
//their land's farmers; a field nobody (land_plan.md).
int EconomyJobOf(int kind);
//A ROPEWALK (goods_plan.md): its plots that stand - it works from ECONOMY_ROPEWALK_MIN_PLOTS - and how fast it
//makes rope against one that short: 1 at 2 plots, 1.5 at 3, 2 at 4. 0 for anything but a ropewalk that works.
int EconomyRopewalkPlots(const ZoneState& z, uint32_t id);
float EconomyRopewalkPace(const ZoneState& z, uint32_t id);
//All of a good the colony has, wherever it lies - stores, the camp, houses, workplaces, fields, crates - and what is
//being carried; not what sites have been brought. What a site short of it could still be brought.
int EconomyGoodHeld(const EconomyState& e, int good);
/*
    What the colony still lacks to MAKE ROPE (goods_plan.md), the first missing first: HEMP - none grown, sown, chosen
    for a field or held - then a ROPEWALK, placed; ECONOMY_ROPE_MAKING when both are there. For the card of a site
    short of rope and NEEDS ATTENTION, which point at what to do.
*/
#define ECONOMY_ROPE_NO_HEMP        0
#define ECONOMY_ROPE_NO_ROPEWALK    1
#define ECONOMY_ROPE_MAKING         2
int EconomyRopeChain(const EconomyState& e, const ZoneState& z);
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
//A person's name, from his id and his surname: for the panel and the tools.
std::string EconomyPersonName(const EconomyWorker& k);
std::string EconomyDeathName(const EconomyDeath& d);
//A settler family's surname (COUPLES): theirs from the start - and a save from before surnames were kept.
int EconomySettlerSurname(uint32_t family);
//A surname by its number, alone ("Veen").
std::string EconomySurnameName(int surname);
//Inside his house or tent, so not drawn.
inline bool EconomyIndoors(const EconomyWorker& k){
    return k.f_indoors && k.house != 0 && k.state == WORKER_AT_HOME;
}
//Construction (CONSTRUCTION): what one storey on `plot` takes of `good`; what all a building's planned storeys -
//a field site's cells - still take, by good; and what has been brought to it.
int EconomyStoreyCost(const ChasmWorld& w, const ZoneState& z, int plot, int good);
//...and what a coarse cell of a site painted on cells (a field) takes of `good`, by its kind.
int EconomyCellCost(int kind, int good);
std::array<int,GOOD_COUNT> EconomySiteCost(const ChasmWorld& w, const ZoneState& z, uint32_t id);
int EconomySiteBrought(const EconomyState& e, uint32_t id, int good);
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
//worked out, for either). -1 while its starting crop is not chosen.
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

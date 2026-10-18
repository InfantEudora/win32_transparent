#ifndef _CHASM_ECONOMY_H_
#define _CHASM_ECONOMY_H_

#include <algorithm>
#include <array>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "type_vec2.h"
#include "type_vec3.h"
#include "Calendar.h"
#include "Goods.h"
#include "Zones.h"

class Walkers;
class Exploration;
struct PropInstance;

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
    and lays roads. A worker's skill sets how fast he works (EconomySkillFactor). A workplace the player STOPPED
    (ZoneBuilding::f_stopped, ZoneKindStoppable; stopping_work_plan.md) has no places: its worker goes free, and
    what it makes stops - a collector fills only while worked.
    LAND (docs/land_plan.md): a field employs nobody itself. Every field that stands belongs to a house -
    its LAND - or, while no house is lived in, to the camp (`EconomyField::owner`, SettleLand): the
    player's choice (ZoneBuilding::land_of) if that house is lived in, else the house it has, else the
    nearest lived-in house, else the camp - so the camp's land passes to a house as soon as there is one.
    The land is its owner's workplace: shared out in field order, a farmer to each ECONOMY_FARMER_CELLS
    cells or more of whole fields (LandPlaces), and only its own family - for the camp, whoever lives
    there - fills those places, in the house's place in the order. A field whose share has no farmer
    LIES FALLOW (`farmer_of` 0).
    THE CAMP'S OWN WORK (woodcutting_plan.md, the user's 2026-10-11): ECONOMY_CAMP_FARMERS on its land at most -
    the rest lies fallow until a house takes it - and ECONOMY_CAMP_WOODCUTTERS of its own (`cutter_places`),
    filled as its land is, by those living there, the strongest first. What they leave open - all of it once the
    last family has moved into houses - goes to the rest of the COLONY (one camp to a colony, its work the colony's):
    to those no other place needs, after every workplace and never taken from one, by skill less the walk from home
    (the user's, 2026-10-14). So the camp's work goes on from houses, after their fields.
    The camp's woodcutter fells from the camp, with no hut: the least of a log (ECONOMY_CAMP_LOG_WOOD), into a store that takes wood or else the camp's supplies, while
    they hold less wood than ECONOMY_CAMP_WOODPILE. So more of either takes houses and huts - and a woodcutter's hut
    standing in its settlement TAKES his place OVER (the user's 2026-10-11): the camp has none of its own then.
    And ECONOMY_CAMP_WATER_CARRIERS (`drawer_places`, settlements_plan.md) where a river's or pool's edge lies within
    ECONOMY_CAMP_WATER_REACH of walking (`draw_plot`, the dry plot beside the wet bank nearest by the walk), filled
    after its woodcutter, the most ingenious first: he walks there, draws ECONOMY_CAMP_WATER_LOAD at
    ECONOMY_CAMP_WATER_SECONDS a unit, and carries it home into the camp's supplies, while they hold less water than
    ECONOMY_CAMP_WATER_HOLD - never under the snow. Enough for an outpost of three; a village wants its collector,
    and a collector standing in its settlement TAKES his place OVER as a hut does the woodcutter's - worked or
    STOPPED (stopping_work_plan.md): the player stops one with water plenty in store, so nobody draws it by hand.

    A worker walks a ROUTE of points: the grid's A* (Walkers::PlanRoute) where he must, and STRAIGHT where
    the line is clear (EconomyStraightClear) - in the open he need not follow the grid (the user's note).
    Walking, he re-plans when the zones change. EVERY ROUND STARTS AND ENDS AT HIS HOUSE (the user's
    choice: so a house beside the workplace and its store is what makes a worker quick):
      - The WOODCUTTER takes, within ECONOMY_WOODCUTTER_REACH of walking from his hut, the nearest felled log lying
        there - LOGS FIRST, the road builders' (the user's, 2026-10-08) - else the nearest standing tree;
        walks to it, fells it if it stands (it leaves a log and a stump), picks the log up - and, with
        another log lying within ECONOMY_LOG_PAIR_REACH and room for both where they go, that one too
        (ECONOMY_WOODCUTTER_LOGS) - and carries them, each worth what his WORKPLACE makes of a log (`log_wood`,
        woodcutting_plan.md: a bigger shop makes the work easier - ECONOMY_WOOD_PER_TREE from a hut,
        ECONOMY_LOFT_LOG_WOOD from one two storeys high on every plot, ECONOMY_CAMP_LOG_WOOD from the camp), to the nearest store
        that takes wood - or, with none, to his WOODPILE - and goes home. From home the pile goes first:
        whenever a store takes wood he fetches from the pile before he fells again. THE WOODPILE'S ROOM
        (`pile_room`, woodcutting_plan.md, the user's: a cheap storehouse) is his hut's INDOORS, by its floor
        area past the floor he works in (ECONOMY_HUT_ROOM past ECONOMY_HUT_WORKSHOP, EconomyHutRoom, `hut_room`) - so
        a hut on a big plot keeps a lot, and one on a tiny plot none and needs a lot to pile on - and the LOTS of
        his own yard (yards_plan.md), by theirs (ECONOMY_LOT_ROOM). A buffer: what lies
        there goes on to a store that takes wood. He fells only while a store or the pile has room for a log.
        The lot's pile is drawn on the lowest lot plot beside his hut (`pile_plot`); a lot is fenced, and the
        HUT IS ITS DOOR: wood is put down and taken up from the hut plot beside it (`pile_door`), or with no lot
        at the hut's own door - nobody walks into the yard. A hut too small to keep a log, with no lot, fells
        nothing: its card and NEEDS ATTENTION say so.
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
      - A COLLECTOR fills while it has a carrier (not under snow); he fetches its water in loads. What it
        holds goes by its standing floor, as a hut's room does (EconomyCollectorRoom, `water_room`): a load at
        the least, or its carrier would never come, and more for the floor past its pump.
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
    A SITE OVER TREES (woodcutting_plan.md, the user's 2026-10-11 - "making sitting on the edge of a forest
    preferred"): the trees on its plots, or a field's cells, stand until a woodcutter fells them - the first he
    goes for, with a road's - and nothing of it rises while one does (SiteTreesStanding); felling the last
    raises what its goods pay for (Felled). Carried for meanwhile as ever.
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
    A ROAD painted in play is a site as well (ZoneRoadPlanned), and costs no wood - only work. ONLY A WOODCUTTER
    FELLS A TREE (woodcutting_plan.md, the user's 2026-10-11 - roads painted everywhere had cleared a forest
    with nobody's axe): the standing trees in a planned road's or wall's way (RoadTrees) are the first a
    woodcutter goes for within his reach (WoodNear), and a line with trees no woodcutter reaches waits
    (`trees_unreached`, NEEDS ATTENTION). When an idle person finds no wood to carry, he goes out to the
    nearest plot of a planned road nobody else is on with nothing of its way standing, and LAYS it
    (ECONOMY_LAY_SECONDS at his pace), which goes down through ZONE_OP_GROUND_RAISE like a garden's
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
    In winter the posted walk the ICE (Walkers::SetSeason; seasons_plan.md, the user's 2026-10-12) - frozen
    water, where the snow lies - and nobody else; an order onto the water itself is refused. Still over it at
    the thaw, he is cut off as above: a risk, warned of by the walk tip and by his card (IceHome, `ice_home`).
    His PACK is what he takes along to live on: water and food, apart from any load (NEEDS_PACK_*). Filled
    from his home's pantry when he sets out from home, and topped up at every supply trip; eaten from as he
    would eat at home, a whole unit whenever it fits, so his bars stay up while it lasts and a pressing need
    comes only once it is empty. Released, he lives on it on his way home too, and home again it goes back into
    the pantry.
    GATHERING (the scout's, defence_plan.md "Scouting, provisioned first"): someone ordered out may be told first to
    fill his pack as full as he can carry (NEEDS_SCOUT_*) - from the places a pressing need sends the posted to,
    his home, a store, the camp's supplies, a collector, a field's harvest, a crate (SeekSupplyOf), the nearest
    first and provisions first where there are any; a second place for what the first lacks (ECONOMY_GATHER_STOPS);
    then on to his post. With nothing to be had, or nowhere he can get to, he goes on as he was (Gather,
    GatherSupplies).
    PROVISIONS (GOOD_PROVISIONS, step 5) are a soldier's food, worth a wheat and a beans together, and OUTSIDE
    GOODS_FOOD (Goods.h): nobody fetches them home and no home holds them. A posted person's food is any food,
    provisions too - the nearest place first, provisions first into his pack. A villager's is the crops; with
    none in any place he can reach he looks again with provisions, his LAST RESORT; and wherever anyone eats,
    provisions go last. MADE by the CARETAKERS - the builders - and only as many as are WANTED: what sites
    and towers still lack of them and what the colony KEEPS (STOCK KEPT, below), less what lies made in stores,
    the camp, crates and the builders' huts, and what is being made (ProvisionsLacking). A builder, before his building work: with
    wheat and beans at his hut, makes them there (ECONOMY_PROVISION_SECONDS each at his pace, up to a load a
    round); with too few on hand and on their way for what is wanted, fetches a load of the scarcer from a
    store, the camp or a crate. The sites' carriers take them from the hut as from a store. What the hut holds
    beyond what the sites still lack goes to a store that takes it, the kept ones among them, and its wheat and
    beans once none are wanted. A hut's stock is for this alone: nobody eats from it.
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
    RUBBLE (docs/stone_plan.md) is a crate of STONE, CRATE_RUBBLE, so all of the above is true of it too: the
    colony's first stone, fallen from the chasm's walls. A new colony's first tick lays it (PlaceRubble), from
    the seed: at the FOOT of a balcony's wall, never on the rim, each pile pushed toward the wall within its plot -
    and along a floatstone SEAM's foot, piles of its crystal, FLOATSTONE (EconomyCrateCrystal). Only on open
    ground; a save keeps what is left of it.
    PICK UP (stone_plan.md), the player's, on any crate: the builders move it before building - after the towers and
    provisions - and from anywhere, an order being the player's; to a store of their own settlement as ever. But
    not past what he can walk without water: a crate whose walk there and on to the store is longer than
    ECONOMY_PICK_UP_WATER of a full water bar is turned down, on the walk alone (the user's, 2026-10-15), and says so.

    --- THE STONECUTTER (docs/stone_plan.md) ---------------------------------------------------------
    A STONECUTTER'S HUT (ZONE_KIND_STONECUTTER) is a woodcutter's hut for stone, built with wood and stone: one place,
    a STONECUTTER (WORKER_JOB_STONECUTTER, by strength). From home, while a store of his has room for stone, within
    ECONOMY_STONECUTTER_REACH's walk of his hut (FindRock): a BOULDER on a site of his settlement's first - a site waits
    for its boulders as for its trees (SiteTreesStanding) - then RUBBLE lying, then the nearest ROCK. A ROCK is a prop of
    the forest (ForestIsRock): STANDING, or broken and CLEARED (PROP_STATE_STUMP's value). Broken (BreakRock), at
    ECONOMY_BREAK_SECONDS a stone of it at his pace, its stone (ForestRockStone) is left where it stood as rubble - a
    crate, so taking it is the crates' - and he lifts a load of it to a store there and then. Only he breaks a rock;
    one built over is gone, as a tree is.

    --- THE MINE (docs/stone_plan.md, step 5) ---------------------------------------------------------
    A MINE (ZONE_KIND_MINE) is one plot on a DEPOSIT (Deposits.h; the zones' MineIsBuildable): a floatstone seam's foot,
    the mountain's foot, an outcrop - built with wood and ECONOMY_MINE_STONE stone. One place, a MINER
    (WORKER_JOB_MINER, by strength). From home, while its deposit has some left and a store of his has room for a load
    of what it gives, he walks to the mine (GoDig), digs a load - ECONOMY_MINE_SECONDS a unit at his pace, at most
    what is left - and carries it to that store, up a winch from a balcony as anyone would. What has been DUG from each
    deposit (`dug`) is saved and hashed. A seam holds DEPOSIT_SEAM_FLOATSTONE: dug out, its mines have no place - its
    miners go free - and its band in the wall is rock again. The mountain and the outcrops never run out.

    --- ROPE (docs/goods_plan.md) -------------------------------------------------------------------
    A ROPEWALK (ZONE_KIND_ROPEWALK) works from ECONOMY_ROPEWALK_MIN_PLOTS standing plots: its one place, a
    ROPEMAKER (WORKER_JOB_ROPEMAKER, by precision). From home he takes a load of rope waiting there to a store
    that takes it, while the stores hold less than the colony keeps (FetchFromWorkplace, rope only, up to
    KeptShort); else, with hemp at the ropewalk, makes rope of it there -
    ECONOMY_ROPE_HEMP a rope, ECONOMY_ROPE_SECONDS each at his pace, faster on a longer ropewalk
    (EconomyRopewalkPace), up to a load a round; else fetches hemp from a store, the camp or a crate (MakeRope,
    AtRopewalk - the builders' provisions errand's states). He makes none past ECONOMY_ROPEWALK_PILE waiting
    there. No count of what is wanted, unlike provisions: hemp has no other use. The rope there is anyone's -
    sites' carriers take it as from a woodpile - and its hemp the ropemaker's alone.
    A WINCH is built with it, ECONOMY_WINCH_ROPE a storey (StoreyGoods), and the colony starts with none: a winch
    site stands staked out, its wood brought, until a hemp field and a ropewalk have made its rope.

    --- STOCK KEPT (docs/goods_plan.md, "Kept in stock, and made to order") -------------------------
    A MADE good (GOODS_MADE: provisions, rope) has a KEPT number - the colony's, set by the player - of it to
    hold in the stores and the camp's supplies (EconomyStoredTotals, the top bar's count), and may have an ORDER
    for more: a level for those to reach once, cleared when they do (OrderMore). The higher of the two is its
    LEVEL (EconomyKeptLevel). Provisions are made up to it on top of what sites and towers want
    (ProvisionsLacking). Rope is made to the ropewalk's pile whatever the level - hemp has no other use - and
    carried off to a store only while the stores hold less (KeptShort), so by default none goes there and the
    stores never fill with it. Both start at ECONOMY_KEPT_START. Set by recorded commands (CHASM_CMD_KEEP);
    saved and hashed.

    --- SETTLEMENTS (docs/settlements_plan.md) -------------------------------------------------------
    A HOME founds a SETTLEMENT - a camp or a house: a tent is just a house (the user's) - the start camp the first.
    Homes that stand, in id order, so the oldest first: one outside every earlier settlement's territory founds its
    own, named by its id; one inside is more beds for that one. So a settlement lasts while any home of it stands,
    and with its first camp gone its oldest house is its centre. Its TERRITORY starts as every plot within its REACH
    of walking from that home over the MAP (Walkers::MapFlood) - never what is built on it. Its reach goes by its
    BEDS, tents and houses' room (EconomySettlementReach): ECONOMY_SETTLEMENT_FIRST for one, up to
    ECONOMY_SETTLEMENT_REACH. Then it GROWS FROM EVERY BUILDING of it that stands (the user's, 2026-10-12):
    ECONOMY_SETTLEMENT_BUILDING round each, ECONOMY_SETTLEMENT_TOWER round a guard tower, ECONOMY_SETTLEMENT_WALL round
    each built wall plot - not a site, a field, range, road, garden or lot - and what stands on land another added
    grows it on. Each plot goes to the settlement it is the shortest walk from, so where two meet the border falls
    between them. A building,
    field, garden, lot, road, wall or crate is in the settlement of its plot (a building's door; a bridge's, either
    end's); a PERSON in his home's. Worked out from the zones (Settle), never saved. A family whose home goes is CRAMMED into another of the
    settlement it stands in (HouseFamilies).
    TAKING A TENT (settlements_plan.md, "Founding: taking a tent"): someone ordered out may be told to take one
    (TakeTent) - off to the nearest camp with one to spare, as GATHERING goes for supplies; there he packs it, its plot
    out of the camp (TentsPacked), and carries it as his load (ECONOMY_TENT_CARRIED) - released too - until he PITCHES
    it where he stands (PitchSpot, Zones::PitchTent, Pitched): a camp plot of its own or beside one, his home from then
    on. His house place he keeps until then, and gives up then: a household of his own, as a new couple is - unless
    his partner lives in that settlement: then he REJOINS them, at their home (PartnerLivingIn), as SETTLING HERE
    does too. A family of nobody but tent carriers, with no home, is ON THE ROAD: no home is found for it - it
    pitches its own.
    WITH THE GROWN (settlements_plan.md, the user's 2026-10-12): nobody young is left behind, nor given an order. A
    CHILD goes along when every grown-up of its household is LEAVING - carrying or taking a tent, or posted onto
    another settlement's ground: posted beside its mother if she is one of them, else its father, else the eldest
    (GoesWith, WithTheGrown), packing and gathering as a scout, with no tent. A BABY is carried by its mother,
    unseen, whenever she is posted and until she is home again, fed from her pack. Pitching or settling, whoever
    goes with him moves into the new home with him (BringAlong). ORPHANS - a household with no grown-up left -
    squeeze into a home of their settlement where a grown-up lives (HouseFamilies).
    THE LIMIT: people take tasks only in their own settlement (Ours) - a site to carry for and where from, line work,
    a job, fetching home, the store a load goes to, the builders' and the ropemaker's errands. Families move only
    into houses of theirs; land goes only to a house or camp of its field's. NOT limited: a post and its supply
    trips - an order is the player's. A PRESSING NEED's supply only of his own settlement or land none holds
    (SeekSupplyOf, the user's 2026-10-11) - never another village's. A play command placing anything
    outside every territory is refused (EconomyCommandInTerritory); erasing never is.

    --- THE FOREST ----------------------------------------------------------------------------------
    `prop_state`, one byte per prop of the world's forest (ForestData::props, whose order is fixed for
    a world): standing, felled with its log lying, or a stump with the log taken. Saved and hashed.

    --- FORAGING (docs/foraging_plan.md) -------------------------------------------------------------
    FRUIT from the wild, to keep a camp of a few going before its fields bear (the user's, 2026-10-12). A PLANT
    bears it - a bush, or an oak the forest made a fruit tree (ForestBears) - while it stands: `fruit`, by prop,
    is what is on it, FORAGE_BUSH_FRUIT or FORAGE_TREE_FRUIT ripe. Picking takes what the picker has room for and
    leaves the rest; once EMPTY, it grows its fruit back over FORAGE_REGROW_DAYS dawns of growing weather
    (`regrow`). At a dawn in winter or under the snow, what is on it is lost - THE FROST, as a field's - and no
    day counts. Built over (EconomyPropCovered) it is GONE for good (FORAGE_GONE), whatever stands there later.
    A new map's plants are all ripe. Saved - those not ripe and full - and hashed.
    THE CAMP'S PICKER (ECONOMY_CAMP_PICKERS, `picker_places`): a place at a camp with a plant in its settlement, after
    its water carrier, by husbandry - and from the rest of the colony as the camp's other work. From home he goes to the nearest ripe plant of his own settlement, picks it at
    FORAGE_PICK_SECONDS a fruit at his pace, and on to the next within FORAGE_ROUND_REACH of him until he has a load
    (ECONOMY_LOAD) or none is left near - then into the camp's supplies, while they hold less fruit than
    ECONOMY_CAMP_FRUIT_HOLD. Nothing takes his place over: picking fades as fields and houses cover the bushes.
    BY HAND (Forage): someone posted is sent to a ripe plant, or to the water's edge, and stands there as his post
    - a one-off: he picks the plant (into his pack, up to a scout's food) or draws water (up to a scout's water,
    at the camp's water carrier's pace), a unit at a time, and stays (`forage`). Only on his own settlement's land
    or land none holds, as a pressing need; never under the snow.
*/

//Per unit of floor area, not per plot: plots differ in size, and what one holds is wiki/cells.md's.
#define ECONOMY_STORE_ROOM          7.5f    //units of goods per unit of floor area
#define ECONOMY_HOUSE_ROOM          4.0f    //a house's, for its own food, water and firewood, per unit of floor area (needs_plan.md)
#define ECONOMY_HOUSE_KEEPS         (GOODS_FOOD | GOOD_BIT(GOOD_WOOD) | GOOD_BIT(GOOD_WATER))
#define ECONOMY_LOAD                5       //what a farmer or a water carrier carries at once
#define ECONOMY_WOOD_PER_TREE       4       //what a log is worth taken up from a woodcutter's hut (woodcutting_plan.md)...
#define ECONOMY_LOFT_LOG_WOOD       5       //...from one with a second storey on every plot: a bigger shop makes the work easier
#define ECONOMY_CAMP_LOG_WOOD       2       //...and from the camp, with no hut at all
#define ECONOMY_HUT_ROOM            6.0f    //wood a woodcutter's hut keeps indoors, per unit of its floor area past...
#define ECONOMY_HUT_WORKSHOP        1.5f    //...the floor he works in (the user's, 2026-10-11): a hut of 3 wood keeps none, one of 4 begins to
#define ECONOMY_LOT_ROOM            2.0f    //...and on a lot of its yard, per unit of its area: about a store's room for the wood it costs
#define ECONOMY_WOODCUTTER_REACH    75.0f   //world units from the hut
//How a woodcutter reaches a plot (EconomyState::wood_reach): not at all, over dry land, or only over the ice in winter.
#define ECONOMY_WOOD_UNREACHED      0
#define ECONOMY_WOOD_REACHED        1
#define ECONOMY_WOOD_ICE            2
#define ECONOMY_SETTLEMENT_REACH    50.0f   //a settlement's territory at most: world units of walking from its oldest home...
#define ECONOMY_SETTLEMENT_FIRST    20.0f   //...with one bed: a single tent pitched (settlements_plan.md)...
#define ECONOMY_SETTLEMENT_PER_BED  5.0f    //...and more for every bed past it, so seven beds reach the most
#define ECONOMY_SETTLEMENT_BUILDING 15.0f   //and every other building of it that stands grows its land this much walking round itself (the user's, 2026-10-12)...
#define ECONOMY_SETTLEMENT_TOWER    30.0f   //...a guard tower this much...
#define ECONOMY_SETTLEMENT_WALL     10.0f   //...and each built wall or gate plot this much
#define ECONOMY_TENT_NONE           0       //TAKING A TENT: a person's `tent` - none...
#define ECONOMY_TENT_TO_TAKE        1       //...told to take one, off to a camp for it...
#define ECONOMY_TENT_CARRIED        2       //...or one packed, carried until he pitches it
#define ECONOMY_PITCH_RINGS         4       //how far from where he stands a tent may go up: rings of plots round him
#define ECONOMY_WOODCUTTER_LOGS     2       //logs he carries off at once, a second lying near the first (the user's)
#define ECONOMY_LOG_PAIR_REACH      12.0f   //...within this of him where he picked the first up: about six plots
#define ECONOMY_FELL_SECONDS        6.0f
#define ECONOMY_PICK_SECONDS        1.0f    //picking up a log, putting a load down
//THE STONECUTTER (stone_plan.md, current tuning): how far from his hut he breaks rocks, how long a stone of a rock
//takes to break at strength 5 - a boulder is the longest work - and the stone a plot-storey of his hut takes.
#define ECONOMY_STONECUTTER_REACH   75.0f
#define ECONOMY_BREAK_SECONDS       2.0f
#define ECONOMY_STONECUTTER_STONE   5
//THE MINE (stone_plan.md step 5, current tuning): how long a stone - or a floatstone - takes to dig at strength 5, and
//the stone a mine is built with besides its wood.
#define ECONOMY_MINE_SECONDS        8.0f
#define ECONOMY_MINE_STONE          15
#define ECONOMY_MINE_FACE           0.3f    //how far toward the rock from his mine's point the miner stands to dig
#define ECONOMY_WATER_SECONDS       4.0f    //a collector fills one unit in this long
#define ECONOMY_WATER_ROOM          6.0f    //and holds, uncarried, a load and this per unit of its floor area past...
#define ECONOMY_WATER_PUMP          1.5f    //...the floor its pump takes (the user's, 2026-10-11, as a woodcutter's hut)
#define ECONOMY_STRAIGHT_STEP       0.5f    //world units between the samples of a straight walk
#define ECONOMY_WORKER_SPEED        1.6f    //walking straight: WALKER_SPEED_GROUND
#define ECONOMY_PERSON_AREA         1.3f    //floor area a person needs: a house holds its floor area over this, whole people
#define ECONOMY_HOUSE_MOST          6       //the largest family a house holds, however big
#define ECONOMY_SKILL_DISTANCE      3.0f    //what a point of skill is worth against the walk to work: a quarter less than it was (the user's, 2026-10-11)
#define ECONOMY_CARRY_STRENGTH      0.025f  //anyone with a load walks this much faster a point of strength over 5, slower under (the user's): ~25% from 1 to 10
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
#define ECONOMY_TENT_ROPE           1       //...and a tent's, painted: its guy lines (settlements_plan.md) - one packed and pitched takes none
#define ECONOMY_KEPT_START          0       //what the colony keeps of a made good before the player says (STOCK KEPT): none
//The MADE goods (STOCK KEPT): made at a workshop from other goods, and kept to a number - a builders' hut's, a ropewalk's.
#define GOODS_MADE                  ((GoodsMask)(GOOD_BIT(GOOD_PROVISIONS) | GOOD_BIT(GOOD_ROPE)))
#define ECONOMY_FARMER_CELLS        6       //a farmer's share of his land: whole fields up to this many cells or more (land_plan.md)
#define ECONOMY_CAMP_FARMERS        1       //the camp's land's shares at most: one farmer (woodcutting_plan.md)
#define ECONOMY_CAMP_WOODCUTTERS    1       //...and its own woodcutter, felling from the camp
#define ECONOMY_CAMP_WOODPILE       60      //...while its supplies hold less wood than this: the settlers' own and some
#define ECONOMY_CAMP_WATER_CARRIERS 1       //...and a water carrier of its own, where a river's or pool's edge is near (settlements_plan.md)...
#define ECONOMY_CAMP_WATER_REACH    30.0f   //...within this much walking of the camp...
#define ECONOMY_CAMP_WATER_SECONDS  16.0f   //...drawing a unit in this long, four times a collector's: some 2.5 a day, enough for three...
#define ECONOMY_CAMP_WATER_LOAD     3       //...carried home this many at a time...
#define ECONOMY_CAMP_WATER_HOLD     12      //...while its supplies hold less water than this
#define ECONOMY_CAMP_PICKERS        1       //...and a picker of its own, where a plant bears fruit in its land (foraging_plan.md)...
#define ECONOMY_CAMP_FRUIT_HOLD     20      //...while its supplies hold less fruit than this
//FORAGING (above). Tuning, set from what a camp's land grows (foraging_plan.md, "Current tuning").
#define FORAGE_BUSH_FRUIT           2       //fruit on a ripe bush...
#define FORAGE_TREE_FRUIT           6       //...and on a ripe fruit tree
#define FORAGE_REGROW_DAYS          10      //dawns of growing weather an emptied plant takes to be ripe again: a season
#define FORAGE_PICK_SECONDS         3.0f    //picking a fruit, at the picker's pace
#define FORAGE_ROUND_REACH          15.0f   //the camp's picker goes on to a plant within this of him, until he has a load
#define FORAGE_SPREAD_REACH         15.0f   //one of a group sent to pick: the nearest ripe plant within this of the one clicked
#define FORAGE_GONE                 0xFF    //a plant's `fruit`: built over, bearing no more
//What someone posted is at by hand (EconomyWorker::forage). Never renumber: saves hold these.
#define FORAGE_NONE                 0
#define FORAGE_FRUIT                1       //picking the plant on his post
#define FORAGE_WATER                2       //drawing water at the edge beside his post
#define ECONOMY_CROP_GRACE          (CALENDAR_DAY_TICKS)    //a field built with no crop waits this long for one, then starts with AUTO (issues_plan.md)
#define ECONOMY_FAIR_DAYS           1       //a house keeps its land's food only while less than this a head ahead of the colony's average
#define ECONOMY_QUEUE_SLACK         8.0f    //errands this much longer than the shortest still go in placing order: ~4 plots
#define ECONOMY_LAY_SECONDS         4.0f    //laying a road plot, at the worker's pace
#define ECONOMY_ROAD_STINT          8       //pieces of road work - a tree felled, a plot laid - a trip
#define ECONOMY_ROAD_REACH          10.0f   //how far from him the next piece of a stint may be
#define ECONOMY_POST_REACH          0.5f    //within this of his post's point he stands on it (defence_plan.md)
#define ECONOMY_POST_ROOM           0.8f    //square units of a plot each of several posted on it takes (selection_plan.md)
#define ECONOMY_POST_STAND          0.75f   //...where they stand: this much of the way from its point to its corners
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
/*
    MEALS (zombies_plan.md, step 2; the user's 2026-10-14): a unit at a time, eaten or drunk over its while, its worth
    coming in a tick at a time - so a meal is seen, on the person's card. Taken from the stock as it starts; the next
    one only once it is done. None starts with a zombie within NEEDS_MEAL_SAFE of him, and one under way waits while
    there is.
*/
#define ECONOMY_MEAL_SECONDS        2.0f                        //eating a unit of food, whatever it is, wherever
#define ECONOMY_DRINK_SECONDS       1.0f                        //...and drinking a unit of water
#define NEEDS_MEAL_SAFE             3.0f                        //no meal with a zombie's body this near his
#define ECONOMY_HOME_DAYS           2                           //a house fetches below this many days of its family's water, food or firewood
#define ECONOMY_FIREWOOD_BURN       1                           //wood a home under the snow burns at dawn, a plot-storey
#define ECONOMY_WOODSHED_DAYS       4                           //a house's room for firewood, apart from its food and water's: days of burning
#define NEEDS_COLD_DRAIN            1                           //health lost a tick more while cold
#define NEEDS_PACK_WATER            3                           //a posted person's PACK (POSTS): units of water
#define NEEDS_PACK_FOOD             (3 * CALENDAR_DAY_TICKS)    //...and food, by the health it gives: a provision's worth
//A SCOUT's pack (GATHERING): as much as he carries - at the pack's balance, so both run out together: 2 days a unit
//of water, and the bars' own reserve (2.7 days of water before it presses, 5.25 of food) - about 14.5 days out.
#define NEEDS_SCOUT_WATER           6
#define NEEDS_SCOUT_FOOD            (9 * CALENDAR_DAY_TICKS)    //three provisions' worth
#define ECONOMY_GATHER_STOPS        2                           //places he goes to fill it: a second for what the first lacks
//The health a unit of food gives - beans most, then wheat, greens least (the user's order; the amounts are
//tuning); 0 for no food.
int EconomyFoodWorth(int good);
//What a person died of. Never renumber: saves hold these.
#define NEEDS_DIED_HUNGER           0
#define NEEDS_DIED_THIRST           1       //dry when his health ran out
#define NEEDS_DIED_COLD             2       //cold (and not dry) when it did
#define NEEDS_DIED_ZOMBIE           3       //a zombie's bite took the last of it (zombies_plan.md, step 2)
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
#define WORKER_JOB_PICKER       7       //a camp's own: fruit from the wild (foraging_plan.md)
#define WORKER_JOB_STONECUTTER  8       //at a stonecutter's hut: breaks rocks for stone (stone_plan.md)
#define WORKER_JOB_MINER        9       //at a mine: digs its deposit, stone or floatstone (stone_plan.md step 5)
#define WORKER_JOB_COUNT        10
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
#define WORKER_HOLDING          6       //carrying, cut off by a re-plan: at his next look a store, or put down (crates step 5)
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
    the eldest who can pair with them. So a new house is a new couple, and a new couple brings children. One who
    lives alone in a house that holds two takes the other in there instead (the user's 2026-10-11).
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
    int timer_full = 0;         //...and what it started at (EconomyStartTimer): his card's task bar, never a rule
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
    vec2 off;                   //SPACING: where his body stands off that, pushed by those near him (EconomyBody)
    bool f_indoors = false;     //in his house (or tent): arrived home, and not set out since
    int health = NEEDS_HEALTH_FULL;     //ticks of life left (NEEDS, above)
    int water = NEEDS_WATER_FULL;       //ticks until he is dry
    int meal_good = -1;                 //MEALS (above): what he is eating or drinking, -1 nothing...
    int meal_ticks = 0;                 //...ticks of it still to go...
    int meal_worth = 0;                 //...and what it has still to give, health or water
    int swing = 0;                      //A FIGHT (ZOMBIES): ticks until his next blow, while a zombie is within reach
    int draw = 0;                       //ARROWS (ZOMBIES): ticks into his turn of the Shoot clip while he shoots, 0 not...
    uint32_t aim = 0;                   //...and the zombie he shoots at, 0 none
    int post = -1;                      //his POST (above): the plot he was sent to stand on, -1 none
    int post_layer = -1;                //...and its layer he was sent to (ramparts_plan.md): -1 its top, 0 its ground
    std::array<int,GOOD_COUNT> pack = {};   //his PACK (POSTS): water and food he carries for himself, by good
    int gather = 0;                     //GATHERING (POSTS): places still to go to for a scout's pack first, 0 none
    int tent = 0;                       //TAKING A TENT (SETTLEMENTS): ECONOMY_TENT_*
    int forage = FORAGE_NONE;           //FORAGING by hand: what he is to do at his post - its units on `timer`
    int archery = 0;                    //ARCHERY (TRAINING, above): experience, of ARCHERY_FULL - kept when he stops
    bool f_chosen = false;              //a guard posted BY HAND (POSTS, step 8): kept ahead of the auto-fill
    uint32_t with = 0;                  //WITH THE GROWN: who a child goes along with, or a baby is carried by, 0 none
                                        //(Economy::WithTheGrown) - a baby's kept until its mother is home again
};

//Starts a timed part of his round: `timer` counts down from `ticks`, and `timer_full` keeps where it began, so his
//card can show how far he has got (selection_plan.md, "How far along"). Every timer is started through this.
inline void EconomyStartTimer(EconomyWorker& k, int ticks){
    k.timer = ticks;
    k.timer_full = ticks;
}
//How far through his timed task he is, 0..1 - or -1 when nothing he is doing runs on the timer. Not while eating and
//drinking (WORKER_SUPPLYING): the meal has its own bar, and that state's timer only paces the waiting.
float EconomyTaskProgress(const EconomyWorker& k);

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
#define CRATE_CAMP              2       //what a camp held, left where it stood when its last tent went (settlements_plan.md)
#define CRATE_RUBBLE            3       //stone fallen from the chasm's walls, laid with a new colony (stone_plan.md)
#define CRATE_BROKEN            4       //...and a rock's stone, left where a stonecutter broke it - rubble too
#define CRATE_SITE              5       //what a building site was brought, left where it stood when it was taken off
inline bool EconomyCrateRubble(int origin){ return origin == CRATE_RUBBLE || origin == CRATE_BROKEN; }
#define ECONOMY_CRATE_RINGS     6       //rings of plots round him searched for open ground to put a load down on: past a camp's tents
/*
    RUBBLE (stone_plan.md, current tuning): at the foot of a balcony's wall only, never on the rim (the user's,
    2026-10-15) - the share of its plots with a pile and the stone in one; on a floatstone SEAM's foot (Deposits.h)
    every plot has one, of the seam's crystal instead; and how far a pile lies toward its wall, as a share of the way.
*/
#define RUBBLE_WALL_CHANCE      0.33f
#define RUBBLE_WALL_STONE_MIN   3
#define RUBBLE_WALL_STONE_MAX   6
#define RUBBLE_FLOATSTONE_MIN   1
#define RUBBLE_FLOATSTONE_MAX   3
#define RUBBLE_TOWARD           0.3f
#define ECONOMY_CRATE_STAND     0.5f    //a crate is taken from beside it: this far short of it, on the side he comes from
//PICK UP's reach: the walk there and on to the store, as a share of a full water bar (NEEDS_WATER_FULL) - a wide margin
//for the water he set out with. Past it, nobody goes: one sent days out died of thirst on the way back (stone_plan.md).
#define ECONOMY_PICK_UP_WATER   0.5f

//A CRATE (above): what lies where, and where it came from.
struct EconomyCrate{
    uint32_t id = 0;            //from 1, never reused
    int plot = -1;              //the plot it lies on
    vec2 pos;                   //where on it (x, world z)
    std::array<int,GOOD_COUNT> stock = {};
    int origin = CRATE_DROPPED;
    uint32_t by = 0;            //who put it down, a person's id - 0 for a find
    uint64_t tick = 0;          //the calendar tick it was put down or placed
    bool f_pick_up = false;     //PICK UP: the builders fetch it before building, from anywhere (stone_plan.md)
    //...and turned down at a builder's last look: too far to walk without water (ECONOMY_PICK_UP_WATER). For the card
    //only - never saved or hashed, nothing in the rules reads it - so after a load it waits for the next look.
    bool f_too_far = false;
};
//Rubble holding floatstone: a seam's crystal fallen at its foot, drawn and named as crystal, not stone.
inline bool EconomyCrateCrystal(const EconomyCrate& c){
    return EconomyCrateRubble(c.origin) && c.stock[GOOD_FLOATSTONE] > 0;
}

/*
    AN EVENT (sound_plan.md, "Game events: one record, many readers"; statistics_plan.md): a moment in the colony,
    made ONCE by the rules at the moment it happens (Economy::Event), so a replay makes the same ones on the same
    ticks. Everything that tells of it reads this one record - the notice line, the statistics' journal, and the
    sound to come - so none of them can disagree with another.

    The last ECONOMY_EVENTS_KEPT are kept for TELLING, with a running count, never saved or hashed: after a load
    nothing old is told again, and telling changes nothing in the colony. A reader keeps its own count of what it
    has seen. What has to outlive a load is the HISTORY (below), which keeps the kinds it needs as they are made.
    Never renumber: the journal saves these.
*/
#define EVENT_NOTHING_TO_TAKE   0       //told to gather, and no food or water to be had anywhere he can get to
#define EVENT_RAN_OUT           1       //an ordered man's pack out of water or food - `what` says which - or both
#define EVENT_NO_TENT           2       //told to take a tent, and no camp he can get to has one to spare
#define EVENT_SEAM_SEEN         3       //the first to see a floatstone seam in a balcony's wall (stone_plan.md step 4)
#define EVENT_BORN              4       //`person` born, at `building` - his family's home
#define EVENT_DIED              5       //`person` died; `amount` the cause (NEEDS_DIED_*)
#define EVENT_TREE_FELLED       6       //`person` felled a tree, prop `amount`
#define EVENT_ROCK_BROKEN       7       //`person` broke a rock, prop `amount` (stone_plan.md)
#define EVENT_STOREY_RAISED     8       //a storey of `building` stands on `plot`; `amount` its kind
#define EVENT_BUILDING_FINISHED 9       //`building` stands whole for the first time - a field's cells all built; `amount` its kind
#define EVENT_HARVEST           10      //field `building` harvested by `person`: `amount` of the good in `what`
#define EVENT_SETTLED           11      //a settlement founded, its camp `building`: a tent pitched outside every one by
                                        //`person`, or a new colony's settlers landing (person 0, `amount` of them)
#define EVENT_DISCOVERED        12      //`person` the first to see an area (areas_plan.md): `amount` the world's area,
                                        //`pos` where he saw it, `what` 1 when it is the first of its kind
#define EVENT_FROST_TOOK        13      //winter took field `building`'s crop, `amount` (ZONE_CROP_*), still growing
#define EVENT_GREW_UP           14      //`person` a young adult: a child no more, and at work from now
#define EVENT_EXPECTING         15      //`person` expecting a child, by her partner, in house `building`
#define EVENT_BOULDER_FOUND     16      //`person` near the first boulder seen on the colony's land, on `plot` (demo_plan.md)
#define EVENT_BALCONY_REACHED   17      //`person` the first on a balcony, lowered by a winch, on `plot` (demo_plan.md)
#define EVENT_GOAL_MET          18      //the demo's goal `amount` (CHASM_GOAL_*) met - by `person`, if one made it so
#define EVENT_GOAL_FAILED       19      //...or failed: `person` the death that failed it
#define EVENT_DEMO_COMPLETE     20      //both of the demo's goals decided; `amount` how many were met
#define EVENT_COLONY_LOST       21      //nobody left alive: `person` the last to die
#define EVENT_COUNT             22
//The demo's goals (demo_plan.md step 7), EVENT_GOAL_*'s `amount`: through the first winter with nobody dead, and the
//chasm reached - a winch built and someone lowered by it. Worked out by the app (ApplicationChasmGoals.cpp).
#define CHASM_GOAL_WINTER       0
#define CHASM_GOAL_CHASM        1
#define CHASM_GOALS             2
#define ECONOMY_EVENTS_KEPT     64      //the last few, for each reader to find the new ones among
struct EconomyEvent{
    uint64_t tick = 0;          //the calendar's
    int kind = EVENT_NOTHING_TO_TAKE;
    uint32_t person = 0;        //who, 0 nobody
    uint32_t building = 0;      //which, 0 none
    int plot = -1;              //where, -1 nowhere in particular: for a sound's place, and the view's glide to it
    vec2 pos;                   //...and where on it (x, world z)
    GoodsMask what = 0;         //EVENT_RAN_OUT: GOOD_BIT(GOOD_WATER), GOODS_EATEN, or both; EVENT_HARVEST its good
    int amount = 0;             //what the kind says
};
const char* EconomyEventName(int kind);    //"born", "tree_felled": the signal a cue table sees, and the tools' name
int EconomyEventByName(const std::string& name);    //...and back, -1 for none

/*
    THE HISTORY (statistics_plan.md): what the colony did, kept by the rules - made in the tick, so a replay makes
    the same one, and SAVED, so a load shows the same statistics as were saved. Two parts:
      - THE JOURNAL: every event of the kinds worth remembering one by one (EconomyJournaled), in the order made -
        written by Economy::Event itself, so the journal and the telling cannot disagree;
      - THE LEDGER: a row a day. The levels at its end - the people by stage and every good the colony has, wherever
        it lies (EconomyDayLevels) - and what happened too often to remember one by one, counted: trees
        felled, rocks broken, storeys raised. A day closes on the first tick of the next, before anything of that
        tick has happened, so a row's people are its births and deaths on from the row before, exactly.
    Totals are worked out from these when shown, never kept beside them, so they cannot disagree either. Nothing
    is folded away: a year is CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON rows. Hashed as a trace part of its own
    ("history"), its tail only - it is only ever added to.
*/
#define ECONOMY_STAGES          4       //LIFE_BABY .. LIFE_ADULT: a ledger row's people
struct EconomyDay{
    int day = 0;                                //the calendar's day (tick / CALENDAR_DAY_TICKS)
    std::array<int,ECONOMY_STAGES> people = {}; //at its end, by stage
    std::array<int,GOOD_COUNT> held = {};       //...and all of each good the colony has
    int felled = 0;                             //over it: EVENT_TREE_FELLED
    int broken = 0;                             //EVENT_ROCK_BROKEN
    int storeys = 0;                            //EVENT_STOREY_RAISED
};
struct EconomyHistory{
    /*
        Where it begins: a new colony's first tick, or the load of a save from before the history - which has none,
        so it starts there. `start` holds the levels then (its counts 0); `felled_from` and `broken_from` the trees
        down and rocks broken by then, which is what lets the check count the felled against the forest.
    */
    uint64_t from = 0;
    EconomyDay start;
    int felled_from = 0;
    int broken_from = 0;
    std::vector<EconomyEvent> journal;          //in the order made
    std::vector<EconomyDay> days;               //THE LEDGER, a row a day closed, oldest first
    EconomyDay today;                           //counting since the day began; its levels filled as it closes
};
//The kinds the journal keeps, and the kinds a ledger row counts.
bool EconomyJournaled(int kind);
bool EconomyCounted(int kind);
//The levels a ledger row holds - the people by stage at `tick`, every good the colony has: gathered, not what lies
//out there as rubble or a find - from the state as it stands.
struct EconomyState;
void EconomyDayLevels(const EconomyState& e, uint64_t tick, EconomyDay& row);
//The props down, as the forest's state stands: the rocks broken, and the rest felled - a log or a stump.
void EconomyForestCleared(const EconomyState& e, int& felled, int& broken);

/*
    ZOMBIES (docs/zombies_plan.md, step 1): dropped by the debug tools for now. A zombie goes for PEOPLE ONLY: the
    nearest out of doors and on the ground, by the straight line, the nearest few tried until one can be walked to -
    looked for again every ZOMBIE_LOOK_TICKS, each zombie on its own tick by its id. It walks as a person does,
    straight where clear and else by the grid as WALK_WHO_ZOMBIE (Walkers.h), at ZOMBIE_PACE of his speed, and stops
    ZOMBIE_REACH short of him. Biting is step 2. Tuning, like the needs.
*/
#define ZOMBIE_LOOK_TICKS       25      //a look for its target every half second
#define ZOMBIE_PACE             0.6f    //of a person's speed on the same ground: anyone outwalks it
#define ZOMBIE_REACH            0.6f    //world units short of its target it stops: an arm's length
#define ZOMBIE_REPLAN           0.4f    //...and after him again once he is this far from where its walk ends: in reach
#define ZOMBIE_TRIES            4       //the nearest this many people tried for a way to, at a look
#define ZOMBIE_LOST_LOOKS       4       //...and with none of them to be got to, a look only one time in this many
#define ZOMBIE_SPAWN_SPREAD     0.25f   //several dropped on one plot: this far apart, outward from its point
#define ZOMBIE_SPAWN_MOST       40      //the most one command drops: a wave is tens
/*
    A FIGHT (zombies_plan.md, step 2; the user's 2026-10-14): both sides hit. A zombie within FIGHT_REACH of whom it is
    after BITES him every ZOMBIE_BITE_SECONDS for ZOMBIE_BITE of his health; a grown-up with one within that reach HITS
    the nearest every FIGHT_SWING_SECONDS for FIGHT_HIT of its ZOMBIE_HEALTH - both by his FIGHT PACE (EconomyFightPace),
    his strength and ingenuity together: harder and more often. Each side's first blow a whole interval after it is in
    reach; all reckoned from where everyone stood as the tick began. So an average person at full health kills one
    in 3 s, a day of health left - about the user's 3 seconds for a fight at full health. Tuning, like the rest.
*/
#define ZOMBIE_HEALTH           60      //a zombie's hit points
#define ZOMBIE_BITE             CALENDAR_DAY_TICKS     //health a bite takes: a day's
#define ZOMBIE_BITE_SECONDS     0.5f
#define FIGHT_REACH             1.0f    //body to body: as far as a bite and a blow reach
#define FIGHT_HIT               10      //hit points a blow takes, at an average pace
#define FIGHT_SWING_SECONDS     0.5f    //...and how often he strikes, at it
/*
    ARROWS (zombies_plan.md, step 3; the user's 2026-10-14): anyone with a bow - an archer or a guard - who is grown and
    STANDS, on his post (a tower's platform among them) or on the range's line, shoots at the nearest zombie he can SEE:
    within his sight's reach and over the ground between (ExplorationSight::Clear), from his eye - on a platform, from its
    height, so he sees and shoots further. Not at one the arrows already in the air will fell. A shot is a turn of the
    Shoot clip, ARROW_SHOT_SECONDS, its arrow leaving the bow ARROW_RELEASE into it at the one he aims at then; a trainee
    stops training while he shoots. Where it goes is a scatter each way, ARROW_SPREAD_* a unit of distance by his
    archery, from a hash of the tick, him and the zombie - inside its body it HITS, for ARROW_HIT, once it has flown
    there at ARROW_SPEED, following it as it walks. A hit stays stuck in the zombie until it falls; a miss lies in the
    ground ARROW_LIE_SECONDS. With a zombie within FIGHT_REACH of him he fights it by hand instead (A FIGHT). Arrows
    without end, for now. Tuning, like the rest.
*/
#define ARROW_SHOT_SECONDS      5.0f    //a turn of the Shoot clip: a shot (the user's)
#define ARROW_RELEASE           0.78f   //...its arrow leaving the bow this far into it - the clip's beat (SHOOT_RELEASE)
#define ARROW_SPEED             16.0f   //world units a second in the air
#define ARROW_HIT               20      //of a zombie's ZOMBIE_HEALTH: three fell one
#define ARROW_SPREAD_NOVICE     0.03f   //the scatter, one standard deviation each way, a unit of distance: no archery...
#define ARROW_SPREAD_MASTER     0.008f  //...and all of it
#define ARROW_BODY_HALF_WIDTH   0.22f   //a zombie's body, as an arrow hits it: this far either side of its middle...
#define ARROW_BODY_HALF_HEIGHT  0.4f    //...and above and below its chest
#define ARROW_CHEST             0.55f   //its chest over its ground: what he aims at
#define ARROW_BOW               0.65f   //his bow over his own: where an arrow leaves from
#define ARROW_MISS_ON           1.43f   //a miss comes down this far past the zombie for every unit it passed over the ground
#define ARROW_LIE_SECONDS       10.0f   //...and lies there this long

struct EconomyZombie{
    uint32_t id = 0;            //from 1, never reused
    uint32_t target = 0;        //the person it is after, 0 none
    /*
        LOST: at its last look it could get to none of the nearest. A way that is not there costs A* the whole of the
        ground it can reach to find out, so it looks again only one time in ZOMBIE_LOST_LOOKS - as someone cut off
        from home tries every fourth look.
    */
    bool f_lost = false;
    int health = ZOMBIE_HEALTH; //A FIGHT (below): its hit points, gone at 0...
    int bite = 0;               //...and ticks until its next bite, while whom it is after is within reach
    vec2 pos;
    vec2 off;                   //SPACING: where its body stands off that, pushed by the zombies near it (EconomyBody)
    //Its route, as a person's (EconomyWorker): route[seg] to route[seg + 1], `along` down that leg - all on the ground.
    std::vector<vec2> route;
    std::vector<float> speed;
    int seg = 0;
    float along = 0.0f;
};
//Which way a zombie faces: along its route, or the way its last leg went.
vec2 EconomyZombieFacing(const EconomyZombie& m);
/*
    Whom a zombie can get at: anyone out of doors and on the ground - not someone indoors, nor a baby in its mother's
    arms (with her, and she is the one out there), nor a guard up on his tower's platform, which it cannot climb.
*/
bool EconomyReachable(const EconomyWorker& k, uint64_t tick);

//An ARROW (above), in the air or come down: who loosed it at what, and where it goes.
struct EconomyArrow{
    uint32_t by = 0;            //who loosed it, a person's id
    uint32_t zombie = 0;        //at which zombie - gone once that falls
    bool f_hit = false;         //...and whether it hits it: decided as it leaves the bow
    vec3 from;                  //where it left the bow (x, height, world z)
    vec3 to;                    //where it goes: a hit's place on its zombie, followed as it walks; a miss's in the ground
    vec2 at;                    //a hit: where in the zombie - across and up from its chest, as it came at it
    int ticks = 0;              //its flight, all of it
    int flown = 0;              //...so far: all of it, and it has come down
    int lie = 0;                //come down in the ground: ticks before it is gone (stuck in its zombie: until that falls)
};
//In the air still.
inline bool EconomyArrowFlying(const EconomyArrow& a){
    return a.flown < a.ticks;
}

/*
    SPACING (zombies_plan.md, "Step 1b"): THE ROUTE IS THE PLAN, THE OFFSET IS THE BODY. A person or a zombie walks his
    route as ever - `pos` is where the route has him, and everything that plans or keeps count reads it - and his BODY
    stands `off` from it: what is drawn and picked, and what will reach and be reached. Once a tick, after everyone has
    moved, each body is pushed away from those of its own kind within SPACING_RADIUS - people from people, zombies from
    zombies, so a zombie is never pushed off whom it reaches for - harder the nearer they are, all pushes summed from
    where everyone stood before any moved. And a zombie is kept out of a person's own room, SPACING_BODY - never the
    other way, and well inside its reach:
      - one walking is pushed only ACROSS his way, so a group on one path walks side by side, and he leans to his
        RIGHT for one ahead of him - within SPACING_AHEAD when that one comes at him or stands in his way, so two
        meeting on a road begin to pass as traffic does before they meet;
      - one standing is pushed any way;
      - with nobody near, the offset drifts back toward the route, SPACING_RETURN of it a second;
      - it is at most SPACING_MOST, and never where its route could not have taken him - the water, a wall, a
        building, a field, over a fence or a cliff (EconomyStraightClear from `pos`): pulled back halfway, or wholly.
    Someone indoors has none. A baby carried, and anyone whose route leaves the ground, take no part: theirs settles
    back at SPACING_SETTLE a second. Tuning, like the rest.
*/
#define SPACING_RADIUS          0.6f    //bodies nearer than this push apart: a figure is about 0.5 across
#define SPACING_BODY            0.4f    //...and a zombie nearer a person than this is pushed off him: not inside him
#define SPACING_PUSH            2.0f    //world units a second the offset moves by, two bodies on one spot
#define SPACING_KEEP_RIGHT      0.5f    //...and of that, to his right for a neighbour straight ahead of a walker
#define SPACING_AHEAD           1.5f    //...looking this far for one coming at him or standing in his way
#define SPACING_RETURN          0.6f    //of the offset, a second, it drifts back toward the route
#define SPACING_SETTLE          4.0f    //...and, taking no part, settles back at
#define SPACING_MOST            0.45f   //the furthest a body stands off its route
inline vec2 EconomyBody(const EconomyWorker& k){
    return k.pos + k.off;
}
inline vec2 EconomyBody(const EconomyZombie& m){
    return m.pos + m.off;
}
//A FIGHT: his pace at it - his strength's and his ingenuity's EconomySkillFactor together, 1 for an average person.
float EconomyFightPace(const EconomyWorker& k);
//MEALS: eating or drinking now.
inline bool EconomyMealOn(const EconomyWorker& k){
    return k.meal_ticks > 0;
}

//A SETTLEMENT (Economy::Settle), worked out from the zones.
struct EconomySettlement{
    uint32_t id = 0;            //its oldest home's building id: what it is named by, and its territory's heart
    int beds = 0;               //its homes' beds - tents, and houses' room - that its reach was worked out from
    float reach = 0.0f;         //world units of walking from that home - and its land grows from every building past it
};

struct EconomyState{
    std::shared_ptr<const ChasmWorld> world;
    std::vector<std::array<int,GOOD_COUNT>> stock;     //by building id - a camp's: its SUPPLIES
    std::vector<std::array<int,GOOD_COUNT>> pantry;    //by building id: a camp's PANTRY, its families' own (STORES)
    std::vector<uint8_t> prop_state;                   //by prop index (ForestData::props)
    std::vector<int> dug;                               //THE MINE: by deposit (Deposits.h), what has been dug out of it
    std::vector<uint8_t> fruit;                         //...a plant's fruit on it (FORAGING), FORAGE_GONE built over; 0 any other prop
    std::vector<uint8_t> regrow;                        //...and an emptied plant's dawns of growing weather since
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
    std::array<int,GOOD_COUNT> kept = {};               //by good: the colony's KEPT number of a made good (STOCK KEPT)
    std::array<int,GOOD_COUNT> ordered = {};            //...and an order's level for the stores, 0 none - cleared once reached
    std::vector<EconomyEvent> events;                   //EVENTS: the last ECONOMY_EVENTS_KEPT, oldest first - not saved
    uint32_t events_made = 0;                           //...and how many since the load: each reader tells the ones it has not
    //...and of each kind: felled trees soon push a scout's notice out of the ring, so a reader that looks seldom (a
    //tool) can still tell how many of a rare kind it missed.
    std::array<uint32_t,EVENT_COUNT> events_by_kind = {};
    EconomyHistory history;                             //THE HISTORY: the journal and the ledger - saved
    std::vector<EconomyZombie> zombies;                 //ZOMBIES, in id order
    uint32_t next_zombie = 1;
    std::vector<EconomyArrow> arrows;                   //ARROWS, in the order loosed
    //Derived from the zones, not saved: what each building takes (0 for none - not a store), and room.
    std::vector<GoodsMask> accepts;
    std::vector<GoodsMask> attached;                      //the goods of the workplaces a store is next to
    std::vector<GoodsMask> keeps;                         //what a building may hold: a store's goods, a house's own
    std::vector<int> room;                              //a store's, and a house's (ECONOMY_HOUSE_ROOM)
    std::vector<int> pile_plot;                         //a woodcutter's woodpile: a lot beside him, -1 none
    std::vector<int> log_wood;                          //what a log taken up by a woodcutter working here is worth, 0 none
    std::vector<int> pile_room;                         //...and the wood its woodpile holds: a hut's, the camp's supplies' (ECONOMY_CAMP_WOODPILE)
    std::vector<int> hut_room;                          //...of a hut's, what it keeps indoors: the rest is on its lots
    std::vector<int> water_room;                        //a collector's: the water it holds uncarried (EconomyCollectorRoom), 0 for any other
    std::vector<int> cutter_places;                     //a camp's own woodcutter's places (ECONOMY_CAMP_WOODCUTTERS), 0 for any other
    std::vector<int> drawer_places;                     //...and its water carrier's (ECONOMY_CAMP_WATER_CARRIERS), 0 for any other
    std::vector<int> draw_plot;                         //...the plot he draws at, beside the water: -1 none, or not a camp
    std::vector<int> picker_places;                     //...and its picker's (ECONOMY_CAMP_PICKERS), 0 for any other (FORAGING)
    std::vector<uint32_t> trees_unreached;              //sites with a tree in their way no woodcutter reaches: a road's, wall's,
                                                        //garden's or lot's plot as ECONOMY_SITE_GROUND | plot, else by id
    std::vector<uint32_t> trees_iced;                   //...of those, the ones a woodcutter reaches over the ice in winter
    std::vector<uint8_t> wood_reach;                    //by plot: how a woodcutter reaches it (ECONOMY_WOOD_*) - the ghost's warning
    std::vector<std::pair<uint32_t,int>> ice_home;      //the posted whose only way home is over the ice, and the days until
                                                        //it thaws on that way (Economy::IceHome) - a card's warning
    std::vector<int> capacity;                          //a house's: how many it holds
    std::vector<int> places;                            //a workplace's: how many it employs (0: not one) - a house's or camp's, its land's farmers
    std::vector<uint32_t> farmer_of;                    //by building id: a field's farmer, the person its share falls to - 0 fallow
    std::vector<int> burn;                              //a house's or camp's firewood a dawn under the snow
    std::vector<int> woodshed;                          //a house's room for firewood
    std::vector<uint8_t> snowed;                        //a house the full winter's snow reaches: it fetches firewood
    std::vector<uint8_t> shut;                          //by building id: SHUT IN, no door (EconomyShutIn)
    std::vector<int> door;                              //by building id: the plot a walk to it ends on, -1 none
    std::vector<uint32_t> settlement;                   //by plot: its SETTLEMENT (its oldest home's id), 0 outside every one
    std::vector<uint32_t> settlement_of;                //by building id: the settlement of its door, 0 none
    std::vector<EconomySettlement> settlements;         //each one, in id order
    int camp_plot = -1;                                 //derived from the world: where the unhoused wait
    uint64_t tick = 0;                                  //the calendar's at its last tick or load: what ages are read at
    uint32_t zones_version = 0xFFFFFFFFu;               //the zones `accepts` was worked out for
    uint32_t felled_version = 0;                        //bumped when a prop's state changes - the view
    uint32_t fruit_version = 0;                         //...and when a plant's fruit does
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
//Where on it he stands: an order's plot's point - or, with several ordered to one plot and layer, a place of his own
//round it by his rank among them (selection_plan.md, "Several people selected"); on a tower, his place on its platform
//by his rank among its guards (ZoneTowerPlace).
vec2 EconomyPostPoint(const EconomyState& e, const EconomyWorker& k);
//How many may stand posted on a plot, each in a place of his own: by its area (ECONOMY_POST_ROOM), at least one.
int EconomyPostRoom(const ChasmWorld& w, int plot);
//A guard tower's full stock of `good` - what it is built with and keeps: so much a guard place (ECONOMY_TOWER_*).
int EconomyTowerStock(const ChasmWorld& w, int plot, int good);
//A guard tower standing whole on `plot` - its platform up: what takes guards and is kept full. Its `places` cannot
//say it, as they are its guard count (step 8), which may be 0.
inline bool EconomyTowerUp(const ZoneState& z, int plot){
    return plot >= 0 && plot < (int)z.storeys.size() && z.KindOf(plot) == ZONE_KIND_TOWER && z.standing[plot] > 0
           && z.standing[plot] == z.storeys[plot];
}
//SETTLEMENTS: the settlement of building `id` - its door's - and a person's, his home's; 0 none.
inline uint32_t EconomySettlementOf(const EconomyState& e, uint32_t id){
    return (id < e.settlement_of.size()) ? e.settlement_of[id] : 0;
}
inline uint32_t EconomyPersonSettlement(const EconomyState& e, const EconomyWorker& k){
    return EconomySettlementOf(e,k.house);
}
//SETTLEMENTS: how far a territory reaches with `beds` beds - one tent's, then more for every bed, up to the most.
inline float EconomySettlementReach(int beds){
    return std::min(ECONOMY_SETTLEMENT_REACH,ECONOMY_SETTLEMENT_FIRST + ECONOMY_SETTLEMENT_PER_BED * (float)std::max(0,beds - 1));
}
//Whether a play command `op` on `index` (ZoneOp: a plot, or a coarse cell for a cell op) keeps inside a settlement:
//a plot - a bridge's near end, a field's cell's middle - in a territory. True for whatever places nothing new.
bool EconomyCommandInTerritory(const EconomyState& e, const ChasmWorld& w, int op, int index);
//ONE CAMP TO A COLONY (settlements_plan.md): the camp of the colony `plot` lies in, 0 none - the one a tent pitched or
//painted there joins, wherever it stands (THE CAMP, Zones.h).
uint32_t EconomyColonyCamp(const EconomyState& e, const ZoneState& z, int plot);
//STOCK KEPT: the made good a building of kind `kind` makes - a builders' hut's provisions, a ropewalk's rope - or -1.
inline int EconomyMadeAt(int kind){
    return (kind == ZONE_KIND_BUILDERS) ? GOOD_PROVISIONS : (kind == ZONE_KIND_ROPEWALK) ? GOOD_ROPE : -1;
}
//STOCK KEPT: what the stores are to hold of made good `good` - its kept number, or an order's level if higher.
inline int EconomyKeptLevel(const EconomyState& e, int good){
    return (good >= 0 && good < GOOD_COUNT) ? std::max(e.kept[good],e.ordered[good]) : 0;
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
    std::vector<std::pair<int,int>> dug;                //THE MINE: deposits dug from - deposit, how much
    std::vector<std::array<int,3>> plants;              //plants not ripe and full (FORAGING): index, fruit, regrow
    std::vector<EconomyWorker> workers;
    std::vector<EconomyField> fields;
    std::vector<std::pair<uint32_t,std::array<int,GOOD_COUNT>>> sites;     //sites with goods brought: building, goods
    std::vector<std::pair<int,int>> ground_sites;       //gardens and lots with wood brought: plot, wood
    std::vector<std::pair<uint32_t,int>> heats;         //homes not HEAT_NONE: building (0 the homeless), heat
    std::vector<EconomyCrate> crates;
    uint32_t next_crate = 1;
    std::vector<std::pair<uint32_t,int>> guards;        //towers' guard counts: building, count
    std::vector<std::pair<int,int>> kept;               //made goods' kept numbers (STOCK KEPT): good, count - none, the start's
    std::vector<std::pair<int,int>> ordered;            //...and their orders' levels: good, level
    uint32_t next_person = 1;
    std::vector<EconomyDeath> dead;
    std::vector<EconomyZombie> zombies;
    uint32_t next_zombie = 1;
    std::vector<EconomyArrow> arrows;
    bool f_before_land = false;         //read from a save older than land (land_plan.md): its fields have no owner
    EconomyHistory history;
    bool f_history = false;             //...a save from before the history has none: it begins at the load
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
    /*
        PROVISIONS (POSTS): how many are still wanted - by sites, towers and the level kept (STOCK KEPT) - beyond
        what is made and being made; below 0, made and not wanted. With `spare`, how many lie made beyond what the
        sites and towers still lack and the stores' share of the level: a hut's to carry to a store. The rules' own
        figure: call it on the simulation thread, between ticks.
    */
    int ProvisionsLacking(const ZoneState& z, int* spare = NULL) const;
    //STOCK KEPT (CHASM_CMD_KEEP): the colony's kept number of made good `good`, 0 or more. False if not a made good.
    bool SetKept(int good, int count);
    /*
        ...or an ORDER for `count` more of it: the level for the stores to reach goes up by `count` from the order's
        running now, or else from what the stores hold or the kept number, the higher. `count` 0 cancels one. Cleared
        on the tick the stores reach it. False if not a made good.
    */
    bool OrderMore(int good, int count, const ZoneState& z);
    /*
        An ORDER (POSTS, CHASM_CMD_ORDER): person `person` sent to stand on `plot`, or released from his post
        with -1 - on its `layer`, the one clicked (ramparts_plan.md), or with -1 its top. Applied between ticks,
        like SetNeeds; what he was doing he drops on the tick that follows. False, and `why` says it, if refused:
        nobody by that id, a child, nothing open to stand on, no way there. `f_spread`, one of a GROUP's orders
        (selection_plan.md, "Several people selected", step 3): onto the plot while it has room (EconomyPostRoom), else
        the nearest open plot round it with room that he can walk to (SpreadPost) - the group's orders come nearest first.
        `clouds`, a PLAY order's (NULL a tool's): sent under them he still goes, but what stops him there is told only as
        no way there - the water or a cliff would give the hidden ground away.
    */
    bool Order(uint32_t person, int plot, const ZoneState& z, Walkers& walkers, std::string* why, int layer = -1,
               bool f_spread = false, const Exploration* clouds = NULL);
    /*
        GATHERING (POSTS, CHASM_CMD_GATHER): person `person`, ordered out, fills a scout's pack before his post - or with
        f_on false goes straight on. He sets out on the tick that follows, as for an order. False, and `why` says it,
        if refused: nobody by that id, not ordered out.
    */
    bool Gather(uint32_t person, bool f_on, const ZoneState& z, Walkers& walkers, std::string* why);
    /*
        TAKING A TENT (SETTLEMENTS, CHASM_CMD_TENT): person `person`, ordered out, takes a tent from the nearest camp
        with one to spare before his post - or with f_on false goes straight on. False, and `why` says it, if refused:
        nobody by that id, not ordered out, a tent on him already.
    */
    bool TakeTent(uint32_t person, bool f_on, const ZoneState& z, Walkers& walkers, std::string* why);
    /*
        PITCHING (SETTLEMENTS, CHASM_CMD_PITCH): where the tent `person` carries goes up - the plot he stands on, or
        the nearest one within ECONOMY_PITCH_RINGS a camp may stand on - and the camp it joins: its colony's (`camp`,
        EconomyColonyCamp), or 0 for a camp of its own.
        False, and `why` says it, with no tent on him or nowhere. The app puts it up (Zones::PitchTent), then:
    */
    bool PitchSpot(uint32_t person, const ZoneState& z, int* plot, uint32_t* camp, std::string* why) const;
    //...he moves in, the zones' change followed at once - his house place given up, a household of his own.
    void Pitched(uint32_t person, int plot, const ZoneState& z, Walkers& walkers);
    /*
        SETTLING (SETTLEMENTS, CHASM_CMD_SETTLE): `person`, ordered onto another settlement's ground and standing inside
        its borders, moves there without a tent - into a camp of it with a bed to spare, an empty house of it, or
        squeezed in - as a household of his own, his order over. False, and `why` says it: not sent to another
        settlement, not there yet.
    */
    bool SettleHere(uint32_t person, const ZoneState& z, Walkers& walkers, std::string* why);
    /*
        FORAGING BY HAND (CHASM_CMD_FORAGE): person `person` sent to pick the ripe plant on `plot` (FORAGE_FRUIT), or to
        the edge of the water on `plot` to draw it (FORAGE_WATER) - an order to stand there (Order), and a one-off once
        he is: into his pack, and he stands on. `f_spread`, one of a group's: the nearest ripe plant to the one clicked
        that none of them is sent to, or the edge's room shared out as a group's posts are. False, and `why` says it.
        `clouds`, a PLAY command's (NULL a tool's): nothing under them - not the plot clicked, nor a plant a group spreads to.
    */
    bool Forage(uint32_t person, int plot, int what, const ZoneState& z, Walkers& walkers, std::string* why,
                bool f_spread = false, const Exploration* clouds = NULL);
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
    //PICK UP (CHASM_CMD_PICK_UP, stone_plan.md): crate `crate` fetched first by the builders, or back in the queue.
    //False, and `why` says it, for no such crate.
    bool SetPickUp(uint32_t crate, bool f_on, std::string* why);
    /*
        ZOMBIES dropped by the debug tools (CHASM_CMD_ZOMBIE, zombies_plan.md): `count` of them on `plot`, spread out
        from its point; `count` 0 takes every zombie off the map. Applied between ticks, like SpawnCrate. False, and
        `why` says it, on ground a zombie may not stand on: the water, the mountain, a building or a wall.
    */
    bool SpawnZombies(int plot, int count, const ZoneState& z, std::string* why);
    //A SEAM SEEN (EVENT_SEAM_SEEN): told by the app's exploration, which is what knows a seam has come into sight.
    void SeamSeen(uint32_t person){ Notice(EVENT_SEAM_SEEN,person,GOOD_BIT(GOOD_FLOATSTONE)); }
    //AN AREA DISCOVERED (EVENT_DISCOVERED): told by the app's exploration too, which is what knows it came into sight.
    void Discovered(uint32_t person, int area, const vec2& at, bool f_first){
        Event(EVENT_DISCOVERED,person,0,-1,at,area,f_first ? 1 : 0);
    }
    //THE FIRST BOULDER on the colony's land, and THE FIRST ON A BALCONY (demo_plan.md, discoveries): the app's
    //exploration looks for both, as it does for seams, until each is in the journal.
    void BoulderFound(uint32_t person, int plot){ Event(EVENT_BOULDER_FOUND,person,0,plot,vec2()); }
    void BalconyReached(uint32_t person, int plot){ Event(EVENT_BALCONY_REACHED,person,0,plot,vec2()); }
    /*
        AN EVENT (above) made by a rule that runs in the app - TickEconomy raising what the builders finished, a tent
        pitched by its command - stamped with `tick`, the calendar tick it belongs to. Into the ring, and the journal
        or today's ledger row as its kind says. Its `pos` is worked out from `plot` when left at the origin.
    */
    void Event(EconomyEvent ev, uint64_t tick);
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
    //The tents packed this tick - the app takes their plots out of their camps (ZONE_OP_BUILD_ERASE).
    const std::vector<int>& TentsPacked() const { return tents_packed; }
    //The standing trees in a planned road's or wall's way, to be felled before it is laid or built. Physics
    //thread - or a tool between ticks (Scene::AtTickBoundary), which is how chasm_economy lists them.
    void RoadTrees(const ZoneState& z, int v, std::vector<int>& out) const;

private:
    EconomyState state;
    std::vector<int> raises;                    //this tick's, in the order built
    std::vector<int> ground_raises;
    std::vector<int> field_raises;
    std::vector<int> crop_defaults;
    std::vector<int> tents_packed;
    /*
        PICK UP's reach, remembered (MoveCrate): a builder's verdict on a crate - too far or not - while nothing it rests
        on has changed: he stands where he did, the zones are as they were, the same store would take it. Otherwise two
        routes are planned at every look, twice a second, for every far crate. Not saved: a miss works it out again to
        the same answer, so a load or a replay is none the wiser. Emptied by Reset, and when it grows past a few dozen.
    */
    struct PickUpLook{
        uint32_t crate = 0;
        uint32_t by = 0;
        uint32_t zones = 0;
        int store = -1;
        vec2 from;
        bool f_far = false;
    };
    std::vector<PickUpLook> pick_up_looks;
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
    //SETTLEMENTS: each plot's walk from its settlement's camp (EconomyState::settlement), and working them out.
    std::vector<float> settlement_walk;
    void Settle(const ZoneState& z, Walkers& walkers);
    //Whether `id` - a building, a crate (ECONOMY_STORE_CRATE), a garden's, lot's or wall's plot (ECONOMY_SITE_GROUND)
    //- is in his settlement: THE LIMIT on what he takes on.
    bool Ours(const EconomyWorker& k, uint32_t id) const;
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
    std::vector<int> rocks;                     //the ROCKS, boulders too (ForestIsRock): prop indices, in order
    std::vector<int> plants;                    //the props that bear fruit (FORAGING), in index order
    void BuildTrees();
    /*
        FORAGING (above): every plant at dawn - the frost taking its fruit, an empty one growing it back; the plants
        built over, gone for good; the fruit on one to be had now; and `want` of it taken off, what was there.
    */
    void FruitAtDawn(uint64_t tick);
    void PlantsCovered(const ZoneState& z);
    int FruitOn(int prop) const;
    int PickFrom(int prop, int want);
    //Plot `plot` his own settlement's land, or none's: where a pressing need may go, and his foraging.
    bool PlotOurs(const EconomyWorker& k, int plot) const;
    //THE CAMP'S PICKER: off to the nearest ripe plant of his settlement within `reach` of `at` that nobody else is
    //after - by the straight line, the first few tried. True if he set off.
    bool GoPick(EconomyWorker& k, const ZoneState& z, Walkers& walkers, const vec2& at, float reach);
    //BY HAND: the plant on his post with the most fruit (-1 none); what he still wants there - fruit his pack has room
    //for, or water; how long a unit of it takes; and a unit of it into his pack.
    int ForagePlant(const EconomyWorker& k) const;
    int ForageWant(const EconomyWorker& k) const;
    int ForageUnitTicks(const EconomyWorker& k) const;
    void ForageUnit(EconomyWorker& k);
    //The dry plot beside the water at wet plot `plot` he draws from - open, the nearest him he can get to; -1 none.
    int BankPlot(const EconomyWorker& k, const ZoneState& z, Walkers& walkers, int plot);
    //What follows from the zones: stocks sized (and a split store's shared out, if f_share), what
    //each store takes and its room, each building's door (where its worker stands); workers and fields
    //kept in step with the buildings.
    void Derive(const ZoneState& z, Walkers& walkers, bool f_share);
    //The zones changed since they were last followed: Derive, then homes, jobs and ways again - Tick's, and a tent's
    //pitching between ticks (Pitched).
    void FollowZones(const ZoneState& z, Walkers& walkers);
    void KeepAtDoors();                         //anyone indoors moved to his house's door, wherever Derive put it
    void Replan(const ZoneState& z, Walkers& walkers);
    void TickWorker(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t calendar_tick, float dt);
    void IceHome(const ZoneState& z, Walkers& walkers);     //state.ice_home, now
    bool RouteTo(EconomyWorker& k, const ZoneState& z, Walkers& walkers, int to_plot, const vec2& to_point,
                 int to_layer = -1);
    bool FindTree(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //THE STONECUTTER: his next stone, from home - a boulder on a site of his settlement's, then rubble lying, then the
    //nearest rock, within reach of his hut and while a store has room; and a rock broken, its stone left as rubble.
    bool FindRock(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void BreakRock(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //The wood a woodcutter could go for near `at`, logs first (f_logs: only logs), and out to the first of it he
    //can reach, of the first `tries`.
    //The standing trees in the way of planned roads and walls of his own within `reach` of `at`, sorted - the first
    //a woodcutter goes for (woodcutting_plan.md).
    void TreesInTheWay(const EconomyWorker& k, const ZoneState& z, const vec2& at, float reach, std::vector<int>& out) const;
    //A tree felled: what waited on it goes up - a wall beside it with its wood all there (RaiseWall), the site it stood
    //on (Build).
    void Felled(const ZoneState& z, int prop);
    //Whether a site still has a tree standing in its way (EconomyTreeSite): it rises only once they are all down.
    bool SiteTreesStanding(const ZoneState& z, uint32_t id) const;
    void WoodNear(const EconomyWorker& k, const ZoneState& z, Walkers& walkers, const vec2& at, float reach, bool f_logs,
                  std::vector<std::pair<std::pair<int,float>,int>>& out) const;
    bool GoForWood(EconomyWorker& k, const ZoneState& z, Walkers& walkers,
                   const std::vector<std::pair<std::pair<int,float>,int>>& found, size_t tries);
    bool FindStore(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void GoHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //POSTS: what he was doing dropped for his post; and the way there - with none, waiting where he is.
    void TakePost(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void GoToPost(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //Everyone ordered to `plot`'s `layer` on his way there or on it, but `except`: off to his place again - one more or
    //one fewer moves them all (EconomyPostPoint).
    void RePlace(int plot, int layer, const EconomyWorker* except, const ZoneState& z, Walkers& walkers);
    //Where one of a group sent to `plot` goes when it is full: the nearest open plot round it, on the same layer, with
    //room and a way there for him. False to stay on `plot` - it has room, or nothing round it does.
    bool SpreadPost(const EconomyWorker& k, int plot, int layer, const ZoneState& z, Walkers& walkers, int* to, int* to_layer);
    //His pack filled from a stock, up to NEEDS_PACK_* - a scout's, gathering, up to NEEDS_SCOUT_*; and, home again,
    //emptied back into his pantry.
    void FillPack(EconomyWorker& k, std::array<int,GOOD_COUNT>& from);
    //GATHERING: off to the nearest place with what his pack lacks of a scout's. False, his gathering over, if it lacks
    //nothing or none is to be had - `f_none` says which.
    bool GatherSupplies(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool* f_none = NULL);
    //TAKING A TENT: the plot of a tent at camp `id` he may pack - one nobody else sleeps in or is on his way for, his
    //own among them - or -1; and off to the nearest camp with one, false if none he can get to has one.
    int TentToPack(const EconomyWorker& k, const ZoneState& z, uint32_t id) const;
    //...a camp's beds to spare for `k` (CampSpare, TentToPack's count); and his house place given up for a household of his own.
    int CampSpare(const ZoneState& z, uint32_t id, const EconomyWorker* k) const;
    void OwnHousehold(EconomyWorker& k);
    //REJOINING: his partner, if they have a home in `settlement` and are not leaving it with a tent - else NULL.
    const EconomyWorker* PartnerLivingIn(const EconomyWorker& k, uint32_t settlement) const;
    //WITH THE GROWN: the grown-up child `k` goes along with, or the mother baby `k` is carried by - 0 neither; the
    //children posted along and the babies carried, at each tick's start; and those with `k`, into his new home.
    uint32_t GoesWith(const EconomyWorker& k) const;
    void WithTheGrown(const ZoneState& z, Walkers& walkers);
    void BringAlong(const EconomyWorker& k, const std::vector<uint32_t>& along, const ZoneState& z, Walkers& walkers);
    std::vector<uint32_t> Along(uint32_t id) const;
    bool GoForTent(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //AN EVENT on the tick being run (above): into the ring, the oldest dropped past ECONOMY_EVENTS_KEPT - and the
    //journal or today's row, as its kind says. A NOTICE is one about a person, told on the notice line.
    void Event(int kind, uint32_t person, uint32_t building, int plot, const vec2& pos, int amount = 0, GoodsMask what = 0);
    void Notice(int kind, uint32_t person, GoodsMask what = 0);
    //THE LEDGER (above): the day before `tick`'s closed into a row, its levels as the state stands, and a new one begun.
    void CloseDay(uint64_t tick);
    //Where a new colony's history begins, or a save's that had none: the levels and the forest's count now.
    void BeginHistory(uint64_t tick);
    void Unpack(EconomyWorker& k, const ZoneState& z);
    //CRATES: his load put down - into a store he stands at that takes it, else as a crate where he stands or on
    //open ground near it; `amount` of `good` into the crate on `plot` (a new one at `at` if none lies there); and
    //the crates whose ground has been built over, lost.
    void DropLoad(EconomyWorker& k, const ZoneState& z);
    int CratePlotNear(const ZoneState& z, int start, const vec2& at) const;
    void AddToCrate(int plot, const vec2& at, int good, int amount, int origin, uint32_t by);
    void LoseCoveredCrates(const ZoneState& z);
    void PlaceRubble(const ZoneState& z);       //RUBBLE, on a new colony's first tick
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
    //A caretaker's last errand: a crate's goods off to a storehouse. True if he set off. `f_picked`: only the crates
    //put PICK UP, from anywhere - his errand before building.
    bool MoveCrate(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_picked = false);
    //The seconds `probe`'s walk takes from where he stands to `to_point` on plot `to_plot`, by the route he would take
    //(RouteTo, so a winch and roads count) - or -1 with no way. `probe` is a copy: its route is thrown away.
    float WalkSeconds(EconomyWorker probe, const ZoneState& z, Walkers& walkers, int to_plot, const vec2& to_point);
    //THE MINE: the deposit mine `id` is on (Deposits.h), -1 none; a miner off to dig at his mine; a deposit dug out.
    int MineDepositAt(uint32_t id) const;
    bool GoDig(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void DugOut(const ZoneState& z, int d);
    bool MineCarrying(uint32_t id) const;      //its miner on his way with a load of it
    //A caretaker's first errand (POSTS, step 7): water or provisions to a guard tower short of them. True if he set off.
    bool KeepTowers(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    //PROVISIONS (POSTS): a builder's errand for them, from home (true if he set off); what he does at his hut.
    bool MakeProvisions(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void AtWorkshop(EconomyWorker& k, const ZoneState& z, Walkers& walkers, float pace);
    //ROPE (goods_plan.md): a ropemaker's errand, from home (true if he set off); what he does at his ropewalk.
    bool MakeRope(EconomyWorker& k, const ZoneState& z, Walkers& walkers);
    void AtRopewalk(EconomyWorker& k, const ZoneState& z, Walkers& walkers, float pace);
    //STOCK KEPT: how many of made good `good` the stores still lack of its level, less what is on its way to one.
    int KeptShort(const ZoneState& z, int good) const;
    void Walk(EconomyWorker& k, float dt);
    //ZOMBIES (above, Zombies.cpp): every zombie a tick on - a look for its target on its tick, then its walk. With
    //`f_zones_moved`, whatever was built or taken down this tick: each one after a target looks again at once.
    void TickZombies(const ZoneState& z, Walkers& walkers, uint64_t tick, float dt, bool f_zones_moved);
    //...its look: after the nearest person it can get to - its way planned again with `f_replan`, even to the one it is
    //after. False, and it stands, with nobody it can.
    bool ZombieLook(EconomyZombie& m, const ZoneState& z, Walkers& walkers, bool f_replan);
    //...and its way to `to` from where it stands, as a person's (RouteTo). False, the route as it was, with none.
    bool ZombieRouteTo(EconomyZombie& m, const ZoneState& z, Walkers& walkers, const vec2& to);
    //SPACING (above, Spacing.cpp): every body's offset a tick on, pushed by its neighbours - after everyone has moved.
    void SpaceOut(const ZoneState& z, float dt);
    //A FIGHT (above, Zombies.cpp): every bite and blow of this tick, from where everyone stands now; the zombies at 0
    //gone, and whom a bite took the last of named in bitten_dead, for Needs to strike off.
    void Fights();
    std::vector<uint32_t> bitten_dead;          //this tick's, cleared as it starts
    //ARROWS (above, Shooting.cpp): the arrows a tick on - landing, and the zombies they fell gone - then every archer's
    //turn of his bow, an arrow loosed at its release. Before the fight, so a zombie shot down bites nobody.
    void Shoot(const ZoneState& z);
    //...whom he would shoot at from where he stands: the nearest zombie he sees that the arrows on their way (`coming`,
    //by zombie, the harm they will do) do not fell - its index, -1 none. His eye's ground in `eye_ground`.
    int ShotTarget(const EconomyWorker& k, const ZoneState& z, const std::vector<int>& coming, float& eye_ground) const;
    //A zombie's body within `within` of his: no meal then (MEALS).
    bool ZombieNear(const EconomyWorker& k, float within) const;
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
    bool CanWalkPosted(const ZoneState& z, Walkers& walkers, const EconomyWorker& k, int to, int layer = -1);
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
    //MEALS: the unit he would eat or drink next from `from` - water first, then the food that keeps worst - -1 none fits;
    //that unit taken and begun, true if one was; and a tick of the meal on.
    int NextMeal(const EconomyWorker& k, const std::array<int,GOOD_COUNT>& from) const;
    bool Consume(EconomyWorker& k, std::array<int,GOOD_COUNT>& from);
    void MealTick(EconomyWorker& k);
    //...at home: a meal under way, or one more his home would give him now - so he stays in for it (TickWorker).
    bool EatingAtHome(const EconomyWorker& k, const ZoneState& z) const;
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

//A prop what is BUILT has covered - a building's standing storey on its plot, a garden or lot built on it, a built field
//cell over it: the view no longer draws it (ApplicationChasm::PropHidden, the same rule less the road's), so nobody
//fells it or waits on it. A SITE covers nothing yet: its trees stand until a woodcutter fells them (EconomyTreeSite).
bool EconomyPropCovered(const ZoneState& z, int plot, int coarse);
//The site a standing tree on `plot` (in coarse cell `coarse`) is in the way of (woodcutting_plan.md: only a woodcutter
//fells, and a site over trees rises once they are down): a building whose storey there is not yet standing, a field's
//cell not yet built, a garden's or lot's plot still a site (ECONOMY_SITE_GROUND | plot). 0 for none.
uint32_t EconomyTreeSite(const ZoneState& z, int plot, int coarse);
//The standing trees on `plot` - or, `coarse` >= 0, in that coarse cell - a site there would wait on: the price tag's
//count. Any thread, on a published state.
int EconomyTreesOn(const ChasmWorld& w, const EconomyState& e, int plot, int coarse);
//...and the BOULDERS still standing there, which only a stonecutter breaks (stone_plan.md).
int EconomyBouldersOn(const ChasmWorld& w, const EconomyState& e, int plot, int coarse);
//FORAGING: the fruit a plant holds ripe - FORAGE_BUSH_FRUIT, FORAGE_TREE_FRUIT - and 0 for a prop that bears none;
int EconomyFruitFull(const PropInstance& p);
//...and what is on prop `prop` to be picked now: 0 for none, a plant gone, or a fruit tree felled. Any thread.
int EconomyFruitOn(const EconomyState& e, int prop);
//...the ripe plant on `plot` with the most fruit, the lower index on a tie: -1 none. Any thread, on a published state.
int EconomyRipePlantOn(const EconomyState& e, int plot);

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
//THE MINE (stone_plan.md step 5): what is left in deposit `d` to dig - INT_MAX for one that never runs out, 0 for none.
int EconomyDepositLeft(const EconomyState& e, int d);
//The skill a job leans on, and how fast a worker with `skill` of it works: 1 at 5, 1.4 at 10, 0.68 at 1.
int EconomyJobSkill(int job);
float EconomySkillFactor(int skill);
//What he can carry on the errand he is on, which his card's load is measured against: a woodcutter two logs of his
//workplace's (ECONOMY_WOODCUTTER_LOGS), the camp's water carrier ECONOMY_CAMP_WATER_LOAD, anyone else ECONOMY_LOAD -
//and never less than he has on him.
int EconomyLoadRoom(const EconomyState& e, const ZoneState& z, const EconomyWorker& k);
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
//...a storey of `kind` on `plot` whatever stands there now: what placing one would cost (the ghost's price tag).
int EconomyKindStoreyCost(const ChasmWorld& w, int kind, int plot, int good);
//...and what a coarse cell of a site painted on cells (a field) takes of `good`, by its kind.
int EconomyCellCost(int kind, int good);
std::array<int,GOOD_COUNT> EconomySiteCost(const ChasmWorld& w, const ZoneState& z, uint32_t id);
int EconomySiteBrought(const EconomyState& e, uint32_t id, int good);
//A garden's or lot's plot: the wood it takes to build, and what has been brought to it.
int EconomyGroundWood(const ChasmWorld& w, int plot);
//What a garden's, lot's or wall's plot takes to build: a wall ECONOMY_WALL_WOOD, the rest by area; a road 0.
int EconomyGroundCost(const ChasmWorld& w, const ZoneState& z, int plot);
//...the same for `ground` painted on `plot`, whatever lies there now; none or a road 0.
int EconomyGroundKindCost(const ChasmWorld& w, int ground, int plot);
//The wood a woodcutter's hut of `floor_area` standing keeps indoors (woodcutting_plan.md): none for the floor he works in.
int EconomyHutRoom(float floor_area);
//...and the water a collector of `floor_area` standing holds: a load, and more for the floor past its pump.
int EconomyCollectorRoom(float floor_area);
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
//Whole days from `tick` until plot `plot` is out from under the snow - its ice gone (Walkers, ICE): 0 not frozen now.
int EconomyIceDays(const ChasmWorld& w, int plot, uint64_t tick);
//What a field is to be sown with NOW, with `ticks_left` of growing weather at `pace`: EconomyFieldSowCrop's
//if it ripens in time - AUTO the most food of those that do - else -1, and it waits for spring.
int EconomyFieldSowNow(const EconomyField& f, const ZoneBuilding& b, float pace, int ticks_left);
//A field's farmer's pace (EconomySkillFactor of his husbandry), 0 with nobody to farm it - from the state alone.
float EconomyFieldPace(const EconomyState& e, uint32_t id);
//A field's growing record, NULL for none.
const EconomyField* EconomyFieldOf(const EconomyState& e, uint32_t id);

#endif

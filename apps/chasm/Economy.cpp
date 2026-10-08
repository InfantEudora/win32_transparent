#include "Economy.h"
#include "Calendar.h"
#include "ChasmWorld.h"
#include "Walkers.h"
#include "RoadMesh.h"
#include <algorithm>
#include <cmath>

const char* GoodName(int good){
    switch (good){
        case GOOD_WOOD:     return "wood";
        case GOOD_WATER:    return "water";
        case GOOD_WHEAT:    return "wheat";
        case GOOD_GREENS:   return "greens";
        case GOOD_BEANS:    return "beans";
        default:            return "?";
    }
}

int GoodByName(const std::string& name){
    for (int g = 0; g < GOOD_COUNT; g++){
        if (name == GoodName(g)){
            return g;
        }
    }
    return -1;
}

const char* EconomyWorkerStateName(int state){
    switch (state){
        case WORKER_AT_HOME:    return "at_home";
        case WORKER_TO_WORK:    return "to_work";
        case WORKER_WORKING:    return "working";
        case WORKER_TO_STORE:   return "to_store";
        case WORKER_UNLOADING:  return "unloading";
        case WORKER_TO_HOME:    return "to_home";
        case WORKER_HOLDING:    return "holding";
        case WORKER_TO_FETCH:   return "to_fetch";
        case WORKER_FETCHING:   return "fetching";
        case WORKER_TO_SITE:    return "to_site";
        case WORKER_BUILDING:   return "building";
        case WORKER_TO_ROAD:    return "to_road";
        case WORKER_CLEARING:   return "clearing";
        case WORKER_LAYING:     return "laying";
        case WORKER_TO_SUPPLY:  return "to_supply";
        case WORKER_SUPPLYING:  return "supplying";
        case WORKER_TO_SOW:     return "to_sow";
        case WORKER_SOWING:     return "sowing";
        case WORKER_TO_GET:     return "to_get";
        case WORKER_GETTING:    return "getting";
        case WORKER_TO_LARDER:  return "to_larder";
        default:                return "?";
    }
}

const char* EconomyJobName(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return "woodcutter";
        case WORKER_JOB_FARMER:     return "farmer";
        case WORKER_JOB_WATER:      return "water_carrier";
        case WORKER_JOB_BUILDER:    return "builder";
        case WORKER_JOB_NONE:       return "none";
        default:                    return "?";
    }
}

const char* SkillName(int skill){
    switch (skill){
        case SKILL_STRENGTH:    return "strength";
        case SKILL_PRECISION:   return "precision";
        case SKILL_HUSBANDRY:   return "husbandry";
        case SKILL_INGENUITY:   return "ingenuity";
        default:                return "?";
    }
}

int EconomyJobOf(int kind){
    switch (kind){
        case ZONE_KIND_WOODCUTTER:  return WORKER_JOB_WOODCUTTER;
        case ZONE_KIND_HOUSE:       return WORKER_JOB_FARMER;      //its land's (land_plan.md): a field employs nobody
        case ZONE_KIND_CAMP:        return WORKER_JOB_FARMER;
        case ZONE_KIND_WATER:       return WORKER_JOB_WATER;
        case ZONE_KIND_BUILDERS:    return WORKER_JOB_BUILDER;
        default:                    return WORKER_JOB_NONE;
    }
}

void EconomyJobCounts(const EconomyState& e, const ZoneState& z, EconomyJobCount out[WORKER_JOB_COUNT], int* jobless){
    for (int j = 0; j < WORKER_JOB_COUNT; j++){
        out[j] = EconomyJobCount();
    }
    size_t n = std::min(z.buildings.size(),e.places.size());
    for (size_t id = 1; id < n; id++){
        int job = EconomyJobOf(z.buildings[id].kind);
        if (job >= 0 && job < WORKER_JOB_COUNT){
            out[job].places += e.places[id];
        }
    }
    int none = 0;
    for (const EconomyWorker& k : e.workers){
        if (k.job >= 0 && k.job < WORKER_JOB_COUNT){
            out[k.job].working++;
        }else{
            none++;
        }
    }
    if (jobless){
        *jobless = none;
    }
}

int EconomyJobSkill(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return SKILL_STRENGTH;
        case WORKER_JOB_FARMER:     return SKILL_HUSBANDRY;
        case WORKER_JOB_WATER:      return SKILL_INGENUITY;
        default:                    return SKILL_STRENGTH;      //a woodcutter, a builder, and anyone carrying
    }
}

float EconomySkillFactor(int skill){
    return 0.6f + 0.08f * (float)std::max(1,std::min(10,skill));
}

/*
    Names by id, from short lists: a first name by the person - a woman's or a man's - and a family name
    by the family, so a family shares its name. Placeholders with the feel of a frontier village;
    nothing in the rules reads them.
*/
std::string EconomyPersonName(const EconomyWorker& k){
    static const char* women[] = {"Ada","Cato","Elin","Fenna","Hanne","Kaat","Lieve","Nora","Roos","Tess",
                                  "Anouk","Femke","Lotte","Mila","Sanne"};
    static const char* men[] = {"Bram","Dirk","Gijs","Ivo","Joris","Maas","Otto","Pim","Sem","Wout",
                                "Bas","Daan","Jesse","Noud","Teun"};
    static const char* family[] = {"Aldering","Brink","Dekker","Hoeve","Kamp","Molen","Rietveld","Smit",
                                   "Ter Horst","Veen","Visser","Wolters"};
    uint32_t a = (k.id * 2654435761u) >> 7;
    uint32_t b = (k.family * 2246822519u) >> 9;
    const char* first = k.f_female ? women[a % (sizeof(women) / sizeof(women[0]))]
                                   : men[a % (sizeof(men) / sizeof(men[0]))];
    return std::string(first) + " " + family[b % (sizeof(family) / sizeof(family[0]))];
}

namespace {

/*
    A crop's growing time, in days of growing weather, and its yield per unit of field area. Greens
    are fast and light, beans slow and rich, wheat between (gameplay_plan.md, "Goods and food") - so
    over a growing year greens come in about four times, wheat twice, beans once or twice. What a unit
    is worth eaten is EconomyFoodWorth's (needs_plan.md).
*/
const int crop_days[ZONE_CROP_COUNT] = {18,8,25};
const float crop_yield[ZONE_CROP_COUNT] = {0.6f,0.35f,0.45f};

/*
    THE ROTATION (fields_plan.md, the user's follows-what table): a crop's yield, in percent, by the crop
    harvested before it. Repeating a crop wears it down; beans feed the soil for whatever follows them.
*/
const int crop_after[ZONE_CROP_COUNT][ZONE_CROP_COUNT] = {
    //sown:  wheat greens beans
    {        60,   100,   100},     //after wheat
    {        100,   70,   100},     //after greens
    {        120,  120,    60},     //after beans
};

//How often a waiting worker looks for something to do, in ticks - staggered by his building's id,
//so they do not all look on the same tick. Half a second: soon enough to look prompt.
#define ECONOMY_LOOK_TICKS  25

//A small integer hash, for the settlers' ages and skills: the same world gives the same colony.
uint32_t Mix(uint32_t a, uint32_t b){
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    h *= 0xC2B2AE3Du;
    h ^= h >> 16;
    return h;
}

/*
    The settlers: three families, all adults - children come with births. Small enough that the first
    houses are a real choice (a family of four does not fit a one-plot, one-storey house).
*/
const int settler_families[] = {4,3,3};

bool IsTree(int kind){
    return kind <= PROP_SNOW_PINE;
}

int SecondsToTicks(float seconds){
    return std::max(1,(int)std::lround(seconds * CALENDAR_TICKS_PER_SECOND));
}

int JobOf(int kind){
    return EconomyJobOf(kind);
}

//The plot a fine quad's corner k is, and a coarse cell's centre: the one vertex its four children share.
int CoarseCentre(const Grid& g, int c){
    for (int k = 0; k < 4; k++){
        int v = g.fine.quads[c * 4].v[k];
        bool f_all = true;
        for (int q = c * 4 + 1; q < c * 4 + 4 && f_all; q++){
            const GridQuad& fq = g.fine.quads[q];
            f_all = (fq.v[0] == v || fq.v[1] == v || fq.v[2] == v || fq.v[3] == v);
        }
        if (f_all){
            return v;
        }
    }
    return g.fine.quads[c * 4].v[0];
}

//Is p under the snow now? Off the map's grid there is nothing to snow on.
bool UnderSnow(const ChasmWorld& w, const vec2& p, uint64_t tick){
    float cover = CalendarSnowCover(tick);
    if (cover <= 0.0f){
        return false;
    }
    float front = CalendarSnowFrontZ(cover,w.grid->bounds_min.y,w.grid->bounds_max.y);
    return CalendarSnowAt(p,front);
}

bool AllInField(const ChasmWorld& w, const ZoneState& z, int v){
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        if (!z.field[w.grid->fine.quads[p.PlotQuadCorner(v,i) / 4].parent]){
            return false;
        }
    }
    return p.PlotQuadCount(v) > 0;
}

int CarryTotal(const std::array<int,GOOD_COUNT>& s){
    int n = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        n += s[g];
    }
    return n;
}

//What a stock holds of water or food (`goods`) in ticks of a person's bar: a unit at what it is worth.
int64_t HeldTicks(const std::array<int,GOOD_COUNT>& s, uint8_t goods){
    int64_t held = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        if (goods & GOOD_BIT(g)){
            held += (int64_t)s[g] * ((g == GOOD_WATER) ? NEEDS_WATER_WORTH : EconomyFoodWorth(g));
        }
    }
    return held;
}

//How many live in a home.
int64_t Living(const std::vector<EconomyWorker>& workers, uint32_t home){
    int64_t n = 0;
    for (const EconomyWorker& o : workers){
        n += (o.house == home) ? 1 : 0;
    }
    return n;
}

//Of `goods`, the one a stock holds most of, the lower on a tie; -1 for none.
int MostOf(const std::array<int,GOOD_COUNT>& s, uint8_t goods){
    int best = -1;
    for (int g = 0; g < GOOD_COUNT; g++){
        if ((goods & GOOD_BIT(g)) && s[g] > 0 && (best < 0 || s[g] > s[best])){
            best = g;
        }
    }
    return best;
}

}

//--- Walking straight ------------------------------------------------------------------------------

bool EconomyStraightClear(const ChasmWorld& w, const ZoneState& z, const vec2& a, const vec2& b,
                          int end_plot_a, int end_plot_b){
    const Terrain& t = *w.terrain;
    const GridPicker& p = *w.picker;
    float len = (b - a).length();
    int steps = std::max(1,(int)std::ceil(len / ECONOMY_STRAIGHT_STEP));
    int ground = end_plot_a;        //a plot the whole way must be the same ground as
    int prev = -1;
    vec2 prev_at = a;
    for (int i = 0; i <= steps; i++){
        vec2 at = a + (b - a) * ((float)i / steps);
        GridPick k = p.Pick(at);
        if (!k.f_hit || k.plot < 0){
            return false;
        }
        int v = k.plot;
        if (v == prev){
            continue;
        }
        if (ground < 0){
            ground = v;
        }
        bool f_end = (v == end_plot_a || v == end_plot_b);
        if (!t.SameGround(v,ground)){
            return false;
        }
        if (!f_end){
            //A camp is open ground between tents, and a standing bridge a deck over the water. A standing
            //palisade is not: the line steps plot to plot along edges, so it cannot slip past one.
            bool f_bridge = ZoneBridgeWalkable(z,v);
            if ((t.wet[v] && !f_bridge) || t.Mountain(v) || (z.storeys[v] > 0 && z.KindOf(v) != ZONE_KIND_CAMP && !f_bridge)
                || AllInField(w,z,v) || ZoneWallStands(z,v)){
                return false;
            }
        }else if (t.wet[v] && !ZoneBridgeWalkable(z,v)){
            return false;       //an end on the wet: only a bridge's
        }
        if (prev >= 0){
            //From one plot to the next: they must be neighbours, with nothing between them, and not
            //too steep - the rules of a walker's edge, met along the line.
            int nb[8];
            int n = ZonePlotNeighbours(w,prev,nb,8);
            bool f_neighbour = false;
            for (int j = 0; j < n; j++){
                f_neighbour = f_neighbour || nb[j] == v;
            }
            if (!f_neighbour){
                return false;
            }
            if (ZoneBoundaryBetween(z,prev,v) != ZONE_BOUNDARY_NONE && !ZoneGateBetween(w,z,prev,v)){
                return false;
            }
            float run = std::max(1e-3f,(w.grid->fine.pos[v] - w.grid->fine.pos[prev]).length());
            if (std::fabs(t.ground[v] - t.ground[prev]) > WALKER_STEEPEST * run){
                return false;
            }
        }
        prev = v;
        prev_at = at;
    }
    (void)prev_at;
    return true;
}

vec2 EconomyWorkerFacing(const EconomyWorker& k){
    int s = std::min(k.seg,(int)k.route.size() - 2);
    if (s >= 0){
        vec2 d = k.route[s + 1] - k.route[s];
        float l = d.length();
        if (l > 1e-4f){
            return d * (1.0f / l);
        }
    }
    return vec2(0.0f,1.0f);
}

//--- The state -------------------------------------------------------------------------------------

void Economy::Reset(std::shared_ptr<const ChasmWorld> world){
    state = EconomyState();
    state.world = world;
    home_plot.clear();
    plots_of.clear();
    if (world && world->forest){
        state.prop_state.assign(world->forest->props.size(),PROP_STATE_STANDING);
    }
    BuildTrees();
    FindCamp();
    MakeSettlers();
    raises.clear();
    ground_raises.clear();
    field_raises.clear();
    share_of.clear();
    field_pace.clear();
    f_resettle = false;
    f_new_colony = true;
}

/*
    THE CAMP's spot (Zones.h, ZoneStartCampPlot): where the settlers stand at the start, and where anyone
    with nowhere to live goes back to. The camp itself is a building the zones place there.
*/
#define ECONOMY_CAMP_REACH      2.0f    //within this of his home plot, he is there - the camp is a ring of people

void Economy::FindCamp(){
    state.camp_plot = state.world ? ZoneStartCampPlot(*state.world) : -1;
}

int EconomySettlerCount(){
    int n = 0;
    for (size_t f = 0; f < sizeof(settler_families) / sizeof(settler_families[0]); f++){
        n += settler_families[f];
    }
    return n;
}

//The colony's first people, standing round the camp. Their ages and skills come from the world's seed.
void Economy::MakeSettlers(){
    state.workers.clear();
    state.next_person = 1;
    if (state.camp_plot < 0){
        return;
    }
    const ChasmWorld& w = *state.world;
    uint32_t seed = w.grid->settings.seed;
    vec2 camp = w.grid->fine.pos[state.camp_plot];
    int n = 0;
    for (size_t f = 0; f < sizeof(settler_families) / sizeof(settler_families[0]); f++){
        for (int m = 0; m < settler_families[f]; m++){
            EconomyWorker k;
            k.id = state.next_person++;
            k.family = (uint32_t)f + 1;
            k.age = 18 + (int)(Mix(seed,k.id * 8 + 0) % 28);
            k.f_female = (Mix(seed,k.id * 8 + 5) & 1) != 0;
            for (int sk = 0; sk < SKILL_COUNT; sk++){
                k.skills[sk] = (uint8_t)(1 + Mix(seed,k.id * 8 + 1 + sk) % 10);
            }
            //In a ring round the camp, a family together.
            float a = (float)n * 0.83f;
            float r = 0.6f + 0.25f * (float)(n % 3);
            k.pos = camp + vec2(std::cos(a),std::sin(a)) * r;
            k.route.assign(1,k.pos);
            state.workers.push_back(k);
            n++;
        }
    }
}

//Can someone standing at `from` walk to plot `to`? The route a worker would take, thrown away.
bool Economy::CanWalk(const ZoneState& z, Walkers& walkers, const vec2& from, int to){
    if (to < 0){
        return false;
    }
    EconomyWorker probe;
    probe.pos = from;
    return RouteTo(probe,z,walkers,to,state.world->grid->fine.pos[to]);
}

//Standing at his home plot (or the camp): where he is "at home", indoors when it is a house.
bool Economy::AtHome(const EconomyWorker& k) const{
    int home = HomePlot(k);
    return home < 0 || (k.pos - state.world->grid->fine.pos[home]).length() < ECONOMY_CAMP_REACH;
}

//Where his round starts and ends: his house's first plot, or the camp while he has none.
int Economy::HomePlot(const EconomyWorker& k) const{
    if (k.house && k.house < home_plot.size() && home_plot[k.house] >= 0){
        //In a camp, his own tent: the camp's plots shared out by his id.
        if (k.house < tents_of.size() && !tents_of[k.house].empty()){
            return tents_of[k.house][k.id % tents_of[k.house].size()];
        }
        return home_plot[k.house];
    }
    return state.camp_plot;
}

/*
    FAMILIES INTO HOUSES. A house that is gone sends its family back to the camp; then each family with
    no house, in family order, takes the empty house of the lowest id that holds all of them, and they
    walk there - now, if they are at the camp, or when their round brings them home.
*/
void Economy::HouseFamilies(const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = z.buildings.size();
    //Only what stands is lived in: a house still a site waits for its builders.
    auto is_house = [&](size_t id){
        return id < n && z.buildings[id].kind == ZONE_KIND_HOUSE && z.buildings[id].size > 0 && stands[id];
    };
    auto is_camp = [&](size_t id){
        return id < n && z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0 && stands[id];
    };
    std::vector<int> living(n,0);
    std::vector<int> family_size;
    std::vector<uint32_t> family_house;
    for (EconomyWorker& k : state.workers){
        //His home is gone: back to the camp's spot, with nowhere to live.
        if (k.house && !is_house(k.house) && !is_camp(k.house)){
            k.house = 0;
            if (k.state == WORKER_AT_HOME){
                GoHome(k,z,walkers);
            }
        }
        if (k.family >= family_size.size()){
            family_size.resize(k.family + 1,0);
            family_house.resize(k.family + 1,0);
        }
        family_size[k.family]++;
        if (k.house){
            living[k.house]++;
            family_house[k.family] = k.house;
        }
    }
    auto move = [&](uint32_t f, uint32_t to){
        if (family_house[f]){
            living[family_house[f]] -= family_size[f];
        }
        living[to] += family_size[f];
        family_house[f] = to;
        for (EconomyWorker& k : state.workers){
            if (k.family == f){
                k.house = to;
                if (k.state == WORKER_AT_HOME){
                    GoHome(k,z,walkers);    //moving in
                }
            }
        }
        state.version++;
    };
    auto where = [&](uint32_t f) -> vec2 {
        for (const EconomyWorker& k : state.workers){
            if (k.family == f){
                return k.pos;       //they keep together
            }
        }
        return vec2(0.0f,0.0f);
    };
    /*
        Into a house: every family without one - with no home, or in a camp - the largest first. Each takes
        the smallest empty house that holds all of them and that they can walk to (the lower id on a tie).
        None: a smaller family living in one that would, and that could move to an empty house that holds
        it, moves there and lets them in - a house grows a storey at a time, so the first family in often
        took it before it was big.
    */
    std::vector<uint32_t> waiting;
    for (uint32_t f = 1; f < family_size.size(); f++){
        if (family_size[f] > 0 && !is_house(family_house[f])){
            waiting.push_back(f);
        }
    }
    std::stable_sort(waiting.begin(),waiting.end(),[&](uint32_t a, uint32_t b){
        return family_size[a] > family_size[b];
    });
    for (uint32_t f : waiting){
        vec2 at = where(f);
        size_t best = 0;
        for (size_t id = 1; id < n; id++){
            if (is_house(id) && living[id] == 0 && state.capacity[id] >= family_size[f]
                && (best == 0 || state.capacity[id] < state.capacity[best]) && CanWalk(z,walkers,at,home_plot[id])){
                best = id;
            }
        }
        if (best == 0){
            for (size_t id = 1; id < n && best == 0; id++){
                if (!is_house(id) || living[id] == 0 || living[id] >= family_size[f] || state.capacity[id] < family_size[f]
                    || !CanWalk(z,walkers,at,home_plot[id])){
                    continue;
                }
                uint32_t lodger = 0;
                for (const EconomyWorker& k : state.workers){
                    if (k.house == id){
                        lodger = k.family;
                        break;
                    }
                }
                size_t elsewhere = 0;
                for (size_t e = 1; e < n; e++){
                    if (is_house(e) && living[e] == 0 && state.capacity[e] >= living[id]
                        && CanWalk(z,walkers,w.grid->fine.pos[home_plot[id]],home_plot[e])
                        && (elsewhere == 0 || state.capacity[e] < state.capacity[elsewhere])){
                        elsewhere = e;
                    }
                }
                if (lodger && elsewhere){
                    move(lodger,(uint32_t)elsewhere);
                    best = id;
                }
            }
        }
        if (best){
            move(f,(uint32_t)best);
        }
    }
    //Still with nowhere: the camp with room for all of them nearest, that they can walk to.
    for (uint32_t f = 1; f < family_size.size(); f++){
        if (family_size[f] == 0 || family_house[f]){
            continue;
        }
        vec2 at = where(f);
        size_t best = 0;
        float best_d = 1e30f;
        for (size_t id = 1; id < n; id++){
            if (is_camp(id) && state.capacity[id] - living[id] >= family_size[f]){
                float d = (w.grid->fine.pos[home_plot[id]] - at).length();
                if (d < best_d && CanWalk(z,walkers,at,home_plot[id])){
                    best_d = d;
                    best = id;
                }
            }
        }
        if (best){
            move(f,(uint32_t)best);
        }
    }
}

/*
    WORKERS INTO WORKPLACES (people_plan.md, builders_plan.md). Every workplace has its PLACES (`places`,
    Derive), filled IN ORDER - the builders' huts first, then every other workplace as it was placed (its id)
    - as far as the people go: with fewer housed adults than places, the newest workplaces stand empty (the
    user's). So a new hut takes its builders from the newest workplaces, and a death costs the newest one its
    worker, who moves to the older place left empty.
    A worker keeps his job while his workplace is there and among those the people reach - the lower person
    ids first where a workplace has fewer places than workers. Anyone else is free again. Then each empty
    place, in the order, takes the free housed adult with the best score: his skill for the job, worth
    ECONOMY_SKILL_DISTANCE world units a point, less the straight line from his house to the workplace - the
    lower id on a tie, and only someone who can walk there.
    A house's or the camp's LAND (land_plan.md) has its place in the order by the house's id, and only its own
    family fills it - for the camp, whoever lives there - so it counts against the people only as far as the
    family reaches. It takes its farmers as the order comes to it: those farming it already, then the free of
    the family, then those working somewhere later in the order - best husbandry first, the lower id on a tie.
    So the order is walked once, every workplace keeping its workers (the land choosing its own) before the
    empty places are filled.
*/
void Economy::AssignJobs(const ZoneState& z, Walkers& walkers){
    SettleLand(z,walkers);
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),state.places.size());
    std::vector<uint32_t> order;
    for (int pass = 0; pass < 2; pass++){
        for (size_t id = 1; id < n; id++){
            if (state.places[id] > 0 && (z.buildings[id].kind == ZONE_KIND_BUILDERS) == (pass == 0)){
                order.push_back((uint32_t)id);
            }
        }
    }
    auto employable = [&](const EconomyWorker& k){
        return k.house && HomePlot(k) >= 0;
    };
    auto is_land = [&](uint32_t id){
        return z.buildings[id].kind == ZONE_KIND_HOUSE || z.buildings[id].kind == ZONE_KIND_CAMP;
    };
    int people = 0;
    for (const EconomyWorker& k : state.workers){
        people += employable(k) ? 1 : 0;
    }
    std::vector<int> reached(n,0);      //of each workplace's places, how many the people reach
    std::vector<int> held(n,0);
    std::vector<uint8_t> kept(state.workers.size(),0);
    for (uint32_t id : order){
        if (!is_land(id)){
            reached[id] = std::min(state.places[id],people);
            people -= reached[id];
            //Its own, as far as its share goes - the lower person ids first.
            for (size_t i = 0; i < state.workers.size() && held[id] < reached[id]; i++){
                const EconomyWorker& k = state.workers[i];
                if (!kept[i] && k.building == id && k.job == JobOf(z.buildings[id].kind) && employable(k)){
                    kept[i] = 1;
                    held[id]++;
                }
            }
            continue;
        }
        //Land: its family's, those already on it first, then the free, then those it takes from a later place.
        std::vector<std::pair<std::pair<int,int>,size_t>> family;     //((rank, -husbandry), index): index order on a tie
        for (size_t i = 0; i < state.workers.size(); i++){
            const EconomyWorker& k = state.workers[i];
            if (kept[i] || k.house != id || !employable(k)){
                continue;
            }
            int rank = (k.job == WORKER_JOB_FARMER && k.building == id) ? 0 : (k.job == WORKER_JOB_NONE) ? 1 : 2;
            family.push_back(std::make_pair(std::make_pair(rank,-(int)k.skills[SKILL_HUSBANDRY]),i));
        }
        std::sort(family.begin(),family.end());
        reached[id] = std::min(state.places[id],std::min(people,(int)family.size()));
        people -= reached[id];
        for (int t = 0; t < reached[id]; t++){
            size_t i = family[t].second;
            EconomyWorker& k = state.workers[i];
            kept[i] = 1;
            held[id]++;
            if (k.job == WORKER_JOB_FARMER && k.building == id){
                continue;
            }
            if (k.job != WORKER_JOB_NONE && (k.state == WORKER_TO_WORK || k.state == WORKER_WORKING
                                             || k.state == WORKER_TO_SOW || k.state == WORKER_SOWING)){
                //Taken from his old workplace on his way to it or at it: home first. A load he already
                //carries he takes on to its store, as anyone does.
                k.prop = -1;
                GoHome(k,z,walkers);
            }
            k.job = WORKER_JOB_FARMER;
            k.building = id;
            state.version++;
        }
    }
    for (size_t i = 0; i < state.workers.size(); i++){
        EconomyWorker& k = state.workers[i];
        if (k.job == WORKER_JOB_NONE || kept[i]){
            continue;
        }
        bool f_there = k.building && k.building < n && employable(k) && z.buildings[k.building].size > 0
                       && JobOf(z.buildings[k.building].kind) == k.job && state.places[k.building] > 0;
        k.job = WORKER_JOB_NONE;
        k.building = 0;
        if (!f_there){
            //His workplace gone: what he was carrying goes with the job, and he goes home.
            k.prop = -1;
            k.carry = 0;
            k.carry_good = -1;
            if (k.state != WORKER_AT_HOME && k.state != WORKER_TO_HOME){
                GoHome(k,z,walkers);
            }
        }else if (k.state == WORKER_TO_WORK || k.state == WORKER_WORKING || k.state == WORKER_TO_SOW || k.state == WORKER_SOWING){
            //Moved by the order, on his way to his old workplace or at it: home. A load he already carries
            //he takes on to its store, as anyone does.
            k.prop = -1;
            GoHome(k,z,walkers);
        }
        state.version++;
    }
    for (uint32_t id : order){
        if (is_land(id)){
            continue;       //its family's alone, chosen above: a place they cannot fill stays empty
        }
        int job = JobOf(z.buildings[id].kind);
        vec2 at = w.grid->fine.pos[home_plot[id]];
        int skill = EconomyJobSkill(job);
        while (held[id] < reached[id]){
            EconomyWorker* best = NULL;
            float best_score = -1e30f;
            for (EconomyWorker& k : state.workers){
                if (k.job != WORKER_JOB_NONE || !employable(k)){
                    continue;
                }
                float score = k.skills[skill] * ECONOMY_SKILL_DISTANCE - (w.grid->fine.pos[HomePlot(k)] - at).length();
                //Only someone who can walk there from his house - a river with no bridge parts a village.
                if (score > best_score && CanWalk(z,walkers,w.grid->fine.pos[HomePlot(k)],home_plot[id])){
                    best_score = score;
                    best = &k;
                }
            }
            if (!best){
                break;      //nobody free can get there: the place stays empty, and he stays free to build
            }
            best->job = job;
            best->building = id;
            held[id]++;
            state.version++;
        }
    }
    Tending();
}

/*
    WHOSE LAND (land_plan.md). Every field that stands gets an owner: the player's choice if that house is lived
    in; else the owner it has, while that is a house lived in - so land does not move to a nearer house built
    later, and nobody's farm changes under them; else the nearest lived-in house it can be walked to, by the
    straight line between their walking points (the lower id on a tie, the first of the nearest few); with
    none, the nearest camp that can. So the camp's land, never kept from a house, passes to one as soon as
    one is lived in. A field still a site, or one nobody can walk to, has no owner. Then the land's places
    (LandPlaces).
*/
void Economy::SettleLand(const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),std::min(stands.size(),home_plot.size()));
    std::vector<uint8_t> lived(n,0);
    for (const EconomyWorker& k : state.workers){
        if (k.house && k.house < n){
            lived[k.house] = 1;
        }
    }
    auto lived_house = [&](uint32_t id){
        return id && id < n && z.buildings[id].kind == ZONE_KIND_HOUSE && z.buildings[id].size > 0 && stands[id]
               && lived[id] && home_plot[id] >= 0;
    };
    for (EconomyField& f : state.fields){
        uint32_t id = f.building;
        uint32_t owner = 0;
        if (id < n && stands[id] && home_plot[id] >= 0){
            if (lived_house(z.buildings[id].land_of)){
                owner = z.buildings[id].land_of;
            }else if (lived_house(f.owner)){
                owner = f.owner;
            }else{
                vec2 at = w.grid->fine.pos[home_plot[id]];
                std::vector<std::pair<float,uint32_t>> houses;
                std::vector<std::pair<float,uint32_t>> camps;
                for (size_t h = 1; h < n; h++){
                    if (home_plot[h] < 0){
                        continue;
                    }
                    float d = (w.grid->fine.pos[home_plot[h]] - at).length();
                    if (lived_house((uint32_t)h)){
                        houses.push_back(std::make_pair(d,(uint32_t)h));
                    }else if (z.buildings[h].kind == ZONE_KIND_CAMP && z.buildings[h].size > 0 && stands[h]){
                        camps.push_back(std::make_pair(d,(uint32_t)h));
                    }
                }
                std::sort(houses.begin(),houses.end());
                std::sort(camps.begin(),camps.end());
                //The way its farmer walks - from the house out to the field's middle. A camp too: land across
                //the chasm from everyone took camp farmers who could never reach it (seen on seed 12). A camp
                //that has it already keeps it unasked, so its land costs no search on every change.
                auto reaches = [&](uint32_t h){
                    return CanWalk(z,walkers,w.grid->fine.pos[home_plot[h]],home_plot[id]);
                };
                for (size_t t = 0; t < houses.size() && t < 4 && !owner; t++){
                    owner = reaches(houses[t].second) ? houses[t].second : 0;
                }
                for (size_t t = 0; t < camps.size() && t < 4 && !owner; t++){
                    owner = (camps[t].second == f.owner || reaches(camps[t].second)) ? camps[t].second : 0;
                }
            }
        }
        if (owner != f.owner){
            f.owner = owner;
            state.version++;
        }
    }
    LandPlaces(z);
}

/*
    The land's PLACES (land_plan.md): each owner's fields shared out in field order, a share taking whole fields
    until it has ECONOMY_FARMER_CELLS cells or more - so five one-cell fields are one farmer's, seven two
    farmers', and one field of ten still one's: a field is never split between farmers. A farmer to a share.
*/
void Economy::LandPlaces(const ZoneState& z){
    size_t n = std::min(z.buildings.size(),state.places.size());
    share_of.assign(z.buildings.size(),-1);
    auto is_land = [&](uint32_t id){
        return id && id < n && z.buildings[id].size > 0
               && (z.buildings[id].kind == ZONE_KIND_HOUSE || z.buildings[id].kind == ZONE_KIND_CAMP);
    };
    for (size_t id = 1; id < n; id++){
        if (is_land((uint32_t)id)){
            state.places[id] = 0;
        }
    }
    std::vector<int> cells(n,0);        //in the owner's last share so far
    for (const EconomyField& f : state.fields){
        uint32_t o = f.owner;
        if (!is_land(o) || f.building >= share_of.size() || f.building >= stands.size() || !stands[f.building]){
            continue;
        }
        if (state.places[o] == 0 || cells[o] >= ECONOMY_FARMER_CELLS){
            state.places[o]++;
            cells[o] = 0;
        }
        cells[o] += (int)cells_of[f.building].size() - site_cells[f.building];
        share_of[f.building] = state.places[o] - 1;
    }
}

/*
    Who farms each share (land_plan.md): the owner's farmers in person-id order, the first the first share's.
    A field grows at its farmer's pace; a share with nobody to farm it LIES FALLOW.
*/
void Economy::Tending(){
    size_t n = share_of.size();
    state.farmer_of.assign(n,0);
    field_pace.assign(n,0.0f);
    std::vector<std::vector<const EconomyWorker*>> farmers(n);
    for (const EconomyWorker& k : state.workers){
        if (k.job == WORKER_JOB_FARMER && k.building < n){
            farmers[k.building].push_back(&k);
        }
    }
    for (const EconomyField& f : state.fields){
        if (f.building >= n || share_of[f.building] < 0 || f.owner >= n){
            continue;
        }
        const std::vector<const EconomyWorker*>& list = farmers[f.owner];
        int s = share_of[f.building];
        if (s < (int)list.size()){
            state.farmer_of[f.building] = list[s]->id;
            field_pace[f.building] = EconomySkillFactor(list[s]->skills[SKILL_HUSBANDRY]);
        }
    }
}

/*
    From his house to his workplace to fetch what it has made - the hut's pile, the collector's water - when a
    store will take a load of it. True if he set off. A farmer's harvest is FetchHarvest's: his land is fields.
*/
bool Economy::FetchFromWorkplace(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (!k.building || home_plot[k.building] < 0){
        return false;
    }
    const std::array<int,GOOD_COUNT>& own = state.stock[k.building];
    int good = -1;
    for (int gd = 0; gd < GOOD_COUNT; gd++){
        if (own[gd] > 0 && (good < 0 || own[gd] > own[good])){
            good = gd;
        }
    }
    int least = (k.job == WORKER_JOB_WATER) ? ECONOMY_LOAD : 1;
    if (good < 0 || own[good] < least){
        return false;
    }
    int load = std::min(k.job == WORKER_JOB_WOODCUTTER ? ECONOMY_WOOD_PER_TREE : ECONOMY_LOAD,own[good]);
    bool f_taken = false;
    for (size_t id = 1; id < state.accepts.size() && !f_taken; id++){
        f_taken = (state.accepts[id] & GOOD_BIT(good)) && state.room[id] - CarryTotal(state.stock[id]) >= load;
    }
    if (!f_taken){
        return false;
    }
    int plot = SourcePlot(k.building,state.world->grid->fine.pos[home_plot[k.building]]);
    if (plot >= 0 && RouteTo(k,z,walkers,plot,state.world->grid->fine.pos[plot])){
        k.prop = -1;
        k.state = WORKER_TO_WORK;
        return true;
    }
    return false;
}

void Economy::BuildTrees(){
    tree_start.clear();
    tree_items.clear();
    tree_nx = tree_nz = 0;
    if (!state.world || !state.world->forest){
        return;
    }
    const Grid& g = *state.world->grid;
    tree_origin = g.bounds_min;
    tree_nx = std::max(1,(int)std::ceil((g.bounds_max.x - g.bounds_min.x) / tree_cell));
    tree_nz = std::max(1,(int)std::ceil((g.bounds_max.y - g.bounds_min.y) / tree_cell));
    const std::vector<PropInstance>& props = state.world->forest->props;
    auto bucket = [&](const PropInstance& pr){
        int bx = std::max(0,std::min(tree_nx - 1,(int)((pr.pos.x - tree_origin.x) / tree_cell)));
        int bz = std::max(0,std::min(tree_nz - 1,(int)((pr.pos.z - tree_origin.y) / tree_cell)));
        return bz * tree_nx + bx;
    };
    tree_start.assign(tree_nx * tree_nz + 1,0);
    for (const PropInstance& pr : props){
        if (IsTree(pr.kind)){
            tree_start[bucket(pr) + 1]++;
        }
    }
    for (size_t i = 1; i < tree_start.size(); i++){
        tree_start[i] += tree_start[i - 1];
    }
    tree_items.resize(tree_start.back());
    std::vector<int> fill(tree_start.begin(),tree_start.end() - 1);
    for (size_t i = 0; i < props.size(); i++){
        if (IsTree(props[i].kind)){
            tree_items[fill[bucket(props[i])]++] = (int)i;
        }
    }
}

/*
    What follows from the zones. Called when they change, and on load (with f_share false: the save's
    stocks are already shared out).
*/
void Economy::Derive(const ZoneState& z, bool f_share){
    const ChasmWorld& w = *state.world;
    const Grid& g = *w.grid;
    const GridPicker& p = *w.picker;
    size_t n = z.buildings.size();
    size_t known = state.stock.size();
    state.stock.resize(n,std::array<int,GOOD_COUNT>{});
    //Every plot building's plots, and a field's first cell.
    plots_of.assign(n,std::vector<int>());
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v]){
            plots_of[z.building[v]].push_back((int)v);
        }
    }
    home_plot.assign(n,-1);
    std::vector<int> first_cell(n,-1);
    cells_of.assign(n,std::vector<int>());
    site_cells.assign(n,0);
    for (size_t c = 0; c < z.field.size(); c++){
        uint32_t id = z.field[c];
        if (!id){
            continue;
        }
        if (first_cell[id] < 0){
            first_cell[id] = (int)c;
        }
        cells_of[id].push_back((int)c);
        site_cells[id] += z.field_built[c] ? 0 : 1;
    }
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].size <= 0){
            continue;
        }
        if (z.buildings[id].kind == ZONE_KIND_FIELD){
            if (first_cell[id] >= 0){
                home_plot[id] = CoarseCentre(g,first_cell[id]);
            }
        }else if (!plots_of[id].empty()){
            home_plot[id] = plots_of[id].front();
        }
    }
    /*
        A store split by an erase: each new piece takes its share of what the store held, by floor
        area, rounded down; the old id keeps the rest. New pieces come in id order, so it is the same
        split every time.
    */
    if (f_share){
        std::vector<float> area(n,0.0f);
        for (size_t id = known; id < n; id++){
            uint32_t from = z.buildings[id].split_from;
            //A store's goods, and a house's own (ECONOMY_HOUSE_KEEPS) - what either holds goes with its floor.
            int kind = z.buildings[id].kind;
            if (from && from < known && (kind == ZONE_KIND_STORE || kind == ZONE_KIND_HOUSE)){
                area[id] = ZoneBuildingFigures(w,z,(uint32_t)id).floor_area;
                if (area[from] == 0.0f){
                    area[from] = ZoneBuildingFigures(w,z,from).floor_area;
                }
            }
        }
        std::vector<float> total(n,0.0f);
        for (size_t id = known; id < n; id++){
            uint32_t from = z.buildings[id].split_from;
            if (area[id] > 0.0f){
                total[from] += area[id];
            }
        }
        std::vector<std::array<int,GOOD_COUNT>> before(n,std::array<int,GOOD_COUNT>{});
        for (size_t id = 1; id < known; id++){
            if (total[id] > 0.0f){
                before[id] = state.stock[id];
                total[id] += area[id];
            }
        }
        for (size_t id = known; id < n; id++){
            uint32_t from = z.buildings[id].split_from;
            if (area[id] > 0.0f && total[from] > 0.0f){
                for (int gd = 0; gd < GOOD_COUNT; gd++){
                    int share = (int)std::floor(before[from][gd] * area[id] / total[from]);
                    state.stock[id][gd] = share;
                    state.stock[from][gd] -= share;
                }
            }
        }
    }
    //Gone buildings hold nothing: a store pulled down loses what it had, a site the wood it was brought.
    state.site.resize(n,0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].size <= 0){
            state.stock[id] = std::array<int,GOOD_COUNT>{};
            state.site[id] = 0;
        }
    }
    /*
        The gardens, lots and walls still to build, and the wood brought to each: a plot that is no longer a
        site - erased, built over, or finished - keeps none (what was left over is spent). A wall's plot is
        one too (line_works_plan.md, step 3), its wood carried once its way is cleared.
    */
    state.ground_site.resize(z.ground.size(),0);
    ground_sites.clear();
    road_sites.clear();
    wall_sites.clear();
    for (size_t v = 0; v < z.ground.size(); v++){
        bool f_wall = ZoneWallPlanned(z,(int)v);
        if ((ZoneEnclosesGround(z.ground[v]) || f_wall) && !z.ground_built[v] && z.storeys[v] == 0){
            ground_sites.push_back((int)v);
            if (f_wall){
                wall_sites.push_back((int)v);
            }
        }else{
            state.ground_site[v] = 0;
            //The roads still to lay (line_works_plan.md): work, not wood, so not among the sites above.
            if (ZoneRoadPlanned(z,(int)v)){
                road_sites.push_back((int)v);
            }
        }
    }
    //What stands of each building, and what is still to build (Zones.h, CONSTRUCTION).
    stands.assign(n,0);
    site_storeys.assign(n,0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind == ZONE_KIND_FIELD){
            //Something of it built: its farmer is taken on (a site has none, fields_plan.md).
            stands[id] = (z.buildings[id].size > 0 && site_cells[id] < (int)cells_of[id].size()) ? 1 : 0;
            continue;
        }
        for (int v : plots_of[id]){
            stands[id] = stands[id] || z.standing[v] > 0;
            site_storeys[id] += z.storeys[v] - z.standing[v];
        }
    }
    /*
        How many each workplace employs (builders_plan.md): one, once it stands. A builders' hut a builder
        per plot-storey standing - and one while nothing of it stands yet, who builds it: with everyone at
        work, nobody else would be free to build the hut that frees them. A house's or camp's are its land's
        farmers, worked out with the fields at the end (LandPlaces).
    */
    state.places.assign(n,0);
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0 || JobOf(b.kind) == WORKER_JOB_NONE || home_plot[id] < 0
            || b.kind == ZONE_KIND_HOUSE || b.kind == ZONE_KIND_CAMP){
            continue;
        }
        if (b.kind == ZONE_KIND_BUILDERS){
            int standing = 0;
            for (int v : plots_of[id]){
                standing += z.standing[v];
            }
            state.places[id] = std::max(1,standing);
        }else{
            state.places[id] = stands[id] ? 1 : 0;
        }
    }
    /*
        What each store takes, and its room. Next to a workplace: that workplace's goods - a woodcutter
        or a collector sharing a fine edge with a store plot, or a field one plot away (buildings keep a
        plot off fields, so beside one is as close as a store gets).
    */
    state.accepts.assign(n,0);
    state.attached.assign(n,0);
    state.keeps.assign(n,0);
    state.room.assign(n,0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_STORE || z.buildings[id].size <= 0 || !stands[id]){
            continue;
        }
        uint8_t attached = 0;
        for (int v : plots_of[id]){
            int nb[8];
            int k = ZonePlotNeighbours(w,v,nb,8);
            for (int j = 0; j < k; j++){
                int kind = z.KindOf(nb[j]);
                if (kind == ZONE_KIND_WOODCUTTER){
                    attached |= GOOD_BIT(GOOD_WOOD);
                }else if (kind == ZONE_KIND_WATER){
                    attached |= GOOD_BIT(GOOD_WATER);
                }
                //A field's crop changes as it is rotated (fields_plan.md): its store takes every food.
                for (int i = 0; i < p.PlotQuadCount(nb[j]); i++){
                    if (z.field[g.fine.quads[p.PlotQuadCorner(nb[j],i) / 4].parent]){
                        attached |= GOODS_FOOD;
                    }
                }
            }
        }
        state.attached[id] = attached;
        state.accepts[id] = attached ? attached : z.buildings[id].allow;
        state.keeps[id] = state.accepts[id];
        state.room[id] = (int)std::floor(ZoneBuildingFigures(w,z,(uint32_t)id).floor_area * ECONOMY_STORE_ROOM);
    }
    /*
        A woodcutter's woodpile: on a lot plot that stands beside his hut, ONE TO A WOODCUTTER - given out
        in building-id order, each taking the lowest plot beside any of his plots that nobody before him
        has, so two huts round one lot never pile on the same spot (the zones refuse a new hut without a
        lot of its own; this settles the rest - an older pair, a lot erased). The view draws the logs on
        the same plot. A woodcutter left without one fells nothing, and his card says so.
    */
    state.pile_plot.assign(n,-1);
    pile_door.assign(n,-1);
    std::vector<uint8_t> lot_used(z.ground.size(),0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_WOODCUTTER || z.buildings[id].size <= 0){
            continue;
        }
        for (int v : plots_of[id]){
            int nb[8];
            int k = ZonePlotNeighbours(w,v,nb,8);
            for (int j = 0; j < k; j++){
                int o = nb[j];
                if (z.ground[o] != ZONE_GROUND_LOT || !z.ground_built[o] || z.storeys[o] > 0 || lot_used[o]){
                    continue;
                }
                if (state.pile_plot[id] < 0 || o < state.pile_plot[id]){
                    state.pile_plot[id] = o;
                }
            }
        }
        if (state.pile_plot[id] >= 0){
            lot_used[state.pile_plot[id]] = 1;
        }
        //The yard's door: of his hut's plots beside the pile's lot, the lowest (plots_of is in index order).
        for (int v : plots_of[id]){
            int nb[8];
            int k = (state.pile_plot[id] >= 0) ? ZonePlotNeighbours(w,v,nb,8) : 0;
            if (std::find(nb,nb + k,state.pile_plot[id]) != nb + k){
                pile_door[id] = v;
                break;
            }
        }
    }
    //How many a house holds: its floor area at ECONOMY_PERSON_AREA each, one family at most this big.
    state.capacity.assign(n,0);
    tents_of.assign(n,std::vector<int>());
    for (size_t id = 1; id < n; id++){
        if (!stands[id]){
            continue;       //nobody lives on a site
        }
        if (z.buildings[id].kind == ZONE_KIND_HOUSE && z.buildings[id].size > 0){
            float area = ZoneBuildingFigures(w,z,(uint32_t)id).floor_area;
            state.capacity[id] = std::max(1,std::min(ECONOMY_HOUSE_MOST,(int)std::floor(area / ECONOMY_PERSON_AREA)));
            //Its family's own food and firewood (P5): smaller than a store's, by the same floor area.
            state.keeps[id] = ECONOMY_HOUSE_KEEPS;
            state.room[id] = (int)std::floor(area * ECONOMY_HOUSE_ROOM);
        }else if (z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0){
            //A camp holds a person a tent, any number of families (Zones.h, THE CAMP).
            for (int v : plots_of[id]){
                if (!ZoneCampFire(v) && z.standing[v] > 0){     //a tent still to be pitched holds nobody
                    tents_of[id].push_back(v);
                }
            }
            state.capacity[id] = (int)tents_of[id].size();
        }
    }
    //A growing record for every field, kept in id order.
    size_t at = 0;
    std::vector<EconomyField> fields;
    for (size_t id = 1; id < n; id++){
        while (at < state.fields.size() && state.fields[at].building < id){
            at++;
        }
        if (z.buildings[id].kind != ZONE_KIND_FIELD || z.buildings[id].size <= 0){
            continue;
        }
        if (at < state.fields.size() && state.fields[at].building == id){
            fields.push_back(state.fields[at]);
        }else{
            EconomyField f;     //empty, waiting to be sown (fields_plan.md)
            f.building = (uint32_t)id;
            //A piece split off by an erase stays its field's owner's land (land_plan.md).
            for (const EconomyField& from : state.fields){
                if (z.buildings[id].split_from && from.building == z.buildings[id].split_from){
                    f.owner = from.owner;
                }
            }
            fields.push_back(f);
        }
    }
    state.fields.swap(fields);
    //The land (land_plan.md): its places by the owners the fields have, and who farms each share.
    LandPlaces(z);
    Tending();
    state.zones_version = z.version;
}

//--- Routes ----------------------------------------------------------------------------------------

/*
    A route from where the worker stands to `to_point`, on or beside plot `to_plot`: straight where the
    line is clear, otherwise the grid's A* from the plot he is on, with a straight step at either end
    between the plot and where he actually is. False, and the route left as it was, if there is no way.
*/
bool Economy::RouteTo(EconomyWorker& k, const ZoneState& z, Walkers& walkers, int to_plot, const vec2& to_point){
    const ChasmWorld& w = *state.world;
    GridPick here = w.picker->Pick(k.pos);
    int from_plot = here.f_hit ? here.plot : -1;
    if (from_plot < 0 || to_plot < 0){
        return false;
    }
    std::vector<vec2> route;
    std::vector<float> speed;
    if (EconomyStraightClear(w,z,k.pos,to_point,from_plot,to_plot)){
        route.push_back(k.pos);
        route.push_back(to_point);
        speed.push_back(ECONOMY_WORKER_SPEED);
    }else{
        std::vector<int> plots;
        std::vector<float> edge_speed;
        if (!walkers.PlanRoute(z,from_plot,to_plot,plots,edge_speed)){
            return false;
        }
        /*
            Part way along an edge, the plot under him can be the one he has just left: a re-plan when the
            zones change - anything placed, anywhere - walked him back to it and turned him round, and every
            placement made the walkers twitch (24 of 29 on grid routes stepped back, 2026-10-07). When the
            path's first plot is behind him - the turn there sharper than square - and the second can be
            walked to straight, he goes on to the second.
        */
        size_t first = 0;
        if (plots.size() >= 2){
            vec2 p0 = w.grid->fine.pos[plots[0]];
            vec2 p1 = w.grid->fine.pos[plots[1]];
            if ((p0 - k.pos).dot(p1 - p0) < 0.0f && EconomyStraightClear(w,z,k.pos,p1,from_plot,plots[1])){
                first = 1;
            }
        }
        route.push_back(k.pos);
        for (size_t i = first; i < plots.size(); i++){
            vec2 pt = w.grid->fine.pos[plots[i]];
            if ((pt - route.back()).length() > 1e-4f){
                //The step from where he stands onto the first plot is open ground; after that, the edges' -
                //and on to the second, the first edge's, which he is on.
                speed.push_back((i == 0) ? ECONOMY_WORKER_SPEED : edge_speed[i - 1]);
                route.push_back(pt);
            }
        }
        if ((to_point - route.back()).length() > 1e-4f){
            speed.push_back(ECONOMY_WORKER_SPEED);
            route.push_back(to_point);
        }
    }
    k.route.swap(route);
    k.speed.swap(speed);
    k.seg = 0;
    k.along = 0.0f;
    k.f_indoors = false;     //setting out
    return true;
}

void Economy::Walk(EconomyWorker& k, float dt){
    float left = dt;
    while (k.seg < (int)k.route.size() - 1 && left > 0.0f){
        vec2 a = k.route[k.seg];
        vec2 b = k.route[k.seg + 1];
        float len = (b - a).length();
        float v = std::max(0.05f,k.speed[k.seg]);
        float need = (len - k.along) / v;
        if (need <= left){
            left -= need;
            k.seg++;
            k.along = 0.0f;
            k.pos = b;
        }else{
            k.along += v * left;
            left = 0.0f;
            k.pos = a + (b - a) * (k.along / std::max(1e-4f,len));
        }
    }
}

static bool Arrived(const EconomyWorker& k){
    return k.seg >= (int)k.route.size() - 1;
}

//Every worker on his way plans again from where he stands - the zones under his route have changed.
void Economy::Replan(const ZoneState& z, Walkers& walkers){
    for (EconomyWorker& k : state.workers){
        //Stranded short of home: perhaps the change opened a way.
        if (k.state == WORKER_AT_HOME && !AtHome(k)){
            GoHome(k,z,walkers);
            continue;
        }
        if (k.state != WORKER_TO_WORK && k.state != WORKER_TO_STORE && k.state != WORKER_TO_HOME
            && k.state != WORKER_TO_FETCH && k.state != WORKER_TO_SITE && k.state != WORKER_TO_ROAD
            && k.state != WORKER_TO_SUPPLY && k.state != WORKER_TO_GET && k.state != WORKER_TO_LARDER
            && k.state != WORKER_TO_SOW){
            continue;
        }
        if (Arrived(k)){
            continue;
        }
        vec2 goal = k.route.back();
        GridPick at = state.world->picker->Pick(goal);
        int goal_plot = at.f_hit ? at.plot : -1;
        if ((k.state == WORKER_TO_WORK || k.state == WORKER_TO_ROAD) && k.prop >= 0){
            goal_plot = state.world->forest->props[k.prop].plot;
        }else if (k.state == WORKER_TO_ROAD){
            goal_plot = EconomyGroundSitePlot(k.site);
        }else if (k.state == WORKER_TO_STORE && k.store < plots_of.size() && !plots_of[k.store].empty()){
            //Re-chosen at arrival if it no longer takes his load; until then, the plot he was making for.
            goal_plot = at.f_hit ? at.plot : plots_of[k.store].front();
        }else if (k.state == WORKER_TO_HOME || k.state == WORKER_TO_LARDER){
            goal_plot = HomePlot(k);
        }
        if (!RouteTo(k,z,walkers,goal_plot,goal)){
            //No way now: stop where he is and think again at the next look.
            k.route.assign(1,k.pos);
            k.speed.clear();
            k.seg = 0;
            k.along = 0.0f;
            k.state = (k.carry > 0) ? WORKER_HOLDING : WORKER_AT_HOME;
            k.prop = -1;
            k.site = 0;         //a carrier cut off from his site gives it up; what he holds goes to a store
            if (k.carry <= 0){
                k.carry_good = -1;  //one fetching home names the kind he was after, holding nothing yet
            }
        }
    }
}

//--- The jobs --------------------------------------------------------------------------------------

/*
    The woodcutter's next tree: the nearest standing tree within reach of his hut, or a felled log lying
    there - nearest by the straight line from the hut, the lower index on a tie - that nobody else is
    after and that is not under a building or a field; then the first of them, in that order, he can
    get to. At most a few are tried, so a hut walled off from its wood does not search the forest
    every look.
*/
bool Economy::FindTree(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    if (!w.forest || tree_nx == 0 || home_plot[k.building] < 0){
        return false;
    }
    const std::vector<PropInstance>& props = w.forest->props;
    vec2 hut = w.grid->fine.pos[home_plot[k.building]];
    std::vector<int> claimed;
    for (const EconomyWorker& o : state.workers){
        if (&o != &k && o.prop >= 0){
            claimed.push_back(o.prop);
        }
    }
    std::vector<std::pair<float,int>> found;
    int r = (int)std::ceil(ECONOMY_WOODCUTTER_REACH / tree_cell);
    int bx = (int)((hut.x - tree_origin.x) / tree_cell);
    int bz = (int)((hut.y - tree_origin.y) / tree_cell);
    for (int z_ = std::max(0,bz - r); z_ <= std::min(tree_nz - 1,bz + r); z_++){
        for (int x_ = std::max(0,bx - r); x_ <= std::min(tree_nx - 1,bx + r); x_++){
            int b = z_ * tree_nx + x_;
            for (int j = tree_start[b]; j < tree_start[b + 1]; j++){
                int i = tree_items[j];
                if (state.prop_state[i] == PROP_STATE_STUMP){
                    continue;
                }
                const PropInstance& pr = props[i];
                float d = (vec2(pr.pos.x,pr.pos.z) - hut).length();
                if (d > ECONOMY_WOODCUTTER_REACH){
                    continue;
                }
                //Hidden under a building or a field: as good as not there.
                if ((pr.plot >= 0 && z.storeys[pr.plot] > 0) || (pr.coarse >= 0 && z.field[pr.coarse])){
                    continue;
                }
                if (std::find(claimed.begin(),claimed.end(),i) != claimed.end()){
                    continue;
                }
                found.push_back(std::make_pair(d,i));
            }
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        int i = found[t].second;
        const PropInstance& pr = props[i];
        vec2 tree(pr.pos.x,pr.pos.z);
        //He stands at the tree's foot, on the side he comes from.
        vec2 to = tree - hut;
        float l = to.length();
        vec2 stand = (l > 0.8f) ? tree - to * (0.6f / l) : tree;
        if (RouteTo(k,z,walkers,pr.plot,stand)){
            k.prop = i;
            k.state = WORKER_TO_WORK;
            return true;
        }
    }
    return false;
}

/*
    The nearest store that takes his load and has room for all of it - by the straight line, the lower
    id on a tie - that he can get to; at it, the plot of it nearest him. A store next to his workplace is
    nearer than any other, which is what makes it his.
*/
bool Economy::FindStore(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    std::vector<std::pair<float,uint32_t>> found;
    for (size_t id = 1; id < state.accepts.size(); id++){
        if (!(state.accepts[id] & GOOD_BIT(k.carry_good))){
            continue;
        }
        if (state.room[id] - CarryTotal(state.stock[id]) < k.carry){
            continue;
        }
        float best = 1e30f;
        for (int v : plots_of[id]){
            best = std::min(best,(w.grid->fine.pos[v] - k.pos).length());
        }
        found.push_back(std::make_pair(best,(uint32_t)id));
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = -1;
        float best = 1e30f;
        for (int v : plots_of[id]){
            float d = (w.grid->fine.pos[v] - k.pos).length();
            if (d < best){
                best = d;
                plot = v;
            }
        }
        if (RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
            k.store = id;
            k.state = WORKER_TO_STORE;
            return true;
        }
    }
    return false;
}

//The way home. In a camp, home is in front of his tent, where he can be seen; in a house, its door.
bool Economy::RouteHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    int home = HomePlot(k);
    bool f_camp = k.house && k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP;
    vec2 at = (home >= 0) ? (f_camp ? ZoneCampDoor(*state.world,home) : state.world->grid->fine.pos[home]) : vec2();
    return home >= 0 && RouteTo(k,z,walkers,home,at);
}

void Economy::GoHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    //Done with what he was doing: a pressing need his home cannot meet comes before going back to it.
    if (SeekSupply(k,z,walkers)){
        return;
    }
    k.prop = -1;
    k.store = 0;
    if (RouteHome(k,z,walkers)){
        k.state = WORKER_TO_HOME;
    }else{
        //No way back: he waits where he is, and is home in the sense that he is free.
        k.route.assign(1,k.pos);
        k.speed.clear();
        k.seg = 0;
        k.along = 0.0f;
        k.state = WORKER_AT_HOME;
    }
}

void Economy::TickWorker(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t tick, float dt){
    bool f_look = ((tick + k.id) % ECONOMY_LOOK_TICKS) == 0;
    std::array<int,GOOD_COUNT>& own = state.stock[k.building];     //his workplace's: stock[0] for none
    float pace = EconomySkillFactor(k.skills[EconomyJobSkill(k.job)]);
    //A woodcutter's woodpile, on the lot beside his hut - reached from its door, the hut plot beside it; -1
    //with no lot standing.
    int pile = (k.job == WORKER_JOB_WOODCUTTER && k.building < pile_door.size()) ? pile_door[k.building] : -1;
    //He has a load: to a store that takes it, or - a woodcutter's wood - to his woodpile. Holding it if neither.
    auto deliver = [&](){
        if (FindStore(k,z,walkers)){
            return;
        }
        if (pile >= 0 && k.carry_good == GOOD_WOOD && own[GOOD_WOOD] + k.carry <= ECONOMY_HUT_PILE){
            if (RouteTo(k,z,walkers,pile,state.world->grid->fine.pos[pile])){
                k.store = 0;
                k.state = WORKER_TO_STORE;
                return;
            }
        }
        k.state = WORKER_HOLDING;
    };
    switch (k.state){
        case WORKER_AT_HOME:
            if (!f_look){
                break;
            }
            /*
                Not home - his way there was cut (a river, a wall), or a re-plan stopped him short of it: he sees
                to a pressing need wherever he can reach one, his own larder being no use to him out here, and
                every few looks tries the way home again (a failed search can cover the whole map). He used to
                wait for the zones to change, and when they did not he stood there until he died of thirst.
            */
            if (!AtHome(k)){
                if (!SeekSupply(k,z,walkers,false) && ((tick + k.id) / ECONOMY_LOOK_TICKS) % 4 == 0){
                    GoHome(k,z,walkers);
                }
                break;
            }
            //Before any work: what he needs and his home has run out of (needs_plan.md).
            if (SeekSupply(k,z,walkers)){
                break;
            }
            //Then his house's larder, running low, if he is the one to go (step 2) - before wood for a site, and
            //before his own round.
            if (FetchHome(k,z,walkers)){
                break;
            }
            if (k.job == WORKER_JOB_NONE || k.job == WORKER_JOB_BUILDER){
                //Nothing of his own to do, or a builder: he carries wood to a construction that wants it, or
                //goes out to clear a planned road's or wall's way and lay a road - whichever comes first in
                //the one ranking (FindSiteWork; line_works_plan.md); the line work alone if none of that could go.
                if (!FindSiteWork(k,z,walkers)){
                    FindRoadWork(k,z,walkers,false);
                }
                break;
            }
            if (!k.house){
                break;
            }
            //A farmer's land (land_plan.md): a field to sow before all else - a harvest can wait, a field sown
            //late grows late - then a harvest to fetch.
            if (k.job == WORKER_JOB_FARMER){
                if (!GoSow(k,z,walkers)){
                    FetchHarvest(k,z,walkers);
                }
                break;
            }
            //What waits at his workplace first - the pile, the harvest, the water - when a store takes it;
            //then, for a woodcutter, a tree, while his pile has room for one - and only with a lot to pile on.
            if (FetchFromWorkplace(k,z,walkers)){
                break;
            }
            if (pile >= 0 && own[GOOD_WOOD] + ECONOMY_WOOD_PER_TREE <= ECONOMY_HUT_PILE){
                FindTree(k,z,walkers);
            }
            break;
        case WORKER_TO_WORK:
            Walk(k,dt);
            if (Arrived(k)){
                if (k.prop < 0){
                    k.state = WORKER_WORKING;       //at his workplace, to pick up a load
                    k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
                    break;
                }
                int ps = state.prop_state[k.prop];
                if (ps == PROP_STATE_STUMP){
                    GoHome(k,z,walkers);    //someone was quicker
                }else{
                    k.state = WORKER_WORKING;
                    //Felling at his pace: a strong woodcutter is quicker at it.
                    k.timer = (ps == PROP_STATE_STANDING) ? SecondsToTicks(ECONOMY_FELL_SECONDS / pace)
                                                          : SecondsToTicks(ECONOMY_PICK_SECONDS);
                }
            }
            break;
        case WORKER_WORKING:
            if (--k.timer > 0){
                break;
            }
            if (k.prop < 0 && k.job == WORKER_JOB_FARMER){
                //At a field of his land: its harvest, home first (land_plan.md).
                if (!PickHarvest(k,z,walkers)){
                    GoHome(k,z,walkers);
                }
                break;
            }
            if (k.prop < 0){
                //Picking up at his workplace: a load of what lies there, if it still does.
                int good = -1;
                for (int gd = 0; gd < GOOD_COUNT; gd++){
                    if (own[gd] > 0 && (good < 0 || own[gd] > own[good])){
                        good = gd;
                    }
                }
                if (good < 0){
                    GoHome(k,z,walkers);
                    break;
                }
                k.carry_good = good;
                k.carry = std::min(k.job == WORKER_JOB_WOODCUTTER ? ECONOMY_WOOD_PER_TREE : ECONOMY_LOAD,own[good]);
                own[good] -= k.carry;
                if (!FindStore(k,z,walkers)){
                    own[good] += k.carry;   //the store went while he walked: it stays where it was
                    k.carry = 0;
                    k.carry_good = -1;
                    GoHome(k,z,walkers);
                }
            }else if (state.prop_state[k.prop] == PROP_STATE_STANDING){
                //Felled: it lies as a log, which he now picks up.
                state.prop_state[k.prop] = PROP_STATE_LOG;
                state.felled_version++;
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
            }else if (state.prop_state[k.prop] == PROP_STATE_LOG){
                state.prop_state[k.prop] = PROP_STATE_STUMP;
                state.felled_version++;
                k.prop = -1;
                k.carry_good = GOOD_WOOD;
                k.carry = ECONOMY_WOOD_PER_TREE;
                deliver();      //never left standing in the wood with it
            }else{
                GoHome(k,z,walkers);
            }
            break;
        case WORKER_TO_STORE:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_UNLOADING;
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
            }
            break;
        case WORKER_UNLOADING:{
            if (--k.timer > 0){
                break;
            }
            uint32_t s = k.store;
            if (s == 0){
                //At his woodpile: onto it, as far as it has room.
                int put = std::min(k.carry,std::max(0,ECONOMY_HUT_PILE - own[k.carry_good]));
                own[k.carry_good] += put;
                k.carry -= put;
            }else if (s < state.accepts.size() && (state.accepts[s] & GOOD_BIT(k.carry_good))){
                //The store may have changed while he walked: what it still takes and has room for.
                int put = std::min(k.carry,std::max(0,state.room[s] - CarryTotal(state.stock[s])));
                state.stock[s][k.carry_good] += put;
                k.carry -= put;
            }
            if (k.carry > 0){
                deliver();
            }else{
                k.carry_good = -1;
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_TO_HOME:
            Walk(k,dt);
            if (Arrived(k)){
                /*
                    His family was given a house while he walked back to the old home (HouseFamilies moves
                    only those standing at home): on to the new one. Left here he was "at home, indoors"
                    in a house he was nowhere near - out of sight, and never looking for work or water.
                */
                if (!AtHome(k)){
                    GoHome(k,z,walkers);
                    break;
                }
                k.state = WORKER_AT_HOME;
                //In at his door - but never into a tent: a camp's people stay where they can be seen.
                k.f_indoors = k.house != 0 && k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_HOUSE;
            }
            break;
        case WORKER_HOLDING:
            //Holding a load: a store that takes it, or room on his pile, whichever comes first; else, for food,
            //his own house once it is short of food again (HomeShort) - a harvest left over when the larder filled
            //while he walked (land_plan.md) was held for good. Only then, so it never crowds out the water; and
            //never wood, which would fill the larder. With none, a pressing need is not left to kill him where
            //he stands; he takes the load along.
            if (f_look){
                deliver();
                if (k.state == WORKER_HOLDING && k.carry_good >= 0 && (GOODS_FOOD & GOOD_BIT(k.carry_good))
                    && (HomeShort(k,z,NULL) & GOODS_FOOD) && RouteHome(k,z,walkers)){
                    k.store = 0;
                    k.state = WORKER_TO_LARDER;
                }
                if (k.state == WORKER_HOLDING){
                    SeekSupply(k,z,walkers);
                }
            }
            break;
        case WORKER_TO_FETCH:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_FETCHING;
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
            }
            break;
        case WORKER_FETCHING:{
            if (--k.timer > 0){
                break;
            }
            //A load of what is there and what the site still wants - either may have changed on the way.
            uint32_t from = k.store;
            bool f_site = SiteAlive(z,k.site);
            int have = (from && from < state.stock.size()) ? state.stock[from][GOOD_WOOD] : 0;
            int take = f_site ? std::min(ECONOMY_LOAD,std::min(have,SiteLeft(z,k.site,&k))) : 0;
            int to = !f_site ? -1 : EconomyIsGroundSite(k.site) ? GroundSiteApproach(z,EconomyGroundSitePlot(k.site))
                                                                : NearestPlot(k.site,k.pos,true);
            if (take <= 0 || to < 0 || !RouteTo(k,z,walkers,to,state.world->grid->fine.pos[to])){
                k.site = 0;
                GoHome(k,z,walkers);
                break;
            }
            state.stock[from][GOOD_WOOD] -= take;
            k.carry_good = GOOD_WOOD;
            k.carry = take;
            k.store = 0;
            k.state = WORKER_TO_SITE;
            break;
        }
        case WORKER_TO_SITE:
            Walk(k,dt);
            if (Arrived(k)){
                uint32_t id = k.site;
                if (SiteAlive(z,id) && SiteNeed(z,id) > 0){
                    if (EconomyIsGroundSite(id)){
                        state.ground_site[EconomyGroundSitePlot(id)] += k.carry;
                    }else{
                        state.site[id] += k.carry;
                    }
                    k.carry = 0;
                    k.carry_good = -1;
                    k.state = WORKER_BUILDING;
                    k.timer = SecondsToTicks(ECONOMY_BUILD_SECONDS / pace);
                }else{
                    //Pulled down, or finished by other hands, while he walked: the wood goes to a store.
                    k.site = 0;
                    deliver();
                }
            }
            break;
        case WORKER_BUILDING:
            if (--k.timer > 0){
                break;
            }
            Build(z,k.site);
            k.site = 0;
            GoHome(k,z,walkers);
            break;
        //--- Road works (line_works_plan.md) ---
        case WORKER_TO_ROAD:{
            Walk(k,dt);
            if (!Arrived(k)){
                break;
            }
            int v = EconomyGroundSitePlot(k.site);
            if (!SiteAlive(z,k.site)){
                NextRoadWork(k,z,walkers);      //erased, or laid by other hands, while he walked
                break;
            }
            if (k.prop >= 0){
                if (state.prop_state[k.prop] == PROP_STATE_STANDING){
                    k.state = WORKER_CLEARING;
                    k.timer = SecondsToTicks(ECONOMY_FELL_SECONDS / pace);
                }else{
                    NextRoadWork(k,z,walkers);  //a woodcutter was quicker
                }
                break;
            }
            //To lay it: only with nothing of its way still standing - a neighbour painted since may have
            //brought a tree into it. A wall has nothing to lay: its wood is carried (FindSiteWork).
            std::vector<int> trees;
            RoadTrees(z,v,trees);
            if (!trees.empty() || z.ground[v] != ZONE_GROUND_ROAD){
                NextRoadWork(k,z,walkers);
                break;
            }
            k.state = WORKER_LAYING;
            k.timer = SecondsToTicks(ECONOMY_LAY_SECONDS / pace);
            break;
        }
        case WORKER_CLEARING:
            if (--k.timer > 0){
                break;
            }
            //Felled, and left where it fell: a log for a woodcutter to fetch, the road's way clear of it.
            if (k.prop >= 0 && state.prop_state[k.prop] == PROP_STATE_STANDING){
                state.prop_state[k.prop] = PROP_STATE_LOG;
                state.felled_version++;
                state.version++;
            }
            k.prop = -1;
            //A wall whose wood came while its last tree stood goes up now.
            if (EconomyIsGroundSite(k.site)){
                RaiseWall(z,EconomyGroundSitePlot(k.site));
            }
            k.stint++;
            NextRoadWork(k,z,walkers);
            break;
        case WORKER_LAYING:{
            if (--k.timer > 0){
                break;
            }
            int v = EconomyGroundSitePlot(k.site);
            std::vector<int> trees;
            if (SiteAlive(z,k.site)){
                RoadTrees(z,v,trees);
            }
            if (SiteAlive(z,k.site) && trees.empty() && z.ground[v] == ZONE_GROUND_ROAD
                && !std::count(ground_raises.begin(),ground_raises.end(),v)){
                ground_raises.push_back(v);     //laid: the app raises it this tick (ZONE_OP_GROUND_RAISE)
                state.version++;
            }
            k.stint++;
            NextRoadWork(k,z,walkers);
            break;
        }
        //--- A pressing need (needs_plan.md) ---
        case WORKER_TO_SUPPLY:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_SUPPLYING;
                k.timer = SecondsToTicks(ECONOMY_MEAL_SECONDS);
            }
            break;
        case WORKER_SUPPLYING:
            if (--k.timer > 0){
                break;
            }
            //From what is there now - others may have been quicker - as he would at home.
            if (k.store && k.store < state.stock.size()){
                Consume(k,state.stock[k.store]);
            }
            /*
                At a collector that is filling, with room for more and no hunger to see to: he waits for its
                next unit, a few seconds off, and drinks his fill - rather than walk home with nothing and back
                again, which kept two of a camp dry for a day, or with one day's water for a walk of more than
                one. While he is still thirsty he counts on a unit there, so nobody else sets out for it.
            */
            if (k.water + NEEDS_WATER_WORTH <= NEEDS_WATER_FULL && !(EconomyPressing(k) & GOODS_FOOD) && k.store < z.buildings.size()
                && z.buildings[k.store].kind == ZONE_KIND_WATER && k.store < home_plot.size() && home_plot[k.store] >= 0
                && !UnderSnow(*state.world,state.world->grid->fine.pos[home_plot[k.store]],tick)
                && std::any_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){ return o.building == k.store; })){
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
                break;
            }
            {
                uint32_t from = k.store;
                k.store = 0;
                if (k.carry > 0){
                    deliver();          //the load he was holding, or another try at it
                    break;
                }
                /*
                    At a store or the camp, for a house short of what is here (step 2): a load of it home with him -
                    the scarcer first - rather than home with nothing and back again for it.
                */
                uint8_t first = 0;
                uint8_t short_of = HomeShort(k,z,&first);
                bool f_store = from < z.buildings.size()
                               && (z.buildings[from].kind == ZONE_KIND_STORE || z.buildings[from].kind == ZONE_KIND_CAMP);
                if (f_store && short_of && (TakeHome(k,z,walkers,from,first)
                                            || TakeHome(k,z,walkers,from,(uint8_t)(short_of & ~first)))){
                    break;
                }
                GoHome(k,z,walkers);
            }
            break;
        //--- Fetching home (needs_plan.md, step 2) ---
        case WORKER_TO_GET:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_GETTING;
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
            }
            break;
        case WORKER_GETTING:{
            if (--k.timer > 0){
                break;
            }
            //Of the kind he came for, what is there now and what home still has room for - either may have
            //changed on the way. Of food, whichever there is most of: what he set out for may be gone.
            uint8_t goods = (k.carry_good == GOOD_WATER) ? GOOD_BIT(GOOD_WATER) : GOODS_FOOD;
            if (!TakeHome(k,z,walkers,k.store,goods)){
                k.carry_good = -1;
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_TO_LARDER:{
            Walk(k,dt);
            if (!Arrived(k)){
                break;
            }
            //His family was moved while he walked: on to the new home with it.
            if (!AtHome(k)){
                if (!RouteHome(k,z,walkers)){
                    deliver();
                }
                break;
            }
            //Into the house's own stock as far as it keeps it and has room; a camp's supplies take it all.
            uint32_t h = k.house;
            int put = 0;
            if (h && h < z.buildings.size() && h < state.stock.size() && k.carry_good >= 0){
                if (z.buildings[h].kind == ZONE_KIND_CAMP){
                    put = k.carry;
                }else if (h < state.keeps.size() && (state.keeps[h] & GOOD_BIT(k.carry_good))){
                    put = std::min(k.carry,std::max(0,state.room[h] - CarryTotal(state.stock[h])));
                }
                state.stock[h][k.carry_good] += put;
            }
            k.carry -= put;
            state.version++;
            if (k.carry > 0){
                deliver();          //what home has no room for after all goes to a store
                break;
            }
            k.carry_good = -1;
            k.state = WORKER_AT_HOME;
            k.f_indoors = h != 0 && h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_HOUSE;
            break;
        }
        //--- Sowing (fields_plan.md) ---
        case WORKER_TO_SOW:
            Walk(k,dt);
            if (Arrived(k)){
                //The field of his land he came to sow is `store` (land_plan.md).
                int cells = (k.store < cells_of.size()) ? std::max(1,(int)cells_of[k.store].size()) : 1;
                k.state = WORKER_SOWING;
                k.timer = SecondsToTicks(ECONOMY_SOW_SECONDS * (float)cells / pace);
            }
            break;
        case WORKER_SOWING:{
            if (--k.timer > 0){
                break;
            }
            //Sown with what is wanted NOW - the player may have chosen again while he walked - if it still
            //wants sowing: built, empty, and its crop decided.
            uint32_t id = k.store;
            EconomyField* f = FieldOf(id);
            int crop = (f && id < z.buildings.size()) ? EconomyFieldSowCrop(*f,z.buildings[id]) : -1;
            if (f && !f->f_sown && crop >= 0 && id < site_cells.size() && site_cells[id] == 0){
                f->crop = crop;
                f->f_sown = true;
                f->grown = 0;
                f->yield = EconomyCropYield(f->last,crop);
                state.version++;
            }
            GoHome(k,z,walkers);
            break;
        }
        default:
            k.state = WORKER_AT_HOME;
            break;
    }
}

EconomyField* Economy::FieldOf(uint32_t id){
    for (EconomyField& f : state.fields){
        if (f.building == id){
            return &f;
        }
    }
    return NULL;
}

/*
    A field of his land wants SOWING (fields_plan.md, land_plan.md): all of it built, nothing growing, its crop
    decided - the player's starting crop the first time, its next crop after - and its share farmed, not lying
    fallow; and nobody else already out to sow it. The nearest to him by the straight line, the lower id on a
    tie. Out to it (`store`), where its harvest is picked up too.
*/
bool Economy::GoSow(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t best = 0;
    float best_d = 1e30f;
    for (const EconomyField& f : state.fields){
        uint32_t id = f.building;
        if (f.owner != k.building || id >= z.buildings.size() || id >= site_cells.size() || site_cells[id] > 0
            || home_plot[id] < 0 || id >= state.farmer_of.size() || !state.farmer_of[id] || f.f_sown
            || EconomyFieldSowCrop(f,z.buildings[id]) < 0){
            continue;
        }
        bool f_taken = std::any_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
            return &o != &k && o.store == id && (o.state == WORKER_TO_SOW || o.state == WORKER_SOWING);
        });
        float d = (w.grid->fine.pos[home_plot[id]] - k.pos).length();
        if (!f_taken && d < best_d){
            best_d = d;
            best = id;
        }
    }
    if (!best){
        return false;
    }
    int plot = SourcePlot(best,k.pos);
    if (plot >= 0 && RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
        k.prop = -1;
        k.store = best;
        k.state = WORKER_TO_SOW;
        return true;
    }
    return false;
}

/*
    From home, a harvest waiting on his land (land_plan.md): of its fields with food lying - fallow ones too,
    for what grew before - the one with the most, the lower id on a tie, that no other farmer is already out to
    fetch from; and only with somewhere for a load of it: the camp's supplies always, his house while it is
    short of food, or else a store with room. Out to it (`store`). True if he set off.
*/
bool Economy::FetchHarvest(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t land = k.building;
    bool f_home = (land < z.buildings.size() && z.buildings[land].kind == ZONE_KIND_CAMP)
                  || (HomeShort(k,z,NULL) & GOODS_FOOD);
    uint32_t best = 0;
    int most = 0;
    for (const EconomyField& f : state.fields){
        uint32_t id = f.building;
        if (f.owner != land || id >= state.stock.size() || home_plot[id] < 0){
            continue;
        }
        int good = MostOf(state.stock[id],GOODS_FOOD);
        int have = (good >= 0) ? state.stock[id][good] : 0;
        if (have <= most){
            continue;
        }
        bool f_taken = std::any_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
            return &o != &k && o.store == id && (o.state == WORKER_TO_WORK || o.state == WORKER_WORKING);
        });
        bool f_stored = f_home;
        int load = std::min(ECONOMY_LOAD,have);
        for (size_t s = 1; s < state.accepts.size() && !f_stored; s++){
            f_stored = (state.accepts[s] & GOOD_BIT(good)) && state.room[s] - CarryTotal(state.stock[s]) >= load;
        }
        if (!f_taken && f_stored){
            best = id;
            most = have;
        }
    }
    if (!best){
        return false;
    }
    int plot = SourcePlot(best,k.pos);
    if (plot >= 0 && RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
        k.prop = -1;
        k.store = best;
        k.state = WORKER_TO_WORK;
        return true;
    }
    return false;
}

/*
    At the field he came to (`store`), its harvest picked up - HOME FIRST (land_plan.md): the camp's land a whole
    load into the camp's supplies, which take it all; a house's while it is short of food, what brings it up
    (TakeHome); otherwise a load to the nearest store that takes it. False, taking nothing, with nothing there or
    nowhere for it.
*/
bool Economy::PickHarvest(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    uint32_t id = k.store;
    if (!id || id >= state.stock.size()){
        return false;
    }
    std::array<int,GOOD_COUNT>& own = state.stock[id];
    int good = MostOf(own,GOODS_FOOD);
    if (good < 0){
        return false;
    }
    if (k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP){
        int take = std::min(ECONOMY_LOAD,own[good]);
        if (!RouteHome(k,z,walkers)){
            return false;
        }
        own[good] -= take;
        k.carry_good = good;
        k.carry = take;
        k.store = 0;
        k.prop = -1;
        k.state = WORKER_TO_LARDER;
        state.version++;
        return true;
    }
    if ((HomeShort(k,z,NULL) & GOODS_FOOD) && TakeHome(k,z,walkers,id,GOODS_FOOD)){
        return true;
    }
    k.carry_good = good;
    k.carry = std::min(ECONOMY_LOAD,own[good]);
    own[good] -= k.carry;
    if (FindStore(k,z,walkers)){
        state.version++;
        return true;
    }
    own[good] += k.carry;       //no store takes it after all: it stays where it was
    k.carry = 0;
    k.carry_good = -1;
    return false;
}

void Economy::Tick(const ZoneState& z, Walkers& walkers, uint64_t tick, float dt){
    if (!state.world || z.world != state.world){
        return;
    }
    const ChasmWorld& w = *state.world;
    raises.clear();
    ground_raises.clear();
    field_raises.clear();
    if (f_resettle && z.version == state.zones_version){
        //A save from before land (Restore): its land settled and its farmers taken on, on its first tick. No
        //re-plan - nothing on the ground changed, so everyone walks on as saved.
        HouseFamilies(z,walkers);
        AssignJobs(z,walkers);
        state.version++;
    }
    f_resettle = false;
    if (z.version != state.zones_version){
        Derive(z,true);
        //A new colony's first tick: the settlers' supplies into their camp.
        if (f_new_colony){
            f_new_colony = false;
            for (size_t id = 1; id < z.buildings.size(); id++){
                if (z.buildings[id].kind == ZONE_KIND_CAMP && stands[id]){
                    std::array<int,GOOD_COUNT>& s = state.stock[id];
                    s[GOOD_WOOD] += ECONOMY_START_WOOD;
                    s[GOOD_WHEAT] += ECONOMY_START_WHEAT;
                    s[GOOD_BEANS] += ECONOMY_START_BEANS;
                    s[GOOD_WATER] += ECONOMY_START_WATER;
                    break;
                }
            }
        }
        HouseFamilies(z,walkers);
        AssignJobs(z,walkers);
        Replan(z,walkers);
        state.version++;
    }
    //Who works where, by workplace id: a collector fills only while somebody works it (a field: field_pace).
    std::vector<float> pace(z.buildings.size(),0.0f);
    for (const EconomyWorker& k : state.workers){
        if (k.building && k.building < pace.size()){
            pace[k.building] = EconomySkillFactor(k.skills[EconomyJobSkill(k.job)]);
        }
    }
    CalendarDate date = CalendarDateOf(tick);
    /*
        The fields grow once SOWN (fields_plan.md), in any weather but winter's and the snow's, while
        their share of the land is farmed, at that farmer's pace (land_plan.md) - a fallow field waits.
        At harvest the field yields its built area x its crop's own yield x the yield it was sown at, and
        is empty again, that crop its last.
    */
    for (EconomyField& f : state.fields){
        int crop = f.crop;
        if (!f.f_sown || crop < 0 || crop >= ZONE_CROP_COUNT){
            continue;
        }
        int home = home_plot[f.building];
        float farmed = (f.building < field_pace.size()) ? field_pace[f.building] : 0.0f;
        if (home < 0 || farmed <= 0.0f || date.season == SEASON_WINTER || UnderSnow(w,w.grid->fine.pos[home],tick)){
            continue;
        }
        f.grown += (int)std::lround(farmed * 100.0f);
        if (f.grown >= crop_days[crop] * CALENDAR_DAY_TICKS * 100){
            float area = ZoneBuildingFigures(w,z,f.building).floor_area;
            state.stock[f.building][GoodOfCrop(crop)] += (int)std::floor(area * crop_yield[crop] * (float)f.yield / 100.0f);
            f.grown = 0;
            f.f_sown = false;
            f.last = crop;
            f.crop = -1;
            state.version++;
        }
    }
    //A collector fills, a unit at a time, up to what it holds - not frozen under the snow.
    int water_ticks = SecondsToTicks(ECONOMY_WATER_SECONDS);
    if (tick % (uint64_t)water_ticks == 0){
        for (size_t id = 1; id < z.buildings.size(); id++){
            if (z.buildings[id].kind != ZONE_KIND_WATER || z.buildings[id].size <= 0 || home_plot[id] < 0 || pace[id] <= 0.0f){
                continue;
            }
            std::array<int,GOOD_COUNT>& s = state.stock[id];
            if (s[GOOD_WATER] < ECONOMY_WATER_HOLD && !UnderSnow(w,w.grid->fine.pos[home_plot[id]],tick)){
                s[GOOD_WATER]++;
                state.version++;
            }
        }
    }
    for (EconomyWorker& k : state.workers){
        TickWorker(k,z,walkers,tick,dt);
    }
    Needs(z,walkers,tick);
    if (!state.workers.empty()){
        state.version++;    //they move every tick - the view redraws them
    }
}

//--- Needs (docs/needs_plan.md) ---------------------------------------------------------------------

int EconomyFoodWorth(int good){
    switch (good){
        case GOOD_WHEAT:    return CALENDAR_DAY_TICKS;
        case GOOD_GREENS:   return CALENDAR_DAY_TICKS / 2;
        case GOOD_BEANS:    return CALENDAR_DAY_TICKS * 2;
        default:            return 0;
    }
}

const char* EconomyDeathCause(int cause){
    switch (cause){
        case NEEDS_DIED_HUNGER: return "hunger";
        case NEEDS_DIED_THIRST: return "thirst";
        default:                return "?";
    }
}

std::string EconomyDeathName(const EconomyDeath& d){
    EconomyWorker k;
    k.id = d.id;
    k.family = d.family;
    k.f_female = d.f_female;
    return EconomyPersonName(k);
}

//A meal and a drink, standing at home - from his house's own stock, or his camp's supplies.
void Economy::EatAndDrink(EconomyWorker& k, const ZoneState& z){
    if (!k.house || k.house >= state.stock.size() || k.house >= z.buildings.size() || k.state != WORKER_AT_HOME
        || !AtHome(k)){
        return;
    }
    int kind = z.buildings[k.house].kind;
    if (kind != ZONE_KIND_HOUSE && kind != ZONE_KIND_CAMP){
        return;
    }
    Consume(k,state.stock[k.house]);
}

/*
    Eating and drinking from a stock, at home or away: only a whole unit that fits under full, so nothing is
    wasted. Water first: food does nothing for someone dry. Then food while any fits, the kind that keeps
    worst first - greens rot, wheat keeps, beans keep longest (gameplay_plan.md, "Goods and food") - so a
    larder eats down its greens and saves its beans.
*/
void Economy::Consume(EconomyWorker& k, std::array<int,GOOD_COUNT>& from){
    while (from[GOOD_WATER] > 0 && k.water + NEEDS_WATER_WORTH <= NEEDS_WATER_FULL){
        from[GOOD_WATER]--;
        k.water += NEEDS_WATER_WORTH;
        state.version++;
    }
    if (EconomyDry(k)){
        return;
    }
    static const int keeps_worst_first[] = {GOOD_GREENS,GOOD_WHEAT,GOOD_BEANS};
    for (bool f_ate = true; f_ate;){
        f_ate = false;
        for (int good : keeps_worst_first){
            int worth = EconomyFoodWorth(good);
            if (from[good] > 0 && k.health + worth <= NEEDS_HEALTH_FULL){
                from[good]--;
                k.health += worth;
                f_ate = true;
                state.version++;
                break;
            }
        }
    }
}

bool Economy::SetNeeds(uint32_t person, int health, int water){
    for (EconomyWorker& k : state.workers){
        if (k.id != person){
            continue;
        }
        if (health >= 0){
            k.health = std::min(health,NEEDS_HEALTH_FULL);
        }
        if (water >= 0){
            k.water = std::min(water,NEEDS_WATER_FULL);
        }
        state.version++;
        return true;
    }
    return false;
}

/*
    A PRESSING NEED (EconomyPressing) his home has none of - water, or food - and he takes on no more work
    (the user's, 2026-10-07): off to the nearest place that has some of what home lacks - a store, the
    camp's supplies, a collector's water, a field's harvest; never another family's house. Nearest by the
    straight line, the lower id on a tie; the first of the nearest few he can walk to. False with no need,
    a home that can meet it, or nowhere he can get to - then he carries on as before, since stopping work
    would save nobody, and the work may be the collector that would. `f_home` false: he cannot get home, so
    what it holds does not count.
*/
bool Economy::SeekSupply(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_home){
    uint8_t want = EconomyPressing(k);
    if (!want){
        return false;
    }
    auto has = [&](uint32_t id, uint8_t goods){
        for (int g = 0; g < GOOD_COUNT; g++){
            if ((goods & GOOD_BIT(g)) && state.stock[id][g] > 0){
                return true;
            }
        }
        return false;
    };
    //What his home has he gets there, on his way back.
    if (f_home && k.house && k.house < state.stock.size()){
        if (has(k.house,GOOD_BIT(GOOD_WATER))){
            want &= (uint8_t)~GOOD_BIT(GOOD_WATER);
        }
        if (has(k.house,GOODS_FOOD)){
            want &= (uint8_t)~GOODS_FOOD;
        }
    }
    if (!want){
        return false;
    }
    /*
        What is there of a kind of good that nobody already on his way to it counts on - a unit each, what
        takes him past pressing, though he drinks or eats his fill when he gets there. Without it a supply
        as small as a collector's sent everyone thirsty at once, and half back with nothing (two of ten dry
        for a day); counting each one's fill instead kept the thirsty at home dry while the collector
        refilled under them (three of ten died).
    */
    auto spare = [&](uint32_t id, uint8_t goods){
        int left = 0;
        for (int g = 0; g < GOOD_COUNT; g++){
            left += (goods & GOOD_BIT(g)) ? state.stock[id][g] : 0;
        }
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.store == id && (o.state == WORKER_TO_SUPPLY || o.state == WORKER_SUPPLYING)
                && (EconomyPressing(o) & goods)){
                left--;
            }
        }
        return left;
    };
    auto offers = [&](uint32_t id){
        return ((want & GOOD_BIT(GOOD_WATER)) && spare(id,GOOD_BIT(GOOD_WATER)) > 0)
            || ((want & GOODS_FOOD) && spare(id,GOODS_FOOD) > 0);
    };
    const ChasmWorld& w = *state.world;
    std::vector<std::pair<float,uint32_t>> found;
    size_t n = std::min(z.buildings.size(),state.stock.size());
    for (size_t id = 1; id < n; id++){
        if (id == k.house || z.buildings[id].size <= 0 || z.buildings[id].kind == ZONE_KIND_HOUSE
            || !has((uint32_t)id,want) || !offers((uint32_t)id)){
            continue;
        }
        int plot = SourcePlot((uint32_t)id,k.pos);
        if (plot >= 0){
            found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = SourcePlot(id,k.pos);
        if (RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
            k.store = id;
            k.prop = -1;
            k.state = WORKER_TO_SUPPLY;
            return true;
        }
    }
    return false;
}

/*
    FETCHING HOME (needs_plan.md, step 2). What his house is short of: water, food or both, where it holds less
    than ECONOMY_HOME_DAYS of what its family drinks or eats - each counted in days of the family's bars, which
    fall a day's worth a day. `first` is the scarcer, water on a tie: thirst kills sooner. Nothing for a camp,
    whose families do not fetch, or for a house without room for a trip's worth - half a load, or half its
    room if that is less: a house too small for two days of both is always short, and was fetched for a unit
    at a time, every look.
*/
uint8_t Economy::HomeShort(const EconomyWorker& k, const ZoneState& z, uint8_t* first) const{
    uint32_t h = k.house;
    if (!h || h >= z.buildings.size() || h >= state.stock.size() || h >= stands.size() || !stands[h]
        || z.buildings[h].kind != ZONE_KIND_HOUSE){
        return 0;
    }
    int trip = std::max(1,std::min((ECONOMY_LOAD + 1) / 2,(state.room[h] + 1) / 2));
    if (state.room[h] - CarryTotal(state.stock[h]) < trip){
        return 0;
    }
    int64_t enough = Living(state.workers,h) * ECONOMY_HOME_DAYS * CALENDAR_DAY_TICKS;
    int64_t water = HeldTicks(state.stock[h],GOOD_BIT(GOOD_WATER));
    int64_t food = HeldTicks(state.stock[h],GOODS_FOOD);
    uint8_t short_of = (uint8_t)((water < enough ? GOOD_BIT(GOOD_WATER) : 0) | (food < enough ? GOODS_FOOD : 0));
    if (first){
        bool f_both = short_of == (uint8_t)(GOOD_BIT(GOOD_WATER) | GOODS_FOOD);
        *first = f_both ? ((food < water) ? (uint8_t)GOODS_FOOD : (uint8_t)GOOD_BIT(GOOD_WATER)) : short_of;
    }
    return short_of;
}

/*
    From home, before other work: off for a load for his house, if it is short (HomeShort) and he is the one to
    go - an idle member first; one with a job, between his rounds, only with no jobless member home to go
    instead - and nobody of the house is on the errand already. From the nearest store or camp with some to
    spare - the camp's supplies are the colony's (the user's, 2026-10-07) - or, for food, a harvest lying on
    the house's own land (land_plan.md: seen in its first run, a family fetching from the camp past its own
    full fields) - by the straight line, the lower id on a tie: the first of the nearest few he can walk to,
    and home from. The scarcer first; the other if nowhere has the scarcer. True if he set off.
*/
bool Economy::FetchHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    uint8_t first = 0;
    uint8_t short_of = HomeShort(k,z,&first);
    if (!short_of){
        return false;
    }
    for (const EconomyWorker& o : state.workers){
        if (&o == &k || o.house != k.house){
            continue;
        }
        if (o.state == WORKER_TO_GET || o.state == WORKER_GETTING || o.state == WORKER_TO_LARDER){
            return false;       //one errand a house at a time
        }
        if (k.job != WORKER_JOB_NONE && o.job == WORKER_JOB_NONE && o.state == WORKER_AT_HOME && AtHome(o)){
            return false;       //an idle one is home: he goes, at his next look
        }
    }
    //What a place has of `goods` that nobody else fetching home already counts on: a load each.
    auto spare = [&](uint32_t id, uint8_t goods){
        int left = 0;
        for (int g = 0; g < GOOD_COUNT; g++){
            left += (goods & GOOD_BIT(g)) ? state.stock[id][g] : 0;
        }
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.store == id && (o.state == WORKER_TO_GET || o.state == WORKER_GETTING)
                && o.carry_good >= 0 && (goods & GOOD_BIT(o.carry_good))){
                left -= ECONOMY_LOAD;
            }
        }
        return left;
    };
    const ChasmWorld& w = *state.world;
    int home = HomePlot(k);
    size_t n = std::min(z.buildings.size(),state.stock.size());
    std::vector<uint8_t> own_land(n,0);
    for (const EconomyField& f : state.fields){
        if (f.owner == k.house && f.building < n){
            own_land[f.building] = 1;
        }
    }
    const uint8_t order[2] = {first,(uint8_t)(short_of & ~first)};
    for (uint8_t goods : order){
        if (!goods){
            continue;
        }
        std::vector<std::pair<float,uint32_t>> found;
        for (size_t id = 1; id < n; id++){
            int kind = z.buildings[id].kind;
            bool f_land = own_land[id] && !(goods & GOOD_BIT(GOOD_WATER));
            if (((kind != ZONE_KIND_STORE && kind != ZONE_KIND_CAMP) && !f_land) || z.buildings[id].size <= 0 || !stands[id]
                || spare((uint32_t)id,goods) <= 0){
                continue;
            }
            int plot = SourcePlot((uint32_t)id,k.pos);
            if (plot >= 0){
                found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
            }
        }
        std::sort(found.begin(),found.end());
        for (size_t t = 0; t < found.size() && t < 4; t++){
            uint32_t id = found[t].second;
            int plot = SourcePlot(id,k.pos);
            vec2 at = w.grid->fine.pos[plot];
            if (CanWalk(z,walkers,at,home) && RouteTo(k,z,walkers,plot,at)){
                k.store = id;
                k.carry_good = MostOf(state.stock[id],goods);   //the kind he is after; nothing carried yet
                k.carry = 0;
                k.prop = -1;
                k.state = WORKER_TO_GET;
                return true;
            }
        }
    }
    return false;
}

/*
    A load for his house picked up at `from`: of `goods`, the kind there is most of - as much as a load and the
    house's room allow, and no more than brings the house to a day past ECONOMY_HOME_DAYS of it, so the next
    errand is a day off and water does not fill the room food needs (it did: nine water, no food, for a day)
    - and off home with it (WORKER_TO_LARDER). False, taking nothing, with none there, no room at home or no
    way back.
*/
bool Economy::TakeHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint32_t from, uint8_t goods){
    uint32_t h = k.house;
    if (!from || from >= state.stock.size() || !h || h >= z.buildings.size() || h >= state.room.size()
        || z.buildings[h].kind != ZONE_KIND_HOUSE){
        return false;
    }
    int good = MostOf(state.stock[from],goods);
    if (good < 0){
        return false;
    }
    int room = state.room[h] - CarryTotal(state.stock[h]);
    int worth = (good == GOOD_WATER) ? NEEDS_WATER_WORTH : EconomyFoodWorth(good);
    int64_t want = Living(state.workers,h) * (ECONOMY_HOME_DAYS + 1) * CALENDAR_DAY_TICKS - HeldTicks(state.stock[h],goods);
    int upto = (want > 0 && worth > 0) ? (int)((want + worth - 1) / worth) : 0;
    int take = std::min(std::min(ECONOMY_LOAD,upto),std::min(state.stock[from][good],room));
    if (take <= 0 || !RouteHome(k,z,walkers)){
        return false;
    }
    state.stock[from][good] -= take;
    k.carry_good = good;
    k.carry = take;
    k.store = 0;
    k.prop = -1;
    k.state = WORKER_TO_LARDER;
    state.version++;
    return true;
}

/*
    Every person, every tick, after his round has moved on: a tick of water gone, and of health - three
    while dry - then a meal and a drink if he stands at home. Health at nothing: he dies. He is struck from
    the colony with whatever he carried; his family stays in its house, and on this same tick his job is
    given out again, and a house left empty taken by a family waiting for one.
*/
void Economy::Needs(const ZoneState& z, Walkers& walkers, uint64_t tick){
    bool f_died = false;
    for (EconomyWorker& k : state.workers){
        if (k.water > 0){
            k.water--;
        }
        k.health -= 1 + (EconomyDry(k) ? NEEDS_DRY_DRAIN : 0);
        EatAndDrink(k,z);
        f_died = f_died || k.health <= 0;
    }
    if (!f_died){
        return;
    }
    std::vector<EconomyWorker> living;
    for (EconomyWorker& k : state.workers){
        if (k.health > 0){
            living.push_back(std::move(k));
            continue;
        }
        EconomyDeath d;
        d.id = k.id;
        d.family = k.family;
        d.f_female = k.f_female;
        d.age = k.age;
        d.tick = tick;
        d.cause = EconomyDry(k) ? NEEDS_DIED_THIRST : NEEDS_DIED_HUNGER;
        state.dead.push_back(d);
    }
    state.workers.swap(living);
    HouseFamilies(z,walkers);
    AssignJobs(z,walkers);
    state.version++;
}

//--- Construction ----------------------------------------------------------------------------------

namespace {

//What a kind's storey takes against a house's: a tent is canvas and a few poles, a winch has its gear too.
float BuildFactor(int kind){
    switch (kind){
        case ZONE_KIND_CAMP:    return 0.5f;
        case ZONE_KIND_WINCH:   return 1.5f;
        case ZONE_KIND_BRIDGE:  return 0.75f;   //a deck and posts, no walls or roof
        default:                return 1.0f;
    }
}

}

int EconomyStoreyWood(const ChasmWorld& w, const ZoneState& z, int plot){
    float wood = w.picker->PlotArea(plot) * ECONOMY_BUILD_WOOD_PER_AREA * BuildFactor(z.KindOf(plot));
    return std::max(1,(int)std::ceil(wood));
}

//For the panel and the tools: the whole map's plots, so not for the tick (Economy::SiteNeed is).
int EconomySiteWood(const ChasmWorld& w, const ZoneState& z, uint32_t id){
    int wood = 0;
    if (id < z.buildings.size() && z.buildings[id].kind == ZONE_KIND_FIELD){
        for (size_t c = 0; c < z.field.size(); c++){
            wood += (z.field[c] == id && !z.field_built[c]) ? ECONOMY_FIELD_WOOD_PER_CELL : 0;
        }
        return wood;
    }
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id){
            wood += (z.storeys[v] - z.standing[v]) * EconomyStoreyWood(w,z,(int)v);
        }
    }
    return wood;
}

int EconomySiteBrought(const EconomyState& e, uint32_t id){
    return (id < e.site.size()) ? e.site[id] : 0;
}

int EconomyGroundWood(const ChasmWorld& w, int plot){
    float wood = w.picker->PlotArea(plot) * ECONOMY_GROUND_WOOD_PER_AREA;
    return std::max(1,(int)std::ceil(wood));
}

int EconomyGroundCost(const ChasmWorld& w, const ZoneState& z, int plot){
    if (plot < 0 || plot >= (int)z.ground.size() || z.ground[plot] == ZONE_GROUND_ROAD){
        return 0;       //a road takes work, not wood (line_works_plan.md)
    }
    return (z.ground[plot] == ZONE_GROUND_WALL) ? ECONOMY_WALL_WOOD : EconomyGroundWood(w,plot);
}

int EconomyGroundBrought(const EconomyState& e, int plot){
    return (plot >= 0 && plot < (int)e.ground_site.size()) ? e.ground_site[plot] : 0;
}

//A site that is still one: a building that has not gone, or a garden, lot or road plot still to build.
bool Economy::SiteAlive(const ZoneState& z, uint32_t id) const{
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        return v >= 0 && v < (int)z.ground.size() && z.ground[v] != ZONE_GROUND_NONE && !z.ground_built[v]
               && z.storeys[v] == 0;
    }
    return id && id < z.buildings.size() && z.buildings[id].size > 0;
}

int Economy::SiteBrought(uint32_t id) const{
    if (EconomyIsGroundSite(id)){
        return EconomyGroundBrought(state,EconomyGroundSitePlot(id));
    }
    return (id < state.site.size()) ? state.site[id] : 0;
}

int Economy::SiteNeed(const ZoneState& z, uint32_t id) const{
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        return SiteAlive(z,id) ? EconomyGroundCost(*state.world,z,v) : 0;
    }
    if (id >= plots_of.size()){
        return 0;
    }
    //A field site: so much a cell still to build (fields_plan.md).
    if (id < site_cells.size() && site_cells[id] > 0){
        return site_cells[id] * ECONOMY_FIELD_WOOD_PER_CELL;
    }
    int wood = 0;
    for (int v : plots_of[id]){
        wood += (z.storeys[v] - z.standing[v]) * EconomyStoreyWood(*state.world,z,v);
    }
    return wood;
}

/*
    What a site still wants brought: what it needs, less what it has and what is on its way - a load
    for each carrier still going to fetch (he takes less if less is wanted), what each carries for it.
    `except` is left out, so a carrier can ask what is left for himself.
*/
int Economy::SiteLeft(const ZoneState& z, uint32_t id, const EconomyWorker* except) const{
    int left = SiteNeed(z,id) - SiteBrought(id);
    for (const EconomyWorker& o : state.workers){
        if (&o == except || o.site != id){
            continue;
        }
        if (o.state == WORKER_TO_FETCH || o.state == WORKER_FETCHING){
            left -= ECONOMY_LOAD;
        }else if (o.state == WORKER_TO_SITE){
            left -= o.carry;
        }
    }
    return left;
}

//The wood at a building nobody is already on his way to take: a load for each carrier going there.
int Economy::WoodFree(uint32_t id) const{
    int wood = state.stock[id][GOOD_WOOD];
    for (const EconomyWorker& o : state.workers){
        if (o.site && o.store == id && (o.state == WORKER_TO_FETCH || o.state == WORKER_FETCHING)){
            wood -= ECONOMY_LOAD;
        }
    }
    return wood;
}

//Where goods are taken from building `id`: a woodcutter's on his pile, the rest at its plot nearest `to`.
int Economy::SourcePlot(uint32_t id, const vec2& to) const{
    if (id < pile_door.size() && pile_door[id] >= 0){
        return pile_door[id];
    }
    return NearestPlot(id,to);
}

/*
    A garden or lot is built from outside: its builder stands on a plot beside it with nothing built on it
    and no wall of its own round it, the lowest index - otherwise its fence would go up round him and shut
    him in. The plot itself only if every neighbour is closed.
*/
int Economy::GroundSiteApproach(const ZoneState& z, int v) const{
    const ChasmWorld& w = *state.world;
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    int best = -1;
    for (int i = 0; i < n; i++){
        int o = nb[i];
        if (z.storeys[o] > 0 || ZoneGroundStands(z,o) || ZoneWallStands(z,o) || w.terrain->wet[o] || !w.terrain->SameGround(o,v)){
            continue;
        }
        if (best < 0 || o < best){
            best = o;
        }
    }
    return (best >= 0) ? best : v;
}

/*
    The plot of building `id` nearest a point, the lower index on a tie; -1 if it has none. A FIELD has no
    plots of its own - it lies on coarse cells - so it is reached at its middle vertex (home_plot, the centre
    of its first cell): its farmer, the hungry and its builders all go there. Without this its harvest went
    untouched - the farmer's way to plot -1 failed, and the hungry passed it by (found 2026-10-07).
*/
int Economy::NearestPlot(uint32_t id, const vec2& to, bool f_dry) const{
    if (id >= plots_of.size()){
        return -1;
    }
    if (plots_of[id].empty()){
        return (id < home_plot.size()) ? home_plot[id] : -1;
    }
    int plot = -1;
    float best = 1e30f;
    for (int v : plots_of[id]){
        if (f_dry && state.world->terrain->wet[v]){
            continue;       //a bridge's span: its wood is brought to its ends, on the banks
        }
        float d = (state.world->grid->fine.pos[v] - to).length();
        if (d < best){
            best = d;
            plot = v;
        }
    }
    return plot;
}

/*
    An idle person's errand: of every site that still wants wood and every building with wood to spare
    - a store, the camp's supplies, a woodcutter's pile - the pair with the shortest walk, by the
    straight line, from him to the wood and on to the site (the lower ids on a tie), in the player's
    order where the walks are close (below). The first of the nearest few he can walk to, and from whose
    wood the site can be walked to.
*/
bool Economy::FindSiteWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),state.site.size());
    std::vector<uint32_t> sites;
    std::vector<uint32_t> sources;
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0){
            continue;
        }
        //A building's storeys still to stand, or a field's cells still a site (fields_plan.md).
        if ((site_storeys[id] > 0 || site_cells[id] > 0) && SiteLeft(z,(uint32_t)id,&k) > 0){
            sites.push_back((uint32_t)id);
        }
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP || b.kind == ZONE_KIND_WOODCUTTER;
        if (f_holds && stands[id] && WoodFree((uint32_t)id) > 0){
            sources.push_back((uint32_t)id);
        }
    }
    //The gardens, lots and walls still to build, after the buildings - so on a tie a building comes first.
    std::vector<int> trees;
    for (int v : ground_sites){
        uint32_t site = ECONOMY_SITE_GROUND | (uint32_t)v;
        //A wall's wood waits for its way to be cleared - the line work below (line_works_plan.md, step 3).
        if (z.ground[v] == ZONE_GROUND_WALL){
            RoadTrees(z,v,trees);
            if (!trees.empty()){
                continue;
            }
        }
        if (SiteLeft(z,site,&k) > 0){
            sites.push_back(site);
        }
    }
    struct Errand{
        float walk;
        uint32_t site, from;
        int from_plot;
        uint32_t key;       //its place in the placing order: a building's id, a road's or wall's line - 0 none
        int job;            //a piece of line work, its index in `jobs`; -1 for a carry
        bool operator<(const Errand& o) const{
            return (walk != o.walk) ? walk < o.walk : (site != o.site) ? site < o.site : from < o.from;
        }
    };
    std::vector<Errand> found;
    auto site_plot = [&](uint32_t s, const vec2& to){
        return EconomyIsGroundSite(s) ? GroundSiteApproach(z,EconomyGroundSitePlot(s)) : NearestPlot(s,to,true);
    };
    for (uint32_t s : sites){
        for (uint32_t f : sources){
            int fp = SourcePlot(f,k.pos);
            int sp = site_plot(s,w.grid->fine.pos[fp]);
            float walk = (w.grid->fine.pos[fp] - k.pos).length() + (w.grid->fine.pos[sp] - w.grid->fine.pos[fp]).length();
            uint32_t key = EconomyIsGroundSite(s) ? z.line[EconomyGroundSitePlot(s)] : s;
            found.push_back(Errand{walk,s,f,fp,key,-1});
        }
    }
    //Line work - a tree in a road's or wall's way, a road plot to lay - in the same ranking, by the walk to it
    //and its line's place (Economy.h, THE QUEUE): no longer only what is left when there is no wood to carry.
    std::vector<LineJob> jobs;
    LineJobs(k,z,false,jobs);
    for (size_t j = 0; j < jobs.size(); j++){
        found.push_back(Errand{jobs[j].walk,ECONOMY_SITE_GROUND | (uint32_t)jobs[j].plot,(uint32_t)j,-1,
                               z.line[jobs[j].plot],(int)j});
    }
    if (found.empty()){
        return false;
    }
    std::sort(found.begin(),found.end());
    /*
        The player's say (construction_plan.md, "Which site goes first"): errands to a site PUT FIRST before
        all others, however far; then, in each of the two, those within ECONOMY_QUEUE_SLACK of its shortest
        in placing order - a building's id or a line's, from one counter - so the queue orders only what is
        near enough for the walk not to matter, and a site clearly nearer still wins. A garden or lot has no
        place in the queue: it keeps its place by distance, and the rest round it are reordered in the
        places left. A line is never put first.
    */
    auto put_first = [&](const Errand& e){
        return e.job < 0 && !EconomyIsGroundSite(e.site) && z.buildings[e.site].f_priority;
    };
    //Before even those: a builder's own hut, while it is still to build - what he was taken on for
    //(builders_plan.md), since with everyone at work nobody else may come to build it.
    auto own_hut = [&](const Errand& e){
        return e.job < 0 && k.job == WORKER_JOB_BUILDER && e.site == k.building;
    };
    size_t n_own = std::stable_partition(found.begin(),found.end(),own_hut) - found.begin();
    size_t n_first = std::stable_partition(found.begin() + n_own,found.end(),put_first) - found.begin();
    auto queue = [&](size_t a, size_t b){
        std::vector<size_t> slots;
        std::vector<Errand> queued;
        for (size_t i = a; i < b && found[i].walk <= found[a].walk + ECONOMY_QUEUE_SLACK; i++){
            if (found[i].key){
                slots.push_back(i);
                queued.push_back(found[i]);
            }
        }
        std::sort(queued.begin(),queued.end(),[](const Errand& x, const Errand& y){
            return (x.key != y.key) ? x.key < y.key : (x.walk != y.walk) ? x.walk < y.walk
                 : (x.site != y.site) ? x.site < y.site : x.from < y.from;
        });
        for (size_t i = 0; i < slots.size(); i++){
            found[slots[i]] = queued[i];
        }
    };
    queue(n_own,n_first);
    queue(n_first,found.size());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        const Errand& e = found[t];
        if (e.job >= 0){
            if (StartLineJob(k,z,walkers,jobs[e.job],false)){
                return true;
            }
            continue;
        }
        vec2 at = w.grid->fine.pos[e.from_plot];
        if (!CanWalk(z,walkers,at,site_plot(e.site,at))){
            continue;
        }
        if (RouteTo(k,z,walkers,e.from_plot,at)){
            k.site = e.site;
            k.store = e.from;
            k.prop = -1;
            k.state = WORKER_TO_FETCH;
            return true;
        }
    }
    return false;
}

void Economy::RaiseWall(const ZoneState& z, int v){
    if (!ZoneWallPlanned(z,v) || z.storeys[v] > 0 || std::count(ground_raises.begin(),ground_raises.end(),v)
        || state.ground_site[v] < ECONOMY_WALL_WOOD){
        return;
    }
    std::vector<int> trees;
    RoadTrees(z,v,trees);
    if (!trees.empty()){
        return;     //a neighbour drawn since brought a tree into its way: up once that is felled
    }
    state.ground_site[v] -= ECONOMY_WALL_WOOD;
    ground_raises.push_back(v);     //it stands: the app raises it this tick (ZONE_OP_GROUND_RAISE)
    state.version++;
}

/*
    A builder has finished with his load: every storey the site's wood now pays for stands - the lowest
    first, over the whole building, so it rises level by level; the lower plot index on a tie. Raised
    through the zones by the app, on this tick (Raises).
*/
void Economy::Build(const ZoneState& z, uint32_t id){
    //A garden's wall or a lot's fence goes up at once, when all its wood is there - a palisade's too, with
    //nothing of its way standing (RaiseWall).
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        if (!SiteAlive(z,id) || std::count(ground_raises.begin(),ground_raises.end(),v)){
            return;
        }
        if (z.ground[v] == ZONE_GROUND_WALL){
            RaiseWall(z,v);
            return;
        }
        int cost = EconomyGroundWood(*state.world,v);
        if (state.ground_site[v] >= cost){
            state.ground_site[v] -= cost;
            ground_raises.push_back(v);
            state.version++;
        }
        return;
    }
    if (id == 0 || id >= plots_of.size() || id >= state.site.size()){
        return;
    }
    //A field site goes down all at once, every cell of it, when its wood is all there (fields_plan.md).
    if (id < site_cells.size() && site_cells[id] > 0){
        int cost = site_cells[id] * ECONOMY_FIELD_WOOD_PER_CELL;
        bool f_raised = false;
        for (int c : cells_of[id]){
            f_raised = f_raised || std::count(field_raises.begin(),field_raises.end(),c) > 0;
        }
        if (!f_raised && state.site[id] >= cost){
            state.site[id] -= cost;
            for (int c : cells_of[id]){
                if (!z.field_built[c]){
                    field_raises.push_back(c);
                }
            }
            state.version++;
        }
        return;
    }
    for (;;){
        int best = -1;
        int best_level = 0;
        float best_shore = 0.0f;
        bool f_bridge = z.buildings[id].kind == ZONE_KIND_BRIDGE;
        for (int v : plots_of[id]){
            int level = z.standing[v] + (int)std::count(raises.begin(),raises.end(),v);
            if (level >= z.storeys[v]){
                continue;
            }
            //A bridge from its banks out over the water: the plot nearest dry land first.
            float shore = f_bridge ? state.world->terrain->WaterEdgeDistance(state.world->grid->fine.pos[v]) : 0.0f;
            if (best < 0 || level < best_level || (level == best_level && shore > best_shore)){
                best = v;
                best_level = level;
                best_shore = shore;
            }
        }
        if (best < 0){
            return;
        }
        int cost = EconomyStoreyWood(*state.world,z,best);
        if (state.site[id] < cost){
            return;
        }
        state.site[id] -= cost;
        raises.push_back(best);
        state.version++;
    }
}

//--- Road works (docs/line_works_plan.md) ------------------------------------------------------------

namespace {

//The verge a planned road's trees are felled from: the view's clearance (RoadNear hides what is nearer a
//laid road) and a little over, for the curve the drawn road takes inside a corner of its edges.
#define ECONOMY_ROAD_VERGE  (ROAD_PROP_CLEARANCE + 0.4f)

float SegmentDistance2D(const vec2& p, const vec2& a, const vec2& b){
    vec2 d = b - a;
    float len2 = d.dot(d);
    float t = (len2 > 1e-9f) ? std::max(0.0f,std::min(1.0f,(p - a).dot(d) / len2)) : 0.0f;
    return (p - (a + d * t)).length();
}

}

/*
    The standing trees in the way of road plot v: within ECONOMY_ROAD_VERGE of its edges toward the road
    plots beside it - planned or laid - and standing bridges, or of its vertex alone, and on its own
    ground (not up or down a cliff). The edges WHOLE, not halves to their midpoints: the neighbour's half is
    felled for it too, which costs nothing, and leaves no tree under a road drawn later from that side.
    A wall plot's the same, along its edges toward the wall plots and gates beside it - its palisade's line
    (RoadMesh.h, WALLS). In bucket and index order, so the same trees in the same order every run.
*/
void Economy::RoadTrees(const ZoneState& z, int v, std::vector<int>& out) const{
    out.clear();
    const ChasmWorld& w = *state.world;
    if (!w.forest || tree_nx == 0 || v < 0 || v >= (int)z.ground.size()){
        return;
    }
    const std::vector<vec2>& pos = w.grid->fine.pos;
    const vec2 c = pos[v];
    std::vector<vec2> ends;
    float reach = 0.0f;
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    bool f_wall = z.ground[v] == ZONE_GROUND_WALL;
    for (int i = 0; i < n; i++){
        int u = nb[i];
        bool f_line = f_wall ? (z.ground[u] == ZONE_GROUND_WALL || z.gate[u])
                             : (z.ground[u] == ZONE_GROUND_ROAD || ZoneBridgeWalkable(z,u));
        if (f_line){
            ends.push_back(pos[u]);
            reach = std::max(reach,(pos[u] - c).length());
        }
    }
    if (ends.empty()){
        ends.push_back(c);
    }
    reach += ECONOMY_ROAD_VERGE;
    const std::vector<PropInstance>& props = w.forest->props;
    int x0 = std::max(0,(int)((c.x - reach - tree_origin.x) / tree_cell));
    int x1 = std::min(tree_nx - 1,(int)((c.x + reach - tree_origin.x) / tree_cell));
    int z0 = std::max(0,(int)((c.y - reach - tree_origin.y) / tree_cell));
    int z1 = std::min(tree_nz - 1,(int)((c.y + reach - tree_origin.y) / tree_cell));
    for (int bz = z0; bz <= z1; bz++){
        for (int bx = x0; bx <= x1; bx++){
            int b = bz * tree_nx + bx;
            for (int j = tree_start[b]; j < tree_start[b + 1]; j++){
                int i = tree_items[j];
                const PropInstance& pr = props[i];
                if (state.prop_state[i] != PROP_STATE_STANDING || pr.plot < 0 || !w.terrain->SameGround(pr.plot,v)){
                    continue;
                }
                vec2 p(pr.pos.x,pr.pos.z);
                for (const vec2& e : ends){
                    if (SegmentDistance2D(p,c,e) < ECONOMY_ROAD_VERGE){
                        out.push_back(i);
                        break;
                    }
                }
            }
        }
    }
}

/*
    Line work (line_works_plan.md): of the plots of planned roads and walls, the nearest him by the straight
    line (the lower index on a tie) - from home, any; on a stint, only within ECONOMY_ROAD_REACH of where he
    stands, so he works along the line rather than across the map. At each, the piece to do: the nearest
    standing tree in its way that nobody is after; with none left standing, a road's plot itself to lay, if
    nobody is laying it - a wall's has nothing to lay, it waits for its wood (FindSiteWork). The first few
    pieces; a tree is walked to the foot of, on his side.
*/
void Economy::LineJobs(const EconomyWorker& k, const ZoneState& z, bool f_stint, std::vector<LineJob>& out) const{
    out.clear();
    if (road_sites.empty() && wall_sites.empty()){
        return;
    }
    const ChasmWorld& w = *state.world;
    const std::vector<vec2>& pos = w.grid->fine.pos;
    std::vector<int> taken_trees;
    std::vector<int> taken_plots;
    for (const EconomyWorker& o : state.workers){
        if (&o == &k){
            continue;
        }
        if (o.prop >= 0){
            taken_trees.push_back(o.prop);      //a woodcutter's, or another road worker's
        }
        if ((o.state == WORKER_TO_ROAD && o.prop < 0) || o.state == WORKER_LAYING){
            taken_plots.push_back(EconomyGroundSitePlot(o.site));
        }
    }
    std::vector<std::pair<float,int>> found;
    auto consider = [&](int v){
        if (std::count(ground_raises.begin(),ground_raises.end(),v)){
            return;         //laid or raised this tick; the zones have not caught up yet
        }
        float d = (pos[v] - k.pos).length();
        if (!f_stint || d <= ECONOMY_ROAD_REACH){
            found.push_back(std::make_pair(d,v));
        }
    };
    for (int v : road_sites){
        consider(v);
    }
    for (int v : wall_sites){
        consider(v);
    }
    std::sort(found.begin(),found.end());
    std::vector<int> trees;
    for (size_t t = 0; t < found.size() && t < 12 && out.size() < 4; t++){
        int v = found[t].second;
        RoadTrees(z,v,trees);
        int tree = -1;
        float best = 1e30f;
        for (int i : trees){
            if (std::find(taken_trees.begin(),taken_trees.end(),i) != taken_trees.end()){
                continue;
            }
            const PropInstance& pr = w.forest->props[i];
            float d = (vec2(pr.pos.x,pr.pos.z) - k.pos).length();
            if (d < best){
                best = d;
                tree = i;
            }
        }
        int to_plot = v;
        vec2 to = pos[v];
        if (tree >= 0){
            const PropInstance& pr = w.forest->props[tree];
            vec2 at(pr.pos.x,pr.pos.z);
            vec2 d = at - k.pos;
            float l = d.length();
            to = (l > 0.8f) ? at - d * (0.6f / l) : at;
            to_plot = pr.plot;
        }else if (!trees.empty() || z.ground[v] != ZONE_GROUND_ROAD
                  || std::find(taken_plots.begin(),taken_plots.end(),v) != taken_plots.end()){
            continue;   //its trees all being felled by others, a wall cleared (its wood is a carry), or being laid
        }
        out.push_back(LineJob{found[t].first,v,tree,to_plot,to});
    }
}

bool Economy::StartLineJob(EconomyWorker& k, const ZoneState& z, Walkers& walkers, const LineJob& j, bool f_stint){
    if (!RouteTo(k,z,walkers,j.to_plot,j.to)){
        return false;
    }
    k.site = ECONOMY_SITE_GROUND | (uint32_t)j.plot;
    k.prop = j.tree;
    k.store = 0;
    k.state = WORKER_TO_ROAD;
    if (!f_stint){
        k.stint = 0;
    }
    return true;
}

//The line work alone - for a stint along the line, or when nothing else in the ranking could be walked to.
bool Economy::FindRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_stint){
    std::vector<LineJob> jobs;
    LineJobs(k,z,f_stint,jobs);
    for (const LineJob& j : jobs){
        if (StartLineJob(k,z,walkers,j,f_stint)){
            return true;
        }
    }
    return false;
}

/*
    A piece of road work done (or found done by others): the next along the line while his trip lasts, home
    after - and home at once if he was given a workplace meanwhile (a builder stays on), as a carrier
    finishes his errand and stops, or once he needs food or water (GoHome sends him for it).
*/
void Economy::NextRoadWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    k.prop = -1;
    if ((k.job == WORKER_JOB_NONE || k.job == WORKER_JOB_BUILDER) && k.stint < ECONOMY_ROAD_STINT && !EconomyPressing(k)
        && FindRoadWork(k,z,walkers,true)){
        return;
    }
    k.site = 0;
    k.stint = 0;
    GoHome(k,z,walkers);
}

//--- Saves -----------------------------------------------------------------------------------------

EconomySaved Economy::Save() const{
    return EconomySaveState(state);
}

EconomySaved EconomySaveState(const EconomyState& state){
    EconomySaved s;
    for (size_t id = 1; id < state.stock.size(); id++){
        if (CarryTotal(state.stock[id]) > 0){
            s.stocks.push_back(std::make_pair((uint32_t)id,state.stock[id]));
        }
    }
    for (size_t i = 0; i < state.prop_state.size(); i++){
        if (state.prop_state[i] != PROP_STATE_STANDING){
            s.props.push_back(std::make_pair((int)i,(int)state.prop_state[i]));
        }
    }
    s.workers = state.workers;
    s.fields = state.fields;
    for (size_t id = 1; id < state.site.size(); id++){
        if (state.site[id] > 0){
            s.sites.push_back(std::make_pair((uint32_t)id,state.site[id]));
        }
    }
    for (size_t v = 0; v < state.ground_site.size(); v++){
        if (state.ground_site[v] > 0){
            s.ground_sites.push_back(std::make_pair((int)v,state.ground_site[v]));
        }
    }
    s.next_person = state.next_person;
    s.dead = state.dead;
    return s;
}

/*
    Put back exactly as saved - the workers on their routes, not planned again (a re-plan could find
    another route of the same cost, and a replay has to be the same walk). Then what follows from the
    zones is worked out, with no stock shared out: the save's already is.
*/
void Economy::Restore(std::shared_ptr<const ChasmWorld> world, const EconomySaved& saved, const ZoneState& z){
    Reset(world);
    if (!world || z.world != world){
        return;
    }
    state.stock.assign(z.buildings.size(),std::array<int,GOOD_COUNT>{});
    for (const auto& st : saved.stocks){
        if (st.first < state.stock.size()){
            state.stock[st.first] = st.second;
        }
    }
    for (const auto& pr : saved.props){
        if (pr.first >= 0 && pr.first < (int)state.prop_state.size()){
            state.prop_state[pr.first] = (uint8_t)pr.second;
        }
    }
    /*
        The people as saved. A save from before P4 has only stand-in workers, with no person ids: they
        are dropped, and the colony starts with its settlers at the camp, as a new one does.
    */
    bool f_people = !saved.workers.empty();
    for (const EconomyWorker& k : saved.workers){
        f_people = f_people && k.id != 0;
    }
    if (f_people){
        state.workers = saved.workers;
        state.next_person = saved.next_person;
        state.dead = saved.dead;
    }
    state.fields = saved.fields;
    state.site.assign(z.buildings.size(),0);
    for (const auto& st : saved.sites){
        if (st.first < state.site.size()){
            state.site[st.first] = st.second;
        }
    }
    state.ground_site.assign(z.ground.size(),0);
    for (const auto& st : saved.ground_sites){
        if (st.first >= 0 && st.first < (int)state.ground_site.size()){
            state.ground_site[st.first] = st.second;
        }
    }
    f_new_colony = false;       //a saved colony has had its supplies
    Derive(z,false);
    /*
        Land never settled (land_plan.md): a save from before land - its fields have no owner, its farmers work
        a field. The first tick settles it. Only such a save: a field with no owner is otherwise one nobody can
        reach, and settling a save of today's again would part a replay from the run it was cut from.
    */
    f_resettle = saved.f_before_land;
    for (const EconomyWorker& k : state.workers){
        f_resettle = f_resettle || (k.job == WORKER_JOB_FARMER
                                    && (k.building >= z.buildings.size() || z.buildings[k.building].kind == ZONE_KIND_FIELD));
    }
    state.felled_version++;
    state.version++;
}

//--- Questions -------------------------------------------------------------------------------------

uint8_t EconomyAccepts(const EconomyState& e, uint32_t id){
    return (id < e.accepts.size()) ? e.accepts[id] : 0;
}

int EconomyStock(const EconomyState& e, uint32_t id, int good){
    return (id < e.stock.size() && good >= 0 && good < GOOD_COUNT) ? e.stock[id][good] : 0;
}

int EconomyStockTotal(const EconomyState& e, uint32_t id){
    return (id < e.stock.size()) ? CarryTotal(e.stock[id]) : 0;
}

std::array<int,GOOD_COUNT> EconomyStoredTotals(const EconomyState& e, const ZoneState& z){
    std::array<int,GOOD_COUNT> t{};
    for (size_t id = 1; id < e.stock.size() && id < z.buildings.size(); id++){
        //Stores, and the camp's supplies - what the colony has to hand.
        if (z.buildings[id].kind == ZONE_KIND_STORE || z.buildings[id].kind == ZONE_KIND_CAMP){
            for (int g = 0; g < GOOD_COUNT; g++){
                t[g] += e.stock[id][g];
            }
        }
    }
    return t;
}

int EconomyCropYield(int last, int crop){
    if (crop < 0 || crop >= ZONE_CROP_COUNT){
        return 0;
    }
    return (last < 0 || last >= ZONE_CROP_COUNT) ? 100 : crop_after[last][crop];
}

int EconomyAutoCrop(int last){
    int best = 0;
    for (int c = 1; c < ZONE_CROP_COUNT; c++){
        if (EconomyCropYield(last,c) > EconomyCropYield(last,best)){
            best = c;
        }
    }
    return best;
}

int EconomyFieldSowCrop(const EconomyField& f, const ZoneBuilding& b){
    if (f.last < 0){
        return (b.crop < ZONE_CROP_COUNT) ? (int)b.crop : -1;    //its first: the player's choice, or nothing yet
    }
    return (b.next_crop < ZONE_CROP_COUNT) ? (int)b.next_crop : EconomyAutoCrop(f.last);
}

const EconomyField* EconomyFieldOf(const EconomyState& e, uint32_t id){
    for (const EconomyField& f : e.fields){
        if (f.building == id){
            return &f;
        }
    }
    return NULL;
}

float EconomyFieldGrowth(const EconomyState& e, const ZoneState& z, uint32_t id){
    (void)z;
    for (const EconomyField& f : e.fields){
        if (f.building == id){
            if (!f.f_sown || f.crop < 0 || f.crop >= ZONE_CROP_COUNT){
                return 0.0f;    //empty
            }
            return std::min(1.0f,(float)f.grown / ((float)crop_days[f.crop] * CALENDAR_DAY_TICKS * 100.0f));
        }
    }
    return 0.0f;
}

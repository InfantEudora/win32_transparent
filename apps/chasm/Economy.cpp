#include "Economy.h"
#include "Calendar.h"
#include "ChasmWorld.h"
#include "Walkers.h"
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
        default:                return "?";
    }
}

const char* EconomyJobName(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return "woodcutter";
        case WORKER_JOB_FARMER:     return "farmer";
        case WORKER_JOB_WATER:      return "water_carrier";
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

int EconomyJobSkill(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return SKILL_STRENGTH;
        case WORKER_JOB_FARMER:     return SKILL_HUSBANDRY;
        case WORKER_JOB_WATER:      return SKILL_INGENUITY;
        default:                    return SKILL_STRENGTH;
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
    over a growing year greens come in about four times, wheat twice, beans once or twice. Food values
    and keeping come with eating (P5).
*/
const int crop_days[ZONE_CROP_COUNT] = {18,8,25};
const float crop_yield[ZONE_CROP_COUNT] = {0.6f,0.35f,0.45f};

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
    switch (kind){
        case ZONE_KIND_WOODCUTTER:  return WORKER_JOB_WOODCUTTER;
        case ZONE_KIND_FIELD:       return WORKER_JOB_FARMER;
        case ZONE_KIND_WATER:       return WORKER_JOB_WATER;
        default:                    return -1;
    }
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
            //A camp is open ground between tents, and a standing bridge a deck over the water.
            bool f_bridge = ZoneBridgeWalkable(z,v);
            if ((t.wet[v] && !f_bridge) || t.Mountain(v) || (z.storeys[v] > 0 && z.KindOf(v) != ZONE_KIND_CAMP && !f_bridge)
                || AllInField(w,z,v)){
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
    WORKERS INTO WORKPLACES. A worker whose workplace is gone, or who has lost his house, is free again.
    Then each workplace with nobody, in id order, takes the free housed adult with the best score: his
    skill for the job, worth ECONOMY_SKILL_DISTANCE world units a point, less the straight line from his
    house to the workplace. The lower id on a tie. Once given, a job stays his.
*/
void Economy::AssignJobs(const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = z.buildings.size();
    std::vector<uint8_t> staffed(n,0);
    for (EconomyWorker& k : state.workers){
        bool f_keep = k.building && k.house && k.building < n && z.buildings[k.building].size > 0
                      && JobOf(z.buildings[k.building].kind) == k.job && home_plot[k.building] >= 0 && stands[k.building];
        if (!f_keep && k.job != WORKER_JOB_NONE){
            k.job = WORKER_JOB_NONE;
            k.building = 0;
            k.prop = -1;
            //What he was carrying goes with the job; he goes home.
            k.carry = 0;
            k.carry_good = -1;
            if (k.state != WORKER_AT_HOME && k.state != WORKER_TO_HOME){
                GoHome(k,z,walkers);
            }
        }
        if (k.job != WORKER_JOB_NONE){
            staffed[k.building] = 1;
        }
    }
    for (size_t id = 1; id < n; id++){
        int job = (z.buildings[id].size > 0) ? JobOf(z.buildings[id].kind) : WORKER_JOB_NONE;
        if (job == WORKER_JOB_NONE || staffed[id] || home_plot[id] < 0 || !stands[id]){
            continue;       //a workplace still a site is not worked yet
        }
        vec2 at = w.grid->fine.pos[home_plot[id]];
        int skill = EconomyJobSkill(job);
        EconomyWorker* best = NULL;
        float best_score = -1e30f;
        for (EconomyWorker& k : state.workers){
            if (k.job != WORKER_JOB_NONE || !k.house || HomePlot(k) < 0){
                continue;
            }
            float score = k.skills[skill] * ECONOMY_SKILL_DISTANCE - (w.grid->fine.pos[HomePlot(k)] - at).length();
            //Only someone who can walk there from his house - a river with no bridge parts a village.
            if (score > best_score && CanWalk(z,walkers,w.grid->fine.pos[HomePlot(k)],home_plot[id])){
                best_score = score;
                best = &k;
            }
        }
        if (best){
            best->job = job;
            best->building = (uint32_t)id;
            staffed[id] = 1;
            state.version++;
        }
    }
}

/*
    From his house to his workplace to fetch what it has made - the hut's pile, the field's harvest, the
    collector's water - when a store will take a load of it. True if he set off.
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
    if (RouteTo(k,z,walkers,plot,state.world->grid->fine.pos[plot])){
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
    for (size_t c = 0; c < z.field.size(); c++){
        uint32_t id = z.field[c];
        if (id && first_cell[id] < 0){
            first_cell[id] = (int)c;
        }
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
        The gardens and lots still to build, and the wood brought to each: a plot that is no longer a
        site - erased, built over, or finished - keeps none (what was left over is spent).
    */
    state.ground_site.resize(z.ground.size(),0);
    ground_sites.clear();
    for (size_t v = 0; v < z.ground.size(); v++){
        if (ZoneEnclosesGround(z.ground[v]) && !z.ground_built[v] && z.storeys[v] == 0){
            ground_sites.push_back((int)v);
        }else{
            state.ground_site[v] = 0;
        }
    }
    //What stands of each building, and what is still to build (Zones.h, CONSTRUCTION).
    stands.assign(n,0);
    site_storeys.assign(n,0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind == ZONE_KIND_FIELD){
            stands[id] = (z.buildings[id].size > 0) ? 1 : 0;
            continue;
        }
        for (int v : plots_of[id]){
            stands[id] = stands[id] || z.standing[v] > 0;
            site_storeys[id] += z.storeys[v] - z.standing[v];
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
                for (int i = 0; i < p.PlotQuadCount(nb[j]); i++){
                    uint32_t f = z.field[g.fine.quads[p.PlotQuadCorner(nb[j],i) / 4].parent];
                    if (f){
                        attached |= GOOD_BIT(GoodOfCrop(z.buildings[f].crop));
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
        A woodcutter's woodpile: on a lot that stands beside his hut - of all his plots' lots, the lowest
        plot index (ZoneLotBeside), so the view puts the logs where he puts them.
    */
    state.pile_plot.assign(n,-1);
    pile_door.assign(n,-1);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_WOODCUTTER || z.buildings[id].size <= 0){
            continue;
        }
        for (int v : plots_of[id]){
            int lot = ZoneLotBeside(w,z,v,true);
            if (lot >= 0 && (state.pile_plot[id] < 0 || lot < state.pile_plot[id])){
                state.pile_plot[id] = lot;
            }
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
            EconomyField f;
            f.building = (uint32_t)id;
            f.crop = z.buildings[id].crop;
            fields.push_back(f);
        }
    }
    state.fields.swap(fields);
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
        route.push_back(k.pos);
        for (size_t i = 0; i < plots.size(); i++){
            vec2 pt = w.grid->fine.pos[plots[i]];
            if ((pt - route.back()).length() > 1e-4f){
                //The step from where he stands onto the first plot is open ground; after that, the edges'.
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
            && k.state != WORKER_TO_FETCH && k.state != WORKER_TO_SITE){
            continue;
        }
        if (Arrived(k)){
            continue;
        }
        vec2 goal = k.route.back();
        GridPick at = state.world->picker->Pick(goal);
        int goal_plot = at.f_hit ? at.plot : -1;
        if (k.state == WORKER_TO_WORK && k.prop >= 0){
            goal_plot = state.world->forest->props[k.prop].plot;
        }else if (k.state == WORKER_TO_STORE && k.store < plots_of.size() && !plots_of[k.store].empty()){
            //Re-chosen at arrival if it no longer takes his load; until then, the plot he was making for.
            goal_plot = at.f_hit ? at.plot : plots_of[k.store].front();
        }else if (k.state == WORKER_TO_HOME){
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

void Economy::GoHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    int home = HomePlot(k);
    k.prop = -1;
    k.store = 0;
    //In a camp, home is in front of his tent, where he can be seen; in a house, its door.
    bool f_camp = k.house && k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_CAMP;
    vec2 at = (home >= 0) ? (f_camp ? ZoneCampDoor(*state.world,home) : state.world->grid->fine.pos[home]) : vec2();
    if (home >= 0 && RouteTo(k,z,walkers,home,at)){
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
            //Not home yet - his way there was cut (a river, a wall): he waits until the zones change.
            if (!AtHome(k)){
                break;
            }
            if (k.job == WORKER_JOB_NONE){
                //Nothing of his own to do: he carries wood to a construction that wants it.
                FindSiteWork(k,z,walkers);
                break;
            }
            if (!k.house){
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
                k.state = WORKER_AT_HOME;
                //In at his door - but never into a tent: a camp's people stay where they can be seen.
                k.f_indoors = k.house != 0 && k.house < z.buildings.size() && z.buildings[k.house].kind == ZONE_KIND_HOUSE;
            }
            break;
        case WORKER_HOLDING:
            //Holding a load: a store that takes it, or room on his pile, whichever comes first.
            if (f_look){
                deliver();
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
        default:
            k.state = WORKER_AT_HOME;
            break;
    }
}

void Economy::Tick(const ZoneState& z, Walkers& walkers, uint64_t tick, float dt){
    if (!state.world || z.world != state.world){
        return;
    }
    const ChasmWorld& w = *state.world;
    raises.clear();
    ground_raises.clear();
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
    //Who works where, by workplace id: a field grows and a collector fills only while somebody works it.
    std::vector<float> pace(z.buildings.size(),0.0f);
    for (const EconomyWorker& k : state.workers){
        if (k.building && k.building < pace.size()){
            pace[k.building] = EconomySkillFactor(k.skills[EconomyJobSkill(k.job)]);
        }
    }
    CalendarDate date = CalendarDateOf(tick);
    //The fields grow in any weather but winter's and the snow's, while tended, at their farmer's pace.
    for (EconomyField& f : state.fields){
        int crop = z.buildings[f.building].crop;
        if (f.crop != crop){
            //Another crop sown: it grows from nothing, whatever the last had reached.
            f.crop = crop;
            f.grown = 0;
        }
        int home = home_plot[f.building];
        if (home < 0 || pace[f.building] <= 0.0f || date.season == SEASON_WINTER || UnderSnow(w,w.grid->fine.pos[home],tick)){
            continue;
        }
        f.grown += (int)std::lround(pace[f.building] * 100.0f);
        if (f.grown >= crop_days[crop] * CALENDAR_DAY_TICKS * 100){
            float area = ZoneBuildingFigures(w,z,f.building).floor_area;
            state.stock[f.building][GoodOfCrop(crop)] += (int)std::floor(area * crop_yield[crop]);
            f.grown = 0;
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
    if (!state.workers.empty()){
        state.version++;    //they move every tick - the view redraws them
    }
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

int EconomyGroundBrought(const EconomyState& e, int plot){
    return (plot >= 0 && plot < (int)e.ground_site.size()) ? e.ground_site[plot] : 0;
}

//A site that is still one: a building that has not gone, or a garden or lot plot still to build.
bool Economy::SiteAlive(const ZoneState& z, uint32_t id) const{
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        return v >= 0 && v < (int)z.ground.size() && ZoneEnclosesGround(z.ground[v]) && !z.ground_built[v]
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
        return SiteAlive(z,id) ? EconomyGroundWood(*state.world,EconomyGroundSitePlot(id)) : 0;
    }
    if (id >= plots_of.size()){
        return 0;
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

//Where wood is taken from building `id`: a woodcutter's on his pile, the rest at its plot nearest `to`.
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
        if (z.storeys[o] > 0 || ZoneGroundStands(z,o) || w.terrain->wet[o] || !w.terrain->SameGround(o,v)){
            continue;
        }
        if (best < 0 || o < best){
            best = o;
        }
    }
    return (best >= 0) ? best : v;
}

//The plot of building `id` nearest a point, the lower index on a tie; -1 if it has none.
int Economy::NearestPlot(uint32_t id, const vec2& to, bool f_dry) const{
    if (id >= plots_of.size()){
        return -1;
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
    straight line, from him to the wood and on to the site (the lower ids on a tie). The first of the
    nearest few he can walk to, and from whose wood the site can be walked to.
*/
bool Economy::FindSiteWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),state.site.size());
    std::vector<uint32_t> sites;
    std::vector<uint32_t> sources;
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0 || b.kind == ZONE_KIND_FIELD){
            continue;
        }
        if (site_storeys[id] > 0 && SiteLeft(z,(uint32_t)id,&k) > 0){
            sites.push_back((uint32_t)id);
        }
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP || b.kind == ZONE_KIND_WOODCUTTER;
        if (f_holds && stands[id] && WoodFree((uint32_t)id) > 0){
            sources.push_back((uint32_t)id);
        }
    }
    //The gardens and lots still to build, after the buildings - so on a tie a building comes first.
    for (int v : ground_sites){
        uint32_t site = ECONOMY_SITE_GROUND | (uint32_t)v;
        if (SiteLeft(z,site,&k) > 0){
            sites.push_back(site);
        }
    }
    if (sites.empty() || sources.empty()){
        return false;
    }
    struct Errand{
        float walk;
        uint32_t site, from;
        int from_plot;
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
            found.push_back(Errand{walk,s,f,fp});
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 3; t++){
        const Errand& e = found[t];
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

/*
    A builder has finished with his load: every storey the site's wood now pays for stands - the lowest
    first, over the whole building, so it rises level by level; the lower plot index on a tie. Raised
    through the zones by the app, on this tick (Raises).
*/
void Economy::Build(const ZoneState& z, uint32_t id){
    //A garden's wall or a lot's fence goes up at once, when all its wood is there.
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        if (!SiteAlive(z,id) || std::count(ground_raises.begin(),ground_raises.end(),v)){
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

float EconomyFieldGrowth(const EconomyState& e, const ZoneState& z, uint32_t id){
    for (const EconomyField& f : e.fields){
        if (f.building == id && id < z.buildings.size()){
            int crop = z.buildings[id].crop;
            if (f.crop != crop){
                return 0.0f;    //sown again this tick
            }
            return std::min(1.0f,(float)f.grown / ((float)crop_days[crop] * CALENDAR_DAY_TICKS * 100.0f));
        }
    }
    return 0.0f;
}

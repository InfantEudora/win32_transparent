#include "Economy.h"
#include "Calendar.h"
#include "ChasmWorld.h"
#include "Walkers.h"
#include "RoadMesh.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

const char* GoodName(int good){
    switch (good){
        case GOOD_WOOD:     return "wood";
        case GOOD_WATER:    return "water";
        case GOOD_WHEAT:    return "wheat";
        case GOOD_GREENS:   return "greens";
        case GOOD_BEANS:    return "beans";
        case GOOD_PROVISIONS: return "provisions";
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
        case WORKER_TO_PLAY:    return "to_play";
        case WORKER_PLAYING:    return "playing";
        case WORKER_TO_POST:    return "to_post";
        case WORKER_ON_POST:    return "on_post";
        case WORKER_TO_RANGE:   return "to_range";
        case WORKER_TRAINING:   return "training";
        case WORKER_TO_CRATE:   return "to_crate";
        case WORKER_LIFTING:    return "lifting";
        case WORKER_TO_INPUT:   return "to_input";
        case WORKER_TAKING_INPUT: return "taking_input";
        case WORKER_TO_WORKSHOP: return "to_workshop";
        case WORKER_MAKING:     return "making";
        case WORKER_TO_GET:     return "to_get";
        case WORKER_GETTING:    return "getting";
        case WORKER_TO_PANTRY:  return "to_pantry";
        default:                return "?";
    }
}

const char* EconomyJobName(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return "woodcutter";
        case WORKER_JOB_FARMER:     return "farmer";
        case WORKER_JOB_WATER:      return "water_carrier";
        case WORKER_JOB_BUILDER:    return "builder";
        case WORKER_JOB_ARCHER:     return "archer";
        case WORKER_JOB_GUARD:      return "guard";
        case WORKER_JOB_NONE:       return "none";
        case WORKER_JOB_PLAYING:    return "playing";
        case WORKER_JOB_BABY:       return "baby";
        case WORKER_JOB_CARING:     return "caring";
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
        case ZONE_KIND_RANGE:       return WORKER_JOB_ARCHER;      //range_plan.md: its trainees
        case ZONE_KIND_TOWER:       return WORKER_JOB_GUARD;       //defence_plan.md: its guards
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
        }else if (k.job == WORKER_JOB_NONE){
            none++;         //a child playing is not jobless
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
        case WORKER_JOB_ARCHER:     return SKILL_PRECISION;
        case WORKER_JOB_GUARD:      return SKILL_PRECISION;     //after archery (AssignJobs)
        default:                    return SKILL_STRENGTH;      //a woodcutter, a builder, and anyone carrying
    }
}

float EconomySkillFactor(int skill){
    return 0.6f + 0.08f * (float)std::max(1,std::min(10,skill));
}

//--- Lifelines (docs/lifeline_plan.md) -------------------------------------------------------------

int EconomyAgeAt(int64_t born, uint64_t tick){
    int64_t lived = (int64_t)tick - born;
    return (lived > 0) ? (int)(lived / CALENDAR_YEAR_TICKS) : 0;
}

int EconomyStageAt(int64_t born, uint64_t tick){
    int64_t lived = (int64_t)tick - born;
    return (lived < LIFE_BABY_TICKS) ? LIFE_BABY : (lived < LIFE_CHILD_TICKS) ? LIFE_CHILD
         : (lived < LIFE_YOUNG_TICKS) ? LIFE_YOUNG : LIFE_ADULT;
}

const char* EconomyStageName(int stage){
    switch (stage){
        case LIFE_BABY:     return "baby";
        case LIFE_CHILD:    return "child";
        case LIFE_YOUNG:    return "young adult";
        case LIFE_ADULT:    return "adult";
        default:            return "?";
    }
}

int EconomyBirthday(int64_t born){
    int64_t in_year = ((born % CALENDAR_YEAR_TICKS) + CALENDAR_YEAR_TICKS) % CALENDAR_YEAR_TICKS;
    return (int)(in_year / CALENDAR_DAY_TICKS);
}

std::string EconomyBirthdayName(int64_t born){
    int day = EconomyBirthday(born);
    return std::string(CalendarSeasonName(day / CALENDAR_DAYS_PER_SEASON)) + " "
           + std::to_string(day % CALENDAR_DAYS_PER_SEASON + 1);
}

int EconomyCoupleDay(uint32_t a, uint32_t b){
    uint32_t lo = std::min(a,b);
    uint32_t hi = std::max(a,b);
    return (int)(((lo * 2654435761u) ^ (hi * 2246822519u) ^ 0x5EEDu) % (uint32_t)(CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON));
}

int EconomyChildrenOf(const EconomyState& e, uint32_t mother){
    if (!mother){
        return 0;
    }
    int n = 0;
    for (const EconomyWorker& k : e.workers){
        n += (k.mother == mother) ? 1 : 0;
    }
    for (const EconomyDeath& d : e.dead){
        n += (d.mother == mother) ? 1 : 0;
    }
    return n;
}

namespace {

//A person's mother and father by id, among the living or the dead: the tree outlives the people in it.
void ParentsOf(const EconomyState& e, uint32_t id, uint32_t out[2]){
    out[0] = out[1] = 0;
    for (const EconomyWorker& k : e.workers){
        if (k.id == id){
            out[0] = k.mother;
            out[1] = k.father;
            return;
        }
    }
    for (const EconomyDeath& d : e.dead){
        if (d.id == id){
            out[0] = d.mother;
            out[1] = d.father;
            return;
        }
    }
}

}

bool EconomyRelated(const EconomyState& e, const EconomyWorker& a, const EconomyWorker& b){
    //Each with their parents and grandparents: any id the two lists share, 0 (nobody) aside. Each list starts
    //with the person, so one being the other's parent or grandparent is a shared id too.
    uint32_t up[2][7];
    const EconomyWorker* both[2] = {&a,&b};
    for (int s = 0; s < 2; s++){
        up[s][0] = both[s]->id;
        up[s][1] = both[s]->mother;
        up[s][2] = both[s]->father;
        ParentsOf(e,up[s][1],&up[s][3]);
        ParentsOf(e,up[s][2],&up[s][5]);
    }
    for (int i = 0; i < 7; i++){
        for (int j = 0; j < 7; j++){
            if (up[0][i] && up[0][i] == up[1][j]){
                return true;
            }
        }
    }
    return false;
}

int EconomyCoupleSurname(const EconomyState& e, uint32_t mother, uint32_t father){
    uint32_t lo = std::min(mother,father);
    uint32_t hi = std::max(mother,father);
    bool f_hi = ((((lo * 2246822519u) ^ (hi * 3266489917u) ^ 0x5A2Eu) >> 7) & 1u) != 0;
    //The one the hash picks, or - with one of them nobody (0) - the other.
    uint32_t pick = (f_hi && hi) || !lo ? hi : lo;
    for (const EconomyWorker& k : e.workers){
        if (k.id == pick){
            return k.surname;
        }
    }
    for (const EconomyDeath& d : e.dead){
        if (d.id == pick){
            return d.surname;
        }
    }
    return 0;
}

//--- Posts (docs/defence_plan.md) --------------------------------------------------------------------

int EconomyPostPlot(const EconomyState& e, const EconomyWorker& k){
    if (k.post >= 0){
        return k.post;
    }
    if (k.job == WORKER_JOB_GUARD && k.building && k.building < e.door.size()){
        return e.door[k.building];
    }
    return -1;
}

vec2 EconomyPostPoint(const EconomyState& e, const EconomyWorker& k){
    int plot = EconomyPostPlot(e,k);
    if (plot < 0 || !e.world || plot >= (int)e.world->grid->fine.pos.size()){
        return k.pos;
    }
    vec2 p = e.world->grid->fine.pos[plot];
    if (k.post >= 0){
        return p;
    }
    //A guard: the first of them by id on the west of the platform, the second on the east - so the two stand
    //apart, and one alone keeps his side when the other goes.
    int rank = 0;
    for (const EconomyWorker& o : e.workers){
        rank += (o.job == WORKER_JOB_GUARD && o.post < 0 && o.building == k.building && o.id < k.id) ? 1 : 0;
    }
    return p + vec2((rank % 2 == 0) ? -ECONOMY_TOWER_STAND : ECONOMY_TOWER_STAND,0.0f);
}

bool EconomyAtPost(const EconomyState& e, const EconomyWorker& k){
    return EconomyPostPlot(e,k) >= 0 && (k.pos - EconomyPostPoint(e,k)).length() < ECONOMY_POST_REACH;
}

uint32_t EconomyOnTower(const EconomyState& e, const ZoneState& z, const EconomyWorker& k){
    int plot = EconomyPostPlot(e,k);
    uint32_t b = (plot >= 0 && plot < (int)z.building.size()) ? z.building[plot] : 0;
    return (b && b < z.buildings.size() && z.buildings[b].kind == ZONE_KIND_TOWER && z.standing[plot] > 0
            && EconomyAtPost(e,k)) ? b : 0;
}

bool EconomyCanOrder(const EconomyWorker& k, uint64_t tick){
    return EconomyStageAt(k.born,tick) >= LIFE_YOUNG;
}

int EconomyPackFood(const EconomyWorker& k){
    int food = 0;
    for (int g = 0; g < GOOD_COUNT; g++){
        food += k.pack[g] * EconomyFoodWorth(g);
    }
    return food;
}

/*
    Names by id, from short lists: a first name by the person - a woman's or a man's - and a SURNAME of his
    own (`surname`): a settler's by his family, so a family shares its name; anyone born since, one of his
    parents' (COUPLES). Placeholders with the feel of a frontier village; nothing in the rules reads them.
    The lists are stepped through by a stride that shares no factor with their length, so the first
    NAME_COUNT people - and families - never share a name (two Catos on seed 12 had, when it was a hash).
*/
#define NAME_COUNT      30      //each first-name list's length
#define SURNAME_COUNT   28
static const char* surnames[SURNAME_COUNT] = {"Aldering","Brink","Dekker","Hoeve","Kamp","Molen","Rietveld","Smit",
                                              "Ter Horst","Veen","Visser","Wolters","Bakker","Boer","Bosch","De Wit",
                                              "Haverkamp","Hulst","Kuiper","Mulder","Postma","Schaap","Timmer",
                                              "Van Dijk","Verhoef","Vos","Wever","Zijlstra"};

int EconomySettlerSurname(uint32_t family){
    return (int)((family * 11u + 4u) % SURNAME_COUNT);
}

std::string EconomySurnameName(int surname){
    return surnames[(surname >= 0 ? surname : 0) % SURNAME_COUNT];
}

std::string EconomyPersonName(const EconomyWorker& k){
    static const char* women[NAME_COUNT] = {"Ada","Cato","Elin","Fenna","Hanne","Kaat","Lieve","Nora","Roos","Tess",
                                            "Anouk","Femke","Lotte","Mila","Sanne","Bente","Evi","Floor","Griet","Ilse",
                                            "Isa","Jet","Liv","Marit","Maud","Merel","Pien","Saar","Veerle","Wies"};
    static const char* men[NAME_COUNT] = {"Bram","Dirk","Gijs","Ivo","Joris","Maas","Otto","Pim","Sem","Wout",
                                          "Bas","Daan","Jesse","Noud","Teun","Aart","Bart","Cas","Dolf","Floris",
                                          "Gerrit","Hidde","Jelle","Kees","Luuk","Mees","Rik","Siem","Thijs","Wessel"};
    uint32_t a = (k.id * 13u + 9u) % NAME_COUNT;
    return std::string(k.f_female ? women[a] : men[a]) + " " + EconomySurnameName(k.surname);
}

namespace {

/*
    A crop's growing time, in days of growing weather, and its yield per unit of field area. Greens
    are fast and light, beans slow and rich, wheat between (gameplay_plan.md, "Goods and food"). Since
    the frost (fields_plan.md) a crop must ripen inside the season - 28 days by the camp, the snow gone
    on spring 3 - so wheat and beans were shortened (the user's, 2026-10-08: 18 and 25 before, beans
    never came through): at an average farmer's pace greens come in three times a season, wheat once
    (twice for a good farmer), beans once if sown in the first week or so after the thaw. What a unit
    is worth eaten is EconomyFoodWorth's (needs_plan.md). The yields were doubled the same day (the
    user's, from 0.6 / 0.35 / 0.45): ten people took about 37 cells, and now about 16 (needs_plan.md,
    "What the rates mean").
*/
const int crop_days[ZONE_CROP_COUNT] = {15,8,20};
const float crop_yield[ZONE_CROP_COUNT] = {1.2f,0.7f,0.9f};

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
    The settlers (lifeline_plan.md): three families, each a couple with their grown children, two of them with
    a child too - twelve people. Sizes 5, 4 and 3, so the first houses are a real choice: a house just big
    enough for the family of three is too small for the other two.
*/
struct SettlerFamily{
    int grown;          //grown children, besides the couple
    int children;       //children, playing
};
const SettlerFamily settler_families[] = {{2,1},{1,1},{1,0}};
//Their ages, from the seed: the couples', the grown children's, and the children's days short of growing up -
//so every child does in the colony's first year.
#define SETTLER_COUPLE_AGE      24      //to 32
#define SETTLER_COUPLE_SPAN     9
#define SETTLER_GROWN_AGE       5       //to 12
#define SETTLER_GROWN_SPAN      8
#define SETTLER_CHILD_DAYS      36
#define SETTLER_DUE_DAY         5       //the expecting settler's child: due from this day of the first year...
#define SETTLER_DUE_SPAN        16      //...over this many - before the first winter, so a baby is seen early

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

//A builders' hut that stands: where provisions are made (POSTS, Economy.h).
static bool Workshop(const ZoneState& z, const std::vector<uint8_t>& stands, uint32_t h){
    return h && h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_BUILDERS && h < stands.size() && stands[h];
}

//What a stock holds of water or food (`goods`) in ticks of a person's bar: a unit at what it is worth.
int64_t HeldTicks(const std::array<int,GOOD_COUNT>& s, GoodsMask goods){
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

/*
    RESERVED (EconomyReserved): what o, on his way to take from `id`, counts on of `good` - for a site, a carrier's
    load or a builder's for provisions (GoodFree leaves these out), and for his home, a load (FetchHome's spare
    leaves it out). The rules and the cards' striped goods count by the same two.
*/
static int SiteReservation(const EconomyWorker& o, uint32_t id, int good){
    if (o.store != id || o.carry_good != good){
        return 0;
    }
    if (o.site && (o.state == WORKER_TO_FETCH || o.state == WORKER_FETCHING)){
        return ECONOMY_LOAD;
    }
    //A builder on his way for wheat or beans for provisions: the load he set out for (MakeProvisions).
    return (o.state == WORKER_TO_INPUT || o.state == WORKER_TAKING_INPUT) ? o.stint : 0;
}

static int HomeReservation(const EconomyWorker& o, uint32_t id, int good){
    return (o.store == id && o.carry_good == good && (o.state == WORKER_TO_GET || o.state == WORKER_GETTING)) ? ECONOMY_LOAD : 0;
}

//Of `goods`, the one a stock holds most of, the lower on a tie; -1 for none.
int MostOf(const std::array<int,GOOD_COUNT>& s, GoodsMask goods){
    int best = -1;
    for (int g = 0; g < GOOD_COUNT; g++){
        if ((goods & GOOD_BIT(g)) && s[g] > 0 && (best < 0 || s[g] > s[best])){
            best = g;
        }
    }
    return best;
}

}

bool EconomyFrozen(const ChasmWorld& w, const ZoneState& z, uint32_t id, uint64_t tick){
    //The first plot, as home_plot takes it.
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id){
            return UnderSnow(w,w.grid->fine.pos[v],tick);
        }
    }
    return false;
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
                || AllInField(w,z,v) || ZoneShutToWalkers(z,v)){     //a gate's shut door too (defence_plan.md)
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
    now = 0;
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
    crop_defaults.clear();
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
    state.camp_plot = state.world ? ZoneStartCampPlot(*state.world,EconomySettlerCount()) : -1;
}

int EconomySettlerCount(){
    int n = 0;
    for (size_t f = 0; f < sizeof(settler_families) / sizeof(settler_families[0]); f++){
        n += 2 + settler_families[f].grown + settler_families[f].children;
    }
    return n;
}

/*
    The colony's first people, standing round the camp: each family its father, its mother, its grown children
    and its child. Their ages, birthdays, the children's sexes and everyone's skills come from the world's seed.
*/
void Economy::MakeSettlers(){
    state.workers.clear();
    state.next_person = 1;
    if (state.camp_plot < 0){
        return;
    }
    const ChasmWorld& w = *state.world;
    uint32_t seed = w.grid->settings.seed;
    vec2 camp = w.grid->fine.pos[state.camp_plot];
    //Born `years` and a day of the year (the seed's) before the colony's first dawn.
    auto born_ago = [&](uint32_t id, int years){
        int day = (int)(Mix(seed,id * 8 + 6) % (CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON));
        return -((int64_t)years * CALENDAR_YEAR_TICKS + (int64_t)day * CALENDAR_DAY_TICKS);
    };
    std::vector<size_t> grown;      //the grown children, by index
    std::vector<size_t> mothers;    //each family's
    int n = 0;
    for (size_t f = 0; f < sizeof(settler_families) / sizeof(settler_families[0]); f++){
        const SettlerFamily& sf = settler_families[f];
        uint32_t parent[2] = {0,0};     //father, mother
        for (int m = 0; m < 2 + sf.grown + sf.children; m++){
            EconomyWorker k;
            k.id = state.next_person++;
            k.family = (uint32_t)f + 1;
            k.surname = (uint8_t)EconomySettlerSurname(k.family);
            if (m < 2){
                //The couple, partners (BIRTHS): he first, she the next id.
                k.f_female = (m == 1);
                k.born = born_ago(k.id,SETTLER_COUPLE_AGE + (int)(Mix(seed,k.id * 8 + 0) % SETTLER_COUPLE_SPAN));
                k.partner = (m == 0) ? k.id + 1 : parent[0];
                parent[m] = k.id;
                if (m == 1){
                    mothers.push_back(state.workers.size());
                }
            }else{
                k.father = parent[0];
                k.mother = parent[1];
                k.f_female = (Mix(seed,k.id * 8 + 5) & 1) != 0;
                if (m < 2 + sf.grown){
                    k.born = born_ago(k.id,SETTLER_GROWN_AGE + (int)(Mix(seed,k.id * 8 + 0) % SETTLER_GROWN_SPAN));
                    grown.push_back(state.workers.size());
                }else{
                    int days = 1 + (int)(Mix(seed,k.id * 8 + 0) % SETTLER_CHILD_DAYS);
                    k.born = -((int64_t)LIFE_CHILD_TICKS - (int64_t)days * CALENDAR_DAY_TICKS);
                    k.job = WORKER_JOB_PLAYING;
                }
            }
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
    //A son and a daughter of different families among the grown children, so that a couple can form
    //(lifeline_plan.md): where the seed gave none, the last grown child is made the first one's other sex.
    bool f_pair = false;
    for (size_t a : grown){
        for (size_t b : grown){
            const EconomyWorker& ka = state.workers[a];
            const EconomyWorker& kb = state.workers[b];
            f_pair = f_pair || (ka.family != kb.family && ka.f_female && !kb.f_female);
        }
    }
    if (!f_pair && grown.size() >= 2){
        state.workers[grown.back()].f_female = !state.workers[grown.front()].f_female;
    }
    //One mother expecting (the user's), the seed's, her child due in the first year's first weeks - every
    //settler mother is under LIFE_CHILDREN_MOST.
    if (!mothers.empty()){
        EconomyWorker& m = state.workers[mothers[Mix(seed,0xB1A7u) % mothers.size()]];
        m.due = (uint64_t)(SETTLER_DUE_DAY + Mix(seed,0xB1A8u) % SETTLER_DUE_SPAN) * CALENDAR_DAY_TICKS;
    }
}

/*
    LIFELINES, a tick of them. A mother caring with no baby of hers left a baby - grown into a child, or dead -
    is free again; a baby a year old plays; a child of 2 1/2 is a young adult (out playing, he finishes and comes
    home first); a young adult turning ADULT may pair (COUPLES), so a house standing empty is offered again on
    that tick rather than at the next change. And BIRTHS: an expecting mother's child is born the first time she
    is home after it is due - never pulled from her work - at her side, indoors (in a camp, in her tent), and she
    is caring from then. The child's surname is one of its parents' (EconomyCoupleSurname).
*/
bool Economy::Lifelines(uint64_t tick, bool& f_house){
    bool f_jobs = false;
    f_house = false;
    for (EconomyWorker& k : state.workers){
        int stage = EconomyStageAt(k.born,tick);
        if (k.job == WORKER_JOB_BABY && stage >= LIFE_CHILD){
            k.job = WORKER_JOB_PLAYING;
            state.version++;
        }
        if (k.job == WORKER_JOB_PLAYING && stage >= LIFE_YOUNG){
            k.job = WORKER_JOB_NONE;
            f_jobs = true;
        }
        if (stage == LIFE_ADULT && tick > 0 && EconomyStageAt(k.born,tick - 1) < LIFE_ADULT && !k.partner){
            f_house = true;
        }
    }
    for (EconomyWorker& m : state.workers){
        if (m.job == WORKER_JOB_CARING && std::none_of(state.workers.begin(),state.workers.end(),[&](const EconomyWorker& o){
                return o.mother == m.id && o.job == WORKER_JOB_BABY; })){
            m.job = WORKER_JOB_NONE;
            f_jobs = true;
        }
    }
    std::vector<EconomyWorker> born;
    uint32_t seed = state.world->grid->settings.seed;
    for (EconomyWorker& m : state.workers){
        if (!m.due || tick < m.due || m.state != WORKER_AT_HOME || !AtHome(m)){
            continue;
        }
        EconomyWorker b;
        b.id = state.next_person++;
        b.family = m.family;
        b.house = m.house;
        b.born = (int64_t)tick;
        b.mother = m.id;
        b.father = m.partner;
        b.surname = (uint8_t)EconomyCoupleSurname(state,m.id,m.partner);
        b.f_female = (Mix(seed,b.id * 8 + 5) & 1) != 0;
        for (int sk = 0; sk < SKILL_COUNT; sk++){
            b.skills[sk] = (uint8_t)(1 + Mix(seed,b.id * 8 + 1 + sk) % 10);     //random, as a settler's - until step 5
        }
        b.job = WORKER_JOB_BABY;
        b.pos = m.pos;
        b.route.assign(1,b.pos);
        b.f_indoors = true;
        born.push_back(b);
        m.due = 0;
        m.job = WORKER_JOB_CARING;
        m.building = 0;
        f_jobs = true;
    }
    if (!born.empty()){
        state.workers.insert(state.workers.end(),born.begin(),born.end());     //the newest ids: still in id order
        state.version++;
    }
    return f_jobs;
}

/*
    BIRTHS (Economy.h), at dawn after the fires: each couple whose day of the year it is, with what a child asks
    of them, has its chance - by a hash of the seed, the mother and the year, so a replay draws it again.
*/
void Economy::ChancesOfChildren(const ZoneState& z, uint64_t tick){
    const int year_days = CALENDAR_SEASONS * CALENDAR_DAYS_PER_SEASON;
    int day = (int)((tick / CALENDAR_DAY_TICKS) % (uint64_t)year_days);
    uint32_t seed = state.world->grid->settings.seed;
    auto person = [&](uint32_t id) -> const EconomyWorker* {
        for (const EconomyWorker& k : state.workers){
            if (k.id == id){
                return &k;
            }
        }
        return NULL;
    };
    auto of_age = [&](const EconomyWorker& k){
        int64_t lived = (int64_t)tick - k.born;
        return EconomyStageAt(k.born,tick) == LIFE_ADULT && lived < LIFE_OLD_TICKS;
    };
    for (EconomyWorker& m : state.workers){
        if (!m.f_female || !m.partner || m.due || m.job == WORKER_JOB_CARING){
            continue;
        }
        const EconomyWorker* p = person(m.partner);
        if (!p || EconomyCoupleDay(m.id,p->id) != day){
            continue;
        }
        //Their own house, standing: not the camp's tents.
        uint32_t h = m.house;
        if (!h || h != p->house || h >= z.buildings.size() || z.buildings[h].kind != ZONE_KIND_HOUSE || !stands[h]){
            continue;
        }
        bool f_warm = h >= state.heat.size() || state.heat[h] != HEAT_COLD;
        bool f_fed = !EconomyPressing(m) && !EconomyPressing(*p);
        if (!f_warm || !f_fed || !of_age(m) || !of_age(*p) || EconomyChildrenOf(state,m.id) >= LIFE_CHILDREN_MOST){
            continue;
        }
        int living = 0;
        for (const EconomyWorker& o : state.workers){
            living += (o.house == h) ? 1 : 0;
        }
        int chance = (h < state.capacity.size() && living < state.capacity[h]) ? LIFE_BIRTH_ROOM : LIFE_BIRTH_FULL;
        if ((int)(Mix(Mix(seed,m.id * 4 + 3),(uint32_t)(tick / CALENDAR_YEAR_TICKS)) % 100) < chance){
            m.due = tick + LIFE_EXPECTING_TICKS;
            state.version++;
        }
    }
}

/*
    PLAYING (lifeline_plan.md): a child at home goes out now and then - on one look in LIFE_PLAY_LOOKS, from a
    hash of him and the look - to a plot a step or two from home that nothing stands on, or among a camp's
    tents (open ground, as the walkers have it), plays there a while (WORKER_PLAYING) and comes home. Only
    where he can walk to; otherwise he stays in.
*/
bool Economy::GoPlay(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t tick){
    uint32_t look = (uint32_t)(tick / ECONOMY_LOOK_TICKS);
    if (Mix(k.id * 2 + 1,look) % LIFE_PLAY_LOOKS != 0){
        return false;
    }
    const ChasmWorld& w = *state.world;
    int v = HomePlot(k);
    for (int step = 0; step < 2 && v >= 0; step++){
        int nb[8];
        int count = ZonePlotNeighbours(w,v,nb,8);
        int open[8];
        int n = 0;
        for (int j = 0; j < count; j++){
            int kind = z.KindOf(nb[j]);
            if ((z.storeys[nb[j]] == 0 && kind == ZONE_KIND_NONE) || kind == ZONE_KIND_CAMP){
                open[n++] = nb[j];
            }
        }
        if (n == 0){
            break;
        }
        v = open[Mix(look,k.id * 4 + (uint32_t)step) % (uint32_t)n];
    }
    if (v < 0 || v == HomePlot(k) || !RouteTo(k,z,walkers,v,w.grid->fine.pos[v])){
        return false;
    }
    k.state = WORKER_TO_PLAY;
    return true;
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
    walk there - now, if they are at the camp, or when their round brings them home. What is still empty
    after them goes to new COUPLES (Economy.h).
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
    //...and only one with a door is moved into: a house SHUT IN is uninhabitable (EconomyShutIn). A family
    //already shut in keeps it - nobody can get them out but the player, by clearing a plot beside it.
    auto can_take = [&](size_t id){
        return is_house(id) && !EconomyShutIn(state,(uint32_t)id);
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
            if (can_take(id) && living[id] == 0 && state.capacity[id] >= family_size[f]
                && (best == 0 || state.capacity[id] < state.capacity[best]) && CanWalk(z,walkers,at,home_plot[id])){
                best = id;
            }
        }
        if (best == 0){
            for (size_t id = 1; id < n && best == 0; id++){
                if (!can_take(id) || living[id] == 0 || living[id] >= family_size[f] || state.capacity[id] < family_size[f]
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
                    if (can_take(e) && living[e] == 0 && state.capacity[e] >= living[id]
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
    /*
        COUPLES (Economy.h): a house still empty, two grown people move into together and found a new family
        there - the eldest unpaired adult first (the lower id on a tie: the workers are in id order), with the
        eldest who can pair with them, into the smallest empty house that holds two and that both can walk to
        (the lower id on a tie). Each keeps their surname; the family each leaves keeps its house.
    */
    std::vector<size_t> empty;
    for (size_t id = 1; id < n; id++){
        if (can_take(id) && living[id] == 0 && state.capacity[id] >= LIFE_COUPLE_ROOM){
            empty.push_back(id);
        }
    }
    std::stable_sort(empty.begin(),empty.end(),[&](size_t a, size_t b){
        return state.capacity[a] < state.capacity[b];
    });
    std::vector<size_t> single;
    for (size_t i = 0; i < state.workers.size() && !empty.empty(); i++){
        const EconomyWorker& k = state.workers[i];
        if (!k.partner && EconomyStageAt(k.born,state.tick) == LIFE_ADULT){
            single.push_back(i);
        }
    }
    std::stable_sort(single.begin(),single.end(),[&](size_t a, size_t b){
        return state.workers[a].born < state.workers[b].born;
    });
    //A new family's number: never one of the dead's, so the tree's families stay apart.
    uint32_t next_family = (uint32_t)family_size.size();
    for (const EconomyDeath& d : state.dead){
        next_family = std::max(next_family,d.family + 1);
    }
    for (size_t s = 0; s < single.size() && !empty.empty(); s++){
        EconomyWorker& a = state.workers[single[s]];
        if (a.partner){
            continue;       //paired this pass already, as someone's eldest match
        }
        for (size_t t = 0; t < single.size(); t++){
            EconomyWorker& b = state.workers[single[t]];
            if (t == s || b.partner || b.f_female == a.f_female || b.family == a.family || EconomyRelated(state,a,b)){
                continue;
            }
            auto home = std::find_if(empty.begin(),empty.end(),[&](size_t id){
                return CanWalk(z,walkers,a.pos,home_plot[id]) && CanWalk(z,walkers,b.pos,home_plot[id]);
            });
            if (home == empty.end()){
                continue;
            }
            uint32_t h = (uint32_t)*home;
            empty.erase(home);
            uint32_t f = next_family++;
            family_size.resize(f + 1,0);
            family_house.resize(f + 1,0);
            for (EconomyWorker* k : {&a,&b}){
                if (k->house){
                    living[k->house]--;
                }
                family_size[k->family]--;
                k->family = f;
                k->house = h;
                if (k->state == WORKER_AT_HOME){
                    GoHome(*k,z,walkers);   //moving in
                }
            }
            a.partner = b.id;
            b.partner = a.id;
            family_size[f] = 2;
            family_house[f] = h;
            living[h] = 2;
            state.version++;
            break;
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
    POSTED people (POSTS, defence_plan.md) are out of it altogether: a job one has is his, its place counted
    taken before the order is walked - never filled again, never moved by it - while its workplace is there to
    have it; a jobless one is given none until he is released.
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
    //Housed, and of an age to work: a child playing is no place's (LIFELINES); nor is anyone posted.
    auto employable = [&](const EconomyWorker& k){
        return k.house && HomePlot(k) >= 0 && !EconomyStageJob(k.job) && k.post < 0;
    };
    auto is_land = [&](uint32_t id){
        return z.buildings[id].kind == ZONE_KIND_HOUSE || z.buildings[id].kind == ZONE_KIND_CAMP;
    };
    //Off an errand for his old job: home - or, with a load on him (a woodcutter's first log, out for a second),
    //to a store with it, holding it if none takes it. Home with it, it was neither put down nor carried on.
    auto leave_errand = [&](EconomyWorker& k){
        k.prop = -1;
        if (k.carry > 0){
            if (!FindStore(k,z,walkers)){
                k.state = WORKER_HOLDING;
            }
        }else{
            GoHome(k,z,walkers);
        }
    };
    int people = 0;
    for (const EconomyWorker& k : state.workers){
        people += employable(k) ? 1 : 0;
    }
    std::vector<int> reached(n,0);      //of each workplace's places, how many the people reach
    std::vector<int> held(n,0);
    std::vector<uint8_t> kept(state.workers.size(),0);
    //The posted keep theirs first, as far as its places go; one whose workplace went comes back to none.
    std::vector<int> reserved(n,0);
    for (size_t i = 0; i < state.workers.size(); i++){
        EconomyWorker& k = state.workers[i];
        if (k.post < 0 || k.job < 0){
            continue;
        }
        uint32_t b = k.building;
        if (b && b < n && z.buildings[b].size > 0 && JobOf(z.buildings[b].kind) == k.job && reserved[b] < state.places[b]){
            reserved[b]++;
            kept[i] = 1;
        }else{
            k.job = WORKER_JOB_NONE;
            k.building = 0;
            state.version++;
        }
    }
    for (uint32_t id : order){
        if (!is_land(id)){
            reached[id] = std::min(state.places[id] - reserved[id],people);
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
        reached[id] = std::min(state.places[id] - reserved[id],std::min(people,(int)family.size()));
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
                leave_errand(k);
            }
            k.job = WORKER_JOB_FARMER;
            k.building = id;
            state.version++;
        }
    }
    for (size_t i = 0; i < state.workers.size(); i++){
        EconomyWorker& k = state.workers[i];
        if (k.job == WORKER_JOB_NONE || EconomyStageJob(k.job) || kept[i] || k.post >= 0){
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
            leave_errand(k);
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
            int best_first = -1;
            float best_score = -1e30f;
            for (EconomyWorker& k : state.workers){
                if (k.job != WORKER_JOB_NONE || !employable(k)){
                    continue;
                }
                //A tower's guards by ARCHERY first - what the range trained - and then as any place, by precision
                //(range_plan.md, defence_plan.md).
                int first = (job == WORKER_JOB_GUARD) ? k.archery : 0;
                float score = k.skills[skill] * ECONOMY_SKILL_DISTANCE - (w.grid->fine.pos[HomePlot(k)] - at).length();
                //Only someone who can walk there from his house - a river with no bridge parts a village.
                if ((first > best_first || (first == best_first && score > best_score))
                    && CanWalk(z,walkers,w.grid->fine.pos[HomePlot(k)],home_plot[id])){
                    best_first = first;
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
            //Posted (defence_plan.md), his share waits for him: still his, and growing nothing meanwhile.
            field_pace[f.building] = (list[s]->post >= 0) ? 0.0f : EconomySkillFactor(list[s]->skills[SKILL_HUSBANDRY]);
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
void Economy::Derive(const ZoneState& z, Walkers& walkers, bool f_share){
    const ChasmWorld& w = *state.world;
    const Grid& g = *w.grid;
    const GridPicker& p = *w.picker;
    size_t n = z.buildings.size();
    size_t known = state.stock.size();
    state.stock.resize(n,std::array<int,GOOD_COUNT>{});
    state.pantry.resize(n,std::array<int,GOOD_COUNT>{});    //a camp's; split, it stays with the piece keeping the id
    state.heat.resize(std::max<size_t>(n,1),HEAT_NONE);
    //Every plot building's plots, and a field's first cell.
    plots_of.assign(n,std::vector<int>());
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v]){
            plots_of[z.building[v]].push_back((int)v);
        }
    }
    home_plot.assign(n,-1);
    state.shut.assign(n,0);
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
        if (ZoneKindOnCells(z.buildings[id].kind)){
            //A field's - or a range's - middle vertex, where its people and carriers walk to.
            if (first_cell[id] >= 0){
                home_plot[id] = CoarseCentre(g,first_cell[id]);
            }
        }else if (!plots_of[id].empty()){
            /*
                Its DOOR: its lowest plot that a walk from open ground can end on (Walkers::OpenFromOutside), so
                its family can get home and its workers to it. Its lowest plot alone, as it was, was often in its
                middle - the coarse grid's vertices keep the lowest numbers when it is subdivided, and one with
                every plot round it built over is shut in - and a house like that was never lived in, a workplace
                never staffed. With no way in anywhere, still its lowest, and the building SHUT IN (EconomyShutIn).
            */
            home_plot[id] = plots_of[id].front();
            state.shut[id] = 1;
            for (int v : plots_of[id]){
                if (walkers.OpenFromOutside(z,v)){
                    home_plot[id] = v;
                    state.shut[id] = 0;
                    break;
                }
            }
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
    //Gone buildings hold nothing: a store pulled down loses what it had, a site what it was brought.
    state.site.resize(n,std::array<int,GOOD_COUNT>{});
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].size <= 0){
            state.stock[id] = std::array<int,GOOD_COUNT>{};
            state.pantry[id] = std::array<int,GOOD_COUNT>{};
            state.site[id] = std::array<int,GOOD_COUNT>{};
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
        if (ZoneKindOnCells(z.buildings[id].kind)){
            //Something of it built: its farmer is taken on (a site has none, fields_plan.md) - a range's trainees.
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
        }else if (b.kind == ZONE_KIND_RANGE){
            //An archery range (range_plan.md): a trainee a built cell.
            state.places[id] = (int)cells_of[id].size() - site_cells[id];
        }else if (b.kind == ZONE_KIND_TOWER){
            //A guard tower (defence_plan.md): its guards once it stands whole - its platform up, its stock in.
            state.places[id] = (stands[id] && site_storeys[id] == 0) ? ECONOMY_TOWER_GUARDS : 0;
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
        GoodsMask attached = 0;
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
                //A field's crop changes as it is rotated (fields_plan.md): its store takes every food. A range
                //on cells makes nothing (range_plan.md), so a store beside one is a store like any other.
                for (int i = 0; i < p.PlotQuadCount(nb[j]); i++){
                    uint32_t cell_building = z.field[g.fine.quads[p.PlotQuadCorner(nb[j],i) / 4].parent];
                    if (cell_building && z.buildings[cell_building].kind == ZONE_KIND_FIELD){
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
    //A guard tower keeps its water and provisions (defence_plan.md): its guards' pantry, not a store - nobody
    //delivers to it, so it takes nothing (`accepts`); its room is what it is built with.
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind == ZONE_KIND_TOWER && z.buildings[id].size > 0){
            state.keeps[id] = GOOD_BIT(GOOD_WATER) | GOOD_BIT(GOOD_PROVISIONS);
            state.room[id] = ECONOMY_TOWER_WATER + ECONOMY_TOWER_PROVISIONS;
        }
    }
    /*
        A woodcutter's woodpile: on a lot plot of HIS OWN YARD that stands beside his hut (yards_plan.md) - the
        lowest such plot beside any of his plots. A plot has one owner, so two huts never pile on the same
        spot. The view draws the logs on the same plot. A woodcutter without one fells nothing, and his card
        and NEEDS ATTENTION say so.
    */
    state.pile_plot.assign(n,-1);
    pile_door.assign(n,-1);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_WOODCUTTER || z.buildings[id].size <= 0){
            continue;
        }
        for (int v : plots_of[id]){
            int nb[8];
            int k = ZonePlotNeighbours(w,v,nb,8);
            for (int j = 0; j < k; j++){
                int o = nb[j];
                if (z.ground[o] != ZONE_GROUND_LOT || !z.ground_built[o] || z.storeys[o] > 0
                    || ZoneGroundOwner(z,o) != (uint32_t)id){
                    continue;
                }
                if (state.pile_plot[id] < 0 || o < state.pile_plot[id]){
                    state.pile_plot[id] = o;
                }
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
    state.burn.assign(n,0);
    state.woodshed.assign(n,0);
    state.snowed.assign(n,0);
    float full_winter = CalendarSnowFrontZ(1.0f,w.grid->bounds_min.y,w.grid->bounds_max.y);
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
            //HEAT: its fire, a plot-storey standing at a time; its woodshed; whether winter's snow reaches it.
            int plot_storeys = 0;
            for (int v : plots_of[id]){
                plot_storeys += z.standing[v];
            }
            state.burn[id] = plot_storeys * ECONOMY_FIREWOOD_BURN;
            state.woodshed[id] = state.burn[id] * ECONOMY_WOODSHED_DAYS;
            state.snowed[id] = (!plots_of[id].empty() && CalendarSnowAt(w.grid->fine.pos[plots_of[id][0]],full_winter)) ? 1 : 0;
        }else if (z.buildings[id].kind == ZONE_KIND_CAMP && z.buildings[id].size > 0){
            //A camp holds a person a tent, any number of families (Zones.h, THE CAMP).
            for (int v : plots_of[id]){
                if (!ZoneCampFire(v) && z.standing[v] > 0){     //a tent still to be pitched holds nobody
                    tents_of[id].push_back(v);
                }
            }
            state.capacity[id] = (int)tents_of[id].size();
            //Its fires burn by its floor area too: a plot each, tents and fires alike (needs_plan.md).
            state.burn[id] = (int)plots_of[id].size() * ECONOMY_FIREWOOD_BURN;
            /*
                Its families FETCH for it like a house's (the user's, 2026-10-09 - they did not, so a baby born
                there went dry once the settlers' supplies did): a pantry's room and a woodshed by its floor area,
                and whether winter's snow reaches it. A load brought home is all taken in, as the supplies are.
            */
            state.room[id] = (int)std::floor(ZoneBuildingFigures(w,z,(uint32_t)id).floor_area * ECONOMY_HOUSE_ROOM);
            state.woodshed[id] = state.burn[id] * ECONOMY_WOODSHED_DAYS;
            state.snowed[id] = (!plots_of[id].empty() && CalendarSnowAt(w.grid->fine.pos[plots_of[id][0]],full_winter)) ? 1 : 0;
        }
    }
    state.door = home_plot;
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

/*
    Indoors, he is at his door wherever it is: a door the zones moved (Derive) moves him with it, through his
    own house, which nobody sees. Left where the old door was, he could be shut in there by the building that
    moved it - "at home", so never going out, and never fetching (a family died of thirst that way, 2026-10-09).
*/
void Economy::KeepAtDoors(){
    for (EconomyWorker& k : state.workers){
        if (!EconomyIndoors(k) || k.house >= home_plot.size() || home_plot[k.house] < 0){
            continue;
        }
        //A camp's are at their tents, not its door: a baby born there is "indoors", in its mother's tent.
        if (k.house < tents_of.size() && !tents_of[k.house].empty()){
            continue;
        }
        vec2 door = state.world->grid->fine.pos[home_plot[k.house]];
        if ((k.pos - door).length() > 1e-4f){
            k.pos = door;
            k.route.assign(1,door);
            k.speed.clear();
            k.seg = 0;
            k.along = 0.0f;
            state.version++;
        }
    }
}

//Every worker on his way plans again from where he stands - the zones under his route have changed.
void Economy::Replan(const ZoneState& z, Walkers& walkers){
    for (EconomyWorker& k : state.workers){
        //Stranded short of home: perhaps the change opened a way.
        if (k.state == WORKER_AT_HOME && !AtHome(k)){
            GoHome(k,z,walkers);
            continue;
        }
        //A child on his way out to play: home instead, the play given up. A trainee on his way to the range the
        //same - out again at his next look, to wherever his place is now (range_plan.md).
        if (k.state == WORKER_TO_PLAY || k.state == WORKER_TO_RANGE){
            GoHome(k,z,walkers);
            continue;
        }
        //Posted and cut off from his post, or on his way to it: the way there again (defence_plan.md).
        if ((k.state == WORKER_ON_POST && !EconomyAtPost(state,k)) || (k.state == WORKER_TO_POST && !Arrived(k))){
            GoToPost(k,z,walkers);
            continue;
        }
        if (k.state != WORKER_TO_WORK && k.state != WORKER_TO_STORE && k.state != WORKER_TO_HOME
            && k.state != WORKER_TO_FETCH && k.state != WORKER_TO_SITE && k.state != WORKER_TO_ROAD
            && k.state != WORKER_TO_SUPPLY && k.state != WORKER_TO_GET && k.state != WORKER_TO_PANTRY
            && k.state != WORKER_TO_SOW && k.state != WORKER_TO_CRATE && k.state != WORKER_TO_INPUT
            && k.state != WORKER_TO_WORKSHOP){
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
        }
        //Home is wherever its door is now: the change may have moved it (Derive), and the old one may be shut in.
        bool f_home = (k.state == WORKER_TO_HOME || k.state == WORKER_TO_PANTRY);
        if (!(f_home ? RouteHome(k,z,walkers) : RouteTo(k,z,walkers,goal_plot,goal))){
            //No way now: stop where he is and think again at the next look - posted, as one cut off from his post.
            k.route.assign(1,k.pos);
            k.speed.clear();
            k.seg = 0;
            k.along = 0.0f;
            k.state = (EconomyPosted(k) && k.carry <= 0) ? WORKER_ON_POST : (k.carry > 0) ? WORKER_HOLDING : WORKER_AT_HOME;
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
    The wood within `reach` of `at` that a woodcutter could go for: standing trees and felled logs (only logs,
    with f_logs) within ECONOMY_WOODCUTTER_REACH of his hut, that nobody else is after and that are not under a
    building or a field - LOGS FIRST (the user's, 2026-10-08: logs left lying by the road builders were passed
    over for the trees nearer the hut), then by the straight line from `at`, the lower index on a tie.
*/
void Economy::WoodNear(const EconomyWorker& k, const ZoneState& z, const vec2& at, float reach, bool f_logs,
                       std::vector<std::pair<std::pair<int,float>,int>>& out) const{
    out.clear();
    const ChasmWorld& w = *state.world;
    if (!w.forest || tree_nx == 0 || k.building >= home_plot.size() || home_plot[k.building] < 0){
        return;
    }
    const std::vector<PropInstance>& props = w.forest->props;
    vec2 hut = w.grid->fine.pos[home_plot[k.building]];
    std::vector<int> claimed;
    for (const EconomyWorker& o : state.workers){
        if (&o != &k && o.prop >= 0){
            claimed.push_back(o.prop);
        }
    }
    int r = (int)std::ceil(reach / tree_cell);
    int bx = (int)((at.x - tree_origin.x) / tree_cell);
    int bz = (int)((at.y - tree_origin.y) / tree_cell);
    for (int z_ = std::max(0,bz - r); z_ <= std::min(tree_nz - 1,bz + r); z_++){
        for (int x_ = std::max(0,bx - r); x_ <= std::min(tree_nx - 1,bx + r); x_++){
            int b = z_ * tree_nx + x_;
            for (int j = tree_start[b]; j < tree_start[b + 1]; j++){
                int i = tree_items[j];
                int ps = state.prop_state[i];
                if (ps == PROP_STATE_STUMP || (f_logs && ps != PROP_STATE_LOG)){
                    continue;
                }
                const PropInstance& pr = props[i];
                vec2 p(pr.pos.x,pr.pos.z);
                float d = (p - at).length();
                if (d > reach || (p - hut).length() > ECONOMY_WOODCUTTER_REACH){
                    continue;
                }
                //Under a building, a garden or lot, or a field: as good as not there (EconomyPropCovered).
                if (EconomyPropCovered(z,pr.plot,pr.coarse)){
                    continue;
                }
                if (std::find(claimed.begin(),claimed.end(),i) != claimed.end()){
                    continue;
                }
                out.push_back(std::make_pair(std::make_pair((ps == PROP_STATE_LOG) ? 0 : 1,d),i));
            }
        }
    }
    std::sort(out.begin(),out.end());
}

/*
    Out to a piece of wood (`found`, WoodNear's): the first of the nearest few he can get to, standing at its
    foot on the side he comes from. At most a few are tried, so a hut walled off from its wood does not
    search the forest every look.
*/
bool Economy::GoForWood(EconomyWorker& k, const ZoneState& z, Walkers& walkers,
                        const std::vector<std::pair<std::pair<int,float>,int>>& found, size_t tries){
    const std::vector<PropInstance>& props = state.world->forest->props;
    for (size_t t = 0; t < found.size() && t < tries; t++){
        int i = found[t].second;
        const PropInstance& pr = props[i];
        vec2 tree(pr.pos.x,pr.pos.z);
        vec2 to = tree - k.pos;
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

//The woodcutter's next wood, from home: a log lying within reach of his hut, else a tree to fell (WoodNear).
bool Economy::FindTree(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (k.building >= home_plot.size() || home_plot[k.building] < 0){
        return false;
    }
    std::vector<std::pair<std::pair<int,float>,int>> found;
    WoodNear(k,z,state.world->grid->fine.pos[home_plot[k.building]],ECONOMY_WOODCUTTER_REACH,false,found);
    return GoForWood(k,z,walkers,found,4);
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
    //Posted (defence_plan.md): back to his post, not home - a guard to his tower.
    if (EconomyPosted(k)){
        GoToPost(k,z,walkers);
        return;
    }
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

/*
    POSTED (defence_plan.md), in the middle of something else - an order lands between ticks, whatever he was
    at: he drops it for his post. A pressing need he is seeing to he finishes first (GoHome sends him on after).
    A storey he was putting up stands with what it has - its wood is in the site already, and nobody else would
    come to finish it. The rest of the errand - a tree, a site, a store - is let go for others (a load on him
    went with the order: Order). Setting out from home, he packs what he will live on from its pantry.
*/
void Economy::TakePost(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    if (k.state == WORKER_TO_SUPPLY || k.state == WORKER_SUPPLYING){
        return;
    }
    if (k.state == WORKER_BUILDING && k.site){
        Build(z,k.site);
    }
    if (k.state == WORKER_AT_HOME && AtHome(k) && k.house && k.house < state.stock.size() && k.house < z.buildings.size()
        && (z.buildings[k.house].kind == ZONE_KIND_HOUSE || z.buildings[k.house].kind == ZONE_KIND_CAMP)){
        FillPack(k,HomePantry(z,k.house));
        if (z.buildings[k.house].kind == ZONE_KIND_CAMP){
            FillPack(k,state.stock[k.house]);       //then the supplies, as he would eat
        }
    }
    k.prop = -1;
    k.site = 0;
    k.store = 0;
    k.stint = 0;
    k.timer = 0;
    if (k.carry <= 0){
        k.carry = 0;
        k.carry_good = -1;      //one fetching home names the kind he was after, holding nothing yet
    }
    GoToPost(k,z,walkers);
}

//The way to his post - a guard's, up onto his tower to his place there; with none, he waits where he is - on his
//post in name, cut off from it (TickWorker, Replan).
void Economy::GoToPost(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    int plot = EconomyPostPlot(state,k);
    if (plot >= 0 && plot < (int)w.grid->fine.pos.size() && RouteTo(k,z,walkers,plot,EconomyPostPoint(state,k))){
        k.state = WORKER_TO_POST;
        return;
    }
    k.route.assign(1,k.pos);
    k.speed.clear();
    k.seg = 0;
    k.along = 0.0f;
    k.state = WORKER_ON_POST;
}

/*
    His PACK filled from a stock (POSTS): water up to NEEDS_PACK_WATER units, food up to NEEDS_PACK_FOOD of the
    health it gives - the kind that keeps best first, since it is what he lives on away from home, and a bean is
    two days in one. A whole unit more while still short, so beans can take it a day past the mark.
*/
void Economy::FillPack(EconomyWorker& k, std::array<int,GOOD_COUNT>& from){
    while (from[GOOD_WATER] > 0 && k.pack[GOOD_WATER] < NEEDS_PACK_WATER){
        from[GOOD_WATER]--;
        k.pack[GOOD_WATER]++;
        state.version++;
    }
    //Provisions first where there are any - a soldier's food, one is the whole pack's (defence_plan.md).
    static const int keeps_best_first[] = {GOOD_PROVISIONS,GOOD_BEANS,GOOD_WHEAT,GOOD_GREENS};
    for (int good : keeps_best_first){
        while (from[good] > 0 && EconomyPackFood(k) < NEEDS_PACK_FOOD){
            from[good]--;
            k.pack[good]++;
            state.version++;
        }
    }
}

//Home again, released: his pack back into the pantry, as far as it keeps each good and has room - a camp's
//pantry takes it all, but PROVISIONS, which no home holds (defence_plan.md). What does not fit stays packed,
//for his next post.
void Economy::Unpack(EconomyWorker& k, const ZoneState& z){
    uint32_t h = k.house;
    if (!h || h >= z.buildings.size() || h >= state.stock.size()){
        return;
    }
    bool f_camp = z.buildings[h].kind == ZONE_KIND_CAMP;
    std::array<int,GOOD_COUNT>& pantry = HomePantry(z,h);
    for (int g = 0; g < GOOD_COUNT; g++){
        if (k.pack[g] <= 0 || g == GOOD_PROVISIONS){
            continue;
        }
        int put = f_camp ? k.pack[g]
                : (h < state.keeps.size() && (state.keeps[h] & GOOD_BIT(g))) ? std::min(k.pack[g],HouseRoom(z,h,g)) : 0;
        if (put > 0){
            pantry[g] += put;
            k.pack[g] -= put;
            state.version++;
        }
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
    //Somewhere that takes `amount` wood whole: a store with the room, or his pile.
    auto wood_room = [&](int amount){
        for (size_t s = 1; s < state.accepts.size(); s++){
            if ((state.accepts[s] & GOOD_BIT(GOOD_WOOD)) && state.room[s] - CarryTotal(state.stock[s]) >= amount){
                return true;
            }
        }
        return pile >= 0 && own[GOOD_WOOD] + amount <= ECONOMY_HUT_PILE;
    };
    //Posted, and at something else: dropped for the post (TakePost) - a pressing need seen to first.
    if (k.post >= 0 && k.state != WORKER_TO_POST && k.state != WORKER_ON_POST && k.state != WORKER_TO_SUPPLY
        && k.state != WORKER_SUPPLYING){
        TakePost(k,z,walkers);
    }
    switch (k.state){
        case WORKER_AT_HOME:
            if (!f_look){
                break;
            }
            /*
                Not home - his way there was cut (a river, a wall), or a re-plan stopped him short of it: he sees
                to a pressing need wherever he can reach one, his own pantry being no use to him out here, and
                every few looks tries the way home again (a failed search can cover the whole map). He used to
                wait for the zones to change, and when they did not he stood there until he died of thirst.
            */
            if (!AtHome(k)){
                if (!SeekSupply(k,z,walkers,false) && ((tick + k.id) / ECONOMY_LOOK_TICKS) % 4 == 0){
                    GoHome(k,z,walkers);
                }
                break;
            }
            //A baby: indoors in its mother's care, and nothing else (BIRTHS) - fed from the house's stock.
            if (k.job == WORKER_JOB_BABY){
                break;
            }
            //Home from a post: his pack back into the pantry (POSTS).
            if (std::any_of(k.pack.begin(),k.pack.end(),[](int n){ return n > 0; })){
                Unpack(k,z);
            }
            //Before any work: what he needs and his home has run out of (needs_plan.md).
            if (SeekSupply(k,z,walkers)){
                break;
            }
            //A guard (defence_plan.md): out to his tower with his pack, as anyone posted sets out - no errands.
            if (k.job == WORKER_JOB_GUARD){
                TakePost(k,z,walkers);
                break;
            }
            //A child: no work, no errands, no building - out to play now and then (LIFELINES).
            if (k.job == WORKER_JOB_PLAYING){
                GoPlay(k,z,walkers,tick);
                break;
            }
            //Then his house's pantry, running low, if he is the one to go (step 2) - before wood for a site, and
            //before his own round.
            if (FetchHome(k,z,walkers)){
                break;
            }
            //A mother caring for her baby: the house's errands, above, and home again (BIRTHS).
            if (k.job == WORKER_JOB_CARING){
                break;
            }
            //A builder, as a caretaker: provisions wanted, before building (MakeProvisions, defence_plan.md).
            if (k.job == WORKER_JOB_BUILDER && MakeProvisions(k,z,walkers)){
                break;
            }
            if (k.job == WORKER_JOB_NONE || k.job == WORKER_JOB_BUILDER){
                //Nothing of his own to do, or a builder: he carries wood to a construction that wants it, or
                //goes out to clear a planned road's or wall's way and lay a road - whichever comes first in
                //the one ranking (FindSiteWork; line_works_plan.md); the line work alone if none of that could go.
                //And a builder, as caretaker, with none of that to do: a crate to a storehouse, LAST of his
                //errands (crates_plan.md, the user's) - never the jobless.
                if (!FindSiteWork(k,z,walkers) && !FindRoadWork(k,z,walkers,false) && k.job == WORKER_JOB_BUILDER){
                    MoveCrate(k,z,walkers);
                }
                break;
            }
            if (!k.house){
                break;
            }
            //A farmer's land (land_plan.md): a field to sow before all else - a harvest can wait, a field sown
            //late grows late - then a harvest to fetch.
            if (k.job == WORKER_JOB_FARMER){
                if (!GoSow(k,z,walkers,tick)){
                    FetchHarvest(k,z,walkers);
                }
                break;
            }
            //A trainee (range_plan.md): out to his place on the line, all year.
            if (k.job == WORKER_JOB_ARCHER){
                GoTrain(k,z,walkers);
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
                    //Someone was quicker: home - or, with a first log on his shoulder, off with that.
                    k.prop = -1;
                    if (k.carry > 0){
                        deliver();
                    }else{
                        GoHome(k,z,walkers);
                    }
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
                k.carry = ((k.carry_good == GOOD_WOOD) ? k.carry : 0) + ECONOMY_WOOD_PER_TREE;
                k.carry_good = GOOD_WOOD;
                /*
                    TWO LOGS A ROUND (the user's, 2026-10-08): with another lying within ECONOMY_LOG_PAIR_REACH of
                    him, and somewhere to take both, he picks that one up too before he carries them off - a
                    log is half the walk of a tree's wood, and nothing to fell.
                */
                if (k.carry < ECONOMY_WOODCUTTER_LOGS * ECONOMY_WOOD_PER_TREE && wood_room(k.carry + ECONOMY_WOOD_PER_TREE)){
                    std::vector<std::pair<std::pair<int,float>,int>> logs;
                    WoodNear(k,z,k.pos,ECONOMY_LOG_PAIR_REACH,true,logs);
                    if (GoForWood(k,z,walkers,logs,2)){
                        break;
                    }
                }
                deliver();      //never left standing in the wood with it
            }else if (k.carry > 0){
                k.prop = -1;
                deliver();
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
        //--- Playing (LIFELINES) ---
        case WORKER_TO_PLAY:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_PLAYING;
                k.timer = SecondsToTicks(LIFE_PLAY_SECONDS);
            }
            break;
        case WORKER_PLAYING:
            if (--k.timer > 0){
                break;
            }
            GoHome(k,z,walkers);
            break;
        //--- Posted (defence_plan.md) ---
        case WORKER_TO_POST:
            //A guard whose tower went, or whose place there did: home (AssignJobs took the job).
            if (EconomyPostPlot(state,k) < 0){
                GoHome(k,z,walkers);
                break;
            }
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_ON_POST;
            }
            break;
        case WORKER_ON_POST:
            if (EconomyPostPlot(state,k) < 0){
                GoHome(k,z,walkers);
                break;
            }
            if (!f_look){
                break;
            }
            /*
                On his post he stands, and looks only to his needs - his own house one supply among the rest
                (SeekSupply); a guard on his tower lives on its stock first (Needs). Cut off from it - a wall, the
                way gone - he sees to a pressing need wherever he can reach one and tries the way again every few
                looks, as someone cut off from home does.
            */
            if (EconomyAtPost(state,k)){
                SeekSupply(k,z,walkers);
            }else if (!SeekSupply(k,z,walkers) && ((tick + k.id) / ECONOMY_LOOK_TICKS) % 4 == 0){
                GoToPost(k,z,walkers);
            }
            break;
        //--- Training (range_plan.md) ---
        case WORKER_TO_RANGE:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_TRAINING;
                k.timer = SecondsToTicks(ECONOMY_TRAIN_SECONDS);
            }
            break;
        case WORKER_TRAINING:{
            //A stint on the line, in any season and under the snow: archery grows at his precision's pace, up to
            //what his precision allows. Given another job meanwhile, he goes home.
            if (k.job != WORKER_JOB_ARCHER){
                GoHome(k,z,walkers);
                break;
            }
            int gain = (int)std::lround(ARCHERY_PER_TICK * pace);
            k.archery = std::min(EconomyArcheryCap(k),k.archery + gain);
            if (--k.timer <= 0){
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_HOLDING:
            //Holding a load: a store that takes it, or room on his pile, whichever comes first; else, for food,
            //his own house once it is short of food again (HomeShort) and not ahead of the colony (FairShare) - a
            //harvest left over when the pantry filled while he walked (land_plan.md) was held for good. Only
            //then, so it never crowds out the water; and
            //never wood, which would fill the pantry. With none, a pressing need is not left to kill him where
            //he stands; he takes the load along.
            if (f_look){
                deliver();
                if (k.state == WORKER_HOLDING && k.carry_good >= 0 && (GOODS_FOOD & GOOD_BIT(k.carry_good))
                    && (HomeShort(k,z,NULL) & GOODS_FOOD) && !HomeAhead(k,z) && RouteHome(k,z,walkers)){
                    k.store = 0;
                    k.state = WORKER_TO_PANTRY;
                }
                if (k.state == WORKER_HOLDING){
                    SeekSupply(k,z,walkers);
                }
            }
            break;
        //--- Moving a crate (crates_plan.md) ---
        case WORKER_TO_CRATE:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_LIFTING;
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
            }
            break;
        case WORKER_LIFTING:{
            if (--k.timer > 0){
                break;
            }
            /*
                A load of the good he came for - or, that gone, of the first that is there - that a store still takes
                whole, with the room, and off to it (FindStore). The crate may have been emptied while he walked, or a
                store filled: then he takes nothing and goes home.
            */
            uint32_t from = k.store;
            std::array<int,GOOD_COUNT>* src = SourceStock(from);
            int good = -1;
            for (int pass = 0; pass < 2 && src && good < 0; pass++){
                for (int g = 0; g < GOOD_COUNT && good < 0; g++){
                    if ((pass == 0) != (g == k.carry_good) || (*src)[g] <= 0){
                        continue;
                    }
                    int amount = std::min(ECONOMY_LOAD,(*src)[g]);
                    for (size_t s = 1; s < state.accepts.size(); s++){
                        if ((state.accepts[s] & GOOD_BIT(g)) && state.room[s] - CarryTotal(state.stock[s]) >= amount){
                            good = g;
                            break;
                        }
                    }
                }
            }
            if (good >= 0){
                k.carry_good = good;
                k.carry = std::min(ECONOMY_LOAD,(*src)[good]);
                if (FindStore(k,z,walkers)){
                    (*src)[good] -= k.carry;
                    CrateTouched(from);
                    break;
                }
            }
            k.carry = 0;
            k.carry_good = -1;
            GoHome(k,z,walkers);
            break;
        }
        //--- Provisions (defence_plan.md, step 5): a builder's wheat and beans to his hut, and making them there ---
        case WORKER_TO_INPUT:
            Walk(k,dt);
            if (Arrived(k)){
                k.state = WORKER_TAKING_INPUT;
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
            }
            break;
        case WORKER_TAKING_INPUT:{
            if (--k.timer > 0){
                break;
            }
            //The load he set out for, or what is left of it there - taken by others, or eaten, while he walked.
            uint32_t from = k.store;
            std::array<int,GOOD_COUNT>* src = SourceStock(from);
            int good = k.carry_good;
            int take = (src && good >= 0 && good < GOOD_COUNT) ? std::min(k.stint,(*src)[good]) : 0;
            uint32_t h = k.building;
            int plot = (take > 0 && h < z.buildings.size()) ? NearestPlot(h,k.pos) : -1;
            if (plot < 0 || !RouteTo(k,z,walkers,plot,state.world->grid->fine.pos[plot])){
                k.store = 0;
                k.stint = 0;
                k.carry_good = -1;
                GoHome(k,z,walkers);
                break;
            }
            (*src)[good] -= take;
            CrateTouched(from);
            k.carry = take;
            k.store = 0;
            k.stint = 0;
            k.state = WORKER_TO_WORKSHOP;
            state.version++;
            break;
        }
        case WORKER_TO_WORKSHOP:
            Walk(k,dt);
            if (Arrived(k)){
                AtWorkshop(k,z,walkers,pace);
            }
            break;
        case WORKER_MAKING:
            if (--k.timer > 0){
                break;
            }
            //One made, into the hut's stock - the hut gone meanwhile, it went with it - and on (AtWorkshop).
            if (Workshop(z,stands,k.building)){
                state.stock[k.building][GOOD_PROVISIONS]++;
                state.version++;
            }
            k.stint++;
            k.state = WORKER_TO_WORKSHOP;   //not MAKING while AtWorkshop counts what is being made
            AtWorkshop(k,z,walkers,pace);
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
            //A load of what is there and what the site still wants of the good he came for - either may have
            //changed on the way.
            uint32_t from = k.store;
            int good = k.carry_good;
            bool f_site = SiteAlive(z,k.site) && good >= 0 && good < GOOD_COUNT;
            std::array<int,GOOD_COUNT>* src = SourceStock(from);     //a building's, or a crate's - gone, if emptied meanwhile
            int have = (f_site && src) ? (*src)[good] : 0;
            int take = f_site ? std::min(ECONOMY_LOAD,std::min(have,SiteLeft(z,k.site,&k)[good])) : 0;
            int to = !f_site ? -1 : EconomyIsGroundSite(k.site) ? GroundSiteApproach(z,EconomyGroundSitePlot(k.site))
                                                                : NearestPlot(k.site,k.pos,true);
            if (take <= 0 || to < 0 || !RouteTo(k,z,walkers,to,state.world->grid->fine.pos[to])){
                k.site = 0;
                k.carry_good = -1;
                GoHome(k,z,walkers);
                break;
            }
            (*src)[good] -= take;
            CrateTouched(from);
            k.carry = take;
            k.store = 0;
            k.state = WORKER_TO_SITE;
            break;
        }
        case WORKER_TO_SITE:
            Walk(k,dt);
            if (Arrived(k)){
                uint32_t id = k.site;
                if (SiteAlive(z,id) && k.carry_good >= 0 && SiteNeed(z,id)[k.carry_good] > 0){
                    if (EconomyIsGroundSite(id)){
                        state.ground_site[EconomyGroundSitePlot(id)] += k.carry;     //wood: all a ground site takes
                    }else{
                        state.site[id][k.carry_good] += k.carry;
                    }
                    k.carry = 0;
                    k.carry_good = -1;
                    k.state = WORKER_BUILDING;
                    k.timer = SecondsToTicks(ECONOMY_BUILD_SECONDS / pace);
                }else{
                    //Pulled down, or finished by other hands, while he walked: the load goes to a store.
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
            //From what is there now - others may have been quicker - as he would at home: at his own (posted), its
            //pantry first. A crate is gone if it was emptied meanwhile, and goes if he empties it.
            if (std::array<int,GOOD_COUNT>* src = SourceStock(k.store)){
                if (k.store == k.house){
                    Consume(k,HomePantry(z,k.house));
                }
                Consume(k,*src);
                CrateTouched(k.store);
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
                //Posted (defence_plan.md): his pack topped up from what is here, and back to the post.
                if (EconomyPosted(k)){
                    if (std::array<int,GOOD_COUNT>* src = SourceStock(from)){
                        if (from == k.house){
                            FillPack(k,HomePantry(z,k.house));
                        }
                        FillPack(k,*src);
                        CrateTouched(from);
                    }
                    GoHome(k,z,walkers);
                    break;
                }
                if (k.carry > 0){
                    deliver();          //the load he was holding, or another try at it
                    break;
                }
                /*
                    A load home with him from wherever he ate or drank, if a home may fetch from it (FetchFrom) - a
                    store, a camp's supplies, a collector, his own land's field (the user's, 2026-10-09: it was only
                    a store or the camp, and only for a house already short) - whenever his house has room under its
                    top-up, the scarcest kind first, as far as TakeHome allows: a field's fair share, firewood's.
                    Not a child: errands are for the grown (LIFELINES).
                */
                if ((from < z.buildings.size() || EconomyIsCrateStore(from)) && (!EconomyStageJob(k.job) || k.job == WORKER_JOB_CARING)
                    && FetchFrom(k,z,from)){
                    GoodsMask first = 0;
                    HomeShort(k,z,&first);
                    bool f_took = false;
                    for (GoodsMask goods : {first,(GoodsMask)GOOD_BIT(GOOD_WATER),(GoodsMask)GOODS_FOOD,(GoodsMask)GOOD_BIT(GOOD_WOOD)}){
                        f_took = f_took || (goods && TakeHome(k,z,walkers,from,goods));
                    }
                    if (f_took){
                        break;
                    }
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
            GoodsMask goods = (k.carry_good == GOOD_WATER || k.carry_good == GOOD_WOOD) ? (GoodsMask)GOOD_BIT(k.carry_good)
                                                                                       : (GoodsMask)GOODS_FOOD;
            if (!TakeHome(k,z,walkers,k.store,goods)){
                k.carry_good = -1;
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_TO_PANTRY:{
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
            /*
                Into the house's own stock as far as it keeps it and has room; a camp's pantry takes it all - but a
                harvest from the camp's own land (PickHarvest: `store` the camp) goes into its supplies, the colony's.
            */
            uint32_t h = k.house;
            int put = 0;
            if (h && h < z.buildings.size() && h < state.stock.size() && k.carry_good >= 0){
                if (z.buildings[h].kind == ZONE_KIND_CAMP){
                    put = k.carry;
                    (k.store == h ? state.stock[h] : HomePantry(z,h))[k.carry_good] += put;
                }else if (h < state.keeps.size() && (state.keeps[h] & GOOD_BIT(k.carry_good))){
                    put = std::min(k.carry,HouseRoom(z,h,k.carry_good));
                    state.stock[h][k.carry_good] += put;
                }
            }
            k.carry -= put;
            k.store = 0;
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
            //wants sowing: built, empty, and its crop decided and still in time for the frost.
            uint32_t id = k.store;
            EconomyField* f = FieldOf(id);
            int crop = -1;
            if (f && id < z.buildings.size() && id < home_plot.size() && home_plot[id] >= 0){
                int left = EconomyGrowingTicksLeft(*state.world,state.world->grid->fine.pos[home_plot[id]],tick);
                crop = EconomyFieldSowNow(*f,z.buildings[id],(id < field_pace.size()) ? field_pace[id] : 0.0f,left);
            }
            if (f && !f->f_sown && crop >= 0 && id < site_cells.size() && site_cells[id] == 0){
                f->crop = crop;
                f->f_sown = true;
                f->grown = 0;
                f->yield = EconomyCropYield(f->last,crop);
                f->frost = -1;
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
    decided - the player's starting crop the first time, its next crop after - and in time to ripen before the
    frost (EconomyFieldSowNow: never in winter or under snow); its share farmed, not lying fallow; and nobody
    else already out to sow it. The nearest to him by the straight line, the lower id on a tie. Out to it
    (`store`), where its harvest is picked up too.
*/
bool Economy::GoSow(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint64_t calendar_tick){
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
        int left = EconomyGrowingTicksLeft(w,w.grid->fine.pos[home_plot[id]],calendar_tick);
        if (EconomyFieldSowNow(f,z.buildings[id],(id < field_pace.size()) ? field_pace[id] : 0.0f,left) < 0){
            continue;       //winter, the snow, or too late in the year for its crop: spring
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
    A trainee from home (range_plan.md): to HIS place on his range's line. The range's built cells, in index
    order, are shared out to its trainees by id, a cell each (a place a built cell); he walks to his cell's
    middle and on to its line (ZoneRangeLine), and a last half-step toward its target, so he stands facing it -
    his facing is his last leg's (EconomyWorkerFacing). Nothing names the cell: he stands in it, which is how
    the view finds his target. True if he set off.
*/
bool Economy::GoTrain(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    uint32_t id = k.building;
    if (id >= cells_of.size() || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_RANGE){
        return false;
    }
    std::vector<int> built;
    for (int c : cells_of[id]){
        if (z.field_built[c]){
            built.push_back(c);
        }
    }
    if (built.empty()){
        return false;
    }
    int rank = 0;
    for (const EconomyWorker& o : state.workers){
        rank += (o.job == WORKER_JOB_ARCHER && o.building == id && o.id < k.id) ? 1 : 0;
    }
    ZoneRangeCell r = ZoneRangeCellAt(*state.world,built[rank % built.size()]);
    if (r.middle < 0 || !RouteTo(k,z,walkers,r.middle,ZoneRangeLine(r))){
        return false;
    }
    k.route.push_back(k.route.back() + r.dir * 0.02f);
    k.speed.push_back(k.speed.empty() ? ECONOMY_WORKER_SPEED : k.speed.back());
    k.prop = -1;
    k.store = 0;
    k.state = WORKER_TO_RANGE;
    return true;
}

/*
    From home, a harvest waiting on his land (land_plan.md): of its fields with food lying - fallow ones too,
    for what grew before - the one with the most, the lower id on a tie, that no other farmer is already out to
    fetch from; and only with somewhere for a load of it: the camp's supplies always, his house while it is
    short of food and not ahead of the colony (FairShare), or else a store with room. Out to it (`store`). True
    if he set off.
*/
bool Economy::FetchHarvest(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t land = k.building;
    bool f_home = (land < z.buildings.size() && z.buildings[land].kind == ZONE_KIND_CAMP)
                  || ((HomeShort(k,z,NULL) & GOODS_FOOD) && !HomeAhead(k,z));
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
    load into the camp's supplies, which take it all; a house's while it is short of food and not ahead of the
    colony, what brings it up to its fair share (TakeHome); otherwise a load to the nearest store that takes it.
    False, taking nothing, with nothing there or nowhere for it.
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
        k.store = k.house;      //for the camp's supplies, not its pantry (WORKER_TO_PANTRY)
        k.prop = -1;
        k.state = WORKER_TO_PANTRY;
        state.version++;
        return true;
    }
    if ((HomeShort(k,z,NULL) & GOODS_FOOD) && !HomeAhead(k,z) && TakeHome(k,z,walkers,id,GOODS_FOOD)){
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

/*
    FAIR SHARES (the user's, 2026-10-08): a family kept its fields' food at home while the next house had none -
    its own land was a pantry nobody else could reach. So what a house may keep from its own land is the
    colony's average - all the food in homes, the camps' supplies with them, over everyone living in them - and
    ECONOMY_FAIR_DAYS a head more; a house with that much sends its harvest to storage, where the others fetch it.
    Fetching from a store or the camp is not held to it: that food is everyone's already.
*/
int64_t Economy::FairShare(const ZoneState& z) const{
    size_t n = std::min(z.buildings.size(),state.stock.size());
    std::vector<uint8_t> home(n,0);
    int64_t people = 0;
    for (const EconomyWorker& k : state.workers){
        if (k.house && k.house < n){
            home[k.house] = 1;
            people++;
        }
    }
    int64_t food = 0;
    for (size_t id = 1; id < n; id++){
        food += home[id] ? HeldTicks(HomeHeld(z,(uint32_t)id),GOODS_FOOD) : 0;    //a camp's pantry with its supplies
    }
    return (people ? food / people : 0) + (int64_t)ECONOMY_FAIR_DAYS * CALENDAR_DAY_TICKS;
}

bool Economy::HomeAhead(const EconomyWorker& k, const ZoneState& z) const{
    uint32_t h = k.house;
    if (!h || h >= z.buildings.size() || h >= state.stock.size() || z.buildings[h].kind != ZONE_KIND_HOUSE){
        return false;
    }
    int64_t living = Living(state.workers,h);
    return living > 0 && HeldTicks(state.stock[h],GOODS_FOOD) >= living * FairShare(z);
}

void Economy::Tick(const ZoneState& z, Walkers& walkers, uint64_t tick, float dt){
    if (!state.world || z.world != state.world){
        return;
    }
    const ChasmWorld& w = *state.world;
    now = tick;
    state.tick = tick;
    raises.clear();
    ground_raises.clear();
    field_raises.clear();
    crop_defaults.clear();
    if (f_resettle && z.version == state.zones_version){
        //A save from before land (Restore): its land settled and its farmers taken on, on its first tick. No
        //re-plan - nothing on the ground changed, so everyone walks on as saved.
        HouseFamilies(z,walkers);
        AssignJobs(z,walkers);
        state.version++;
    }
    f_resettle = false;
    if (z.version != state.zones_version){
        size_t known = state.stock.size();
        Derive(z,walkers,true);
        //A guard tower placed in debug stands whole at once, never a site: it stands FULL, as one built would
        //(defence_plan.md). New ids only - a load never comes here, and a tower once placed keeps what it has.
        for (size_t id = known; id < z.buildings.size(); id++){
            if (z.buildings[id].kind == ZONE_KIND_TOWER && stands[id] && site_storeys[id] == 0){
                state.stock[id][GOOD_WATER] = ECONOMY_TOWER_WATER;
                state.stock[id][GOOD_PROVISIONS] = ECONOMY_TOWER_PROVISIONS;
            }
        }
        KeepAtDoors();
        LoseCoveredCrates(z);
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
    //LIFELINES: the stages moving on and the births, the places shared out again at once when they free
    //someone or take a mother out of hers - not at the zones' next change.
    bool f_house = false;
    bool f_grew = Lifelines(tick,f_house);
    //Someone of age to pair: the free houses offered again (COUPLES) - and the places after, a couple moving.
    if (f_house){
        HouseFamilies(z,walkers);
        f_grew = true;
    }
    //...and an order given or ended since the last tick (Order): the posted out of the staffing, the released
    //back in - on this tick, after the zones' own change if there was one.
    if (f_grew || f_restaff){
        AssignJobs(z,walkers);
        state.version++;
    }
    f_restaff = false;
    //Who works where, by workplace id: a collector fills only while somebody works it (a field: field_pace) -
    //not while he is away at a post (defence_plan.md).
    std::vector<float> pace(z.buildings.size(),0.0f);
    for (const EconomyWorker& k : state.workers){
        if (k.building && k.building < pace.size() && k.post < 0){
            pace[k.building] = EconomySkillFactor(k.skills[EconomyJobSkill(k.job)]);
        }
    }
    CalendarDate date = CalendarDateOf(tick);
    /*
        A CROP LEFT UNCHOSEN (issues_plan.md, the user's 2026-10-09): a field with every cell built, never sown,
        with no starting crop, waits ECONOMY_CROP_GRACE for the player and then starts with AUTO - a cell of it
        named in crop_defaults, which the app sets on this same tick, as it raises what the builders finished.
        Counted in ticks, so it stops with the pause and a replay does it again on the same tick.
    */
    for (EconomyField& f : state.fields){
        uint32_t id = f.building;
        bool f_waiting = id < z.buildings.size() && z.buildings[id].crop == ZONE_CROP_NONE && !f.f_sown && f.last < 0
                         && id < cells_of.size() && !cells_of[id].empty() && site_cells[id] == 0;
        if (!f_waiting){
            f.wait = 0;
            continue;
        }
        //Held at the grace, so a default the zones refused is named again on the next tick.
        f.wait = std::min(f.wait + 1,(int)ECONOMY_CROP_GRACE);
        if (f.wait >= ECONOMY_CROP_GRACE){
            crop_defaults.push_back(cells_of[id][0]);
        }
    }
    /*
        The fields grow once SOWN (fields_plan.md), in any weather but winter's and the snow's, while
        their share of the land is farmed, at that farmer's pace (land_plan.md) - a fallow field waits.
        At harvest the field yields its built area x its crop's own yield x the yield it was sown at, and
        is empty again, that crop its last. THE FROST (Economy.h): winter takes whatever still grows -
        the field empty again with its soil as it was, the crop before still its last, the loss kept for
        its card until it is sown again.
    */
    for (EconomyField& f : state.fields){
        int crop = f.crop;
        if (!f.f_sown || crop < 0 || crop >= ZONE_CROP_COUNT){
            continue;
        }
        if (date.season == SEASON_WINTER){
            f.frost = crop;
            f.f_sown = false;
            f.crop = -1;
            f.grown = 0;
            state.version++;
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
    //HEAT: every home's fire, lit or not, at dawn - and then the couples whose day it is (BIRTHS), which
    //wants that fire.
    if (tick % (uint64_t)CALENDAR_DAY_TICKS == 0){
        Heat(z,tick);
        ChancesOfChildren(z,tick);
    }
    for (EconomyWorker& k : state.workers){
        TickWorker(k,z,walkers,tick,dt);
    }
    Needs(z,walkers,tick);
    //After everyone has moved, died or moved out: a camp left empty opens its pantry to all.
    OpenPantries(z);
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
        //Made from a wheat and a beans, and worth the two together (defence_plan.md, PROVISIONS).
        case GOOD_PROVISIONS: return EconomyFoodWorth(GOOD_WHEAT) + EconomyFoodWorth(GOOD_BEANS);
        default:            return 0;
    }
}

const char* EconomyDeathCause(int cause){
    switch (cause){
        case NEEDS_DIED_HUNGER: return "hunger";
        case NEEDS_DIED_THIRST: return "thirst";
        case NEEDS_DIED_COLD:   return "cold";
        default:                return "?";
    }
}

std::string EconomyDeathName(const EconomyDeath& d){
    EconomyWorker k;
    k.id = d.id;
    k.family = d.family;
    k.f_female = d.f_female;
    k.surname = d.surname;
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
    Consume(k,HomePantry(z,k.house));
    if (kind == ZONE_KIND_CAMP){
        Consume(k,state.stock[k.house]);    //then the supplies (STORES, Economy.h)
    }
}

/*
    Eating and drinking from a stock, at home or away: only a whole unit that fits under full, so nothing is
    wasted. Water first: food does nothing for someone dry. Then food while any fits, the kind that keeps
    worst first - greens rot, wheat keeps, beans keep longest (gameplay_plan.md, "Goods and food") - so a
    pantry eats down its greens and saves its beans. PROVISIONS last of all (defence_plan.md): kept for
    soldiers, eaten only when nothing else there fits.
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
    static const int keeps_worst_first[] = {GOOD_GREENS,GOOD_WHEAT,GOOD_BEANS,GOOD_PROVISIONS};
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

bool Economy::SetStock(uint32_t id, int good, int amount, bool f_pantry){
    if (!id || id >= state.stock.size() || id >= state.pantry.size() || good < 0 || good >= GOOD_COUNT || amount < 0){
        return false;
    }
    (f_pantry ? state.pantry[id] : state.stock[id])[good] = amount;
    state.version++;
    return true;
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
    AN ORDER (POSTS, defence_plan.md). Sent: only someone old enough, to a plot nothing stands on - open ground,
    a road, a field, a camp's or a standing bridge's - that he can walk to from where he is. The new post is
    his on the spot; what he was doing he drops on the next tick (TakePost, from TickWorker), after the zones
    that tick brings, and the staffing follows then too (f_restaff). A LOAD on him he puts down at once, as a
    CRATE (DropLoad, crates_plan.md) - here and not on the next tick, so it is down before the meal a pressing
    need sends him to, and lies on the ground as the zones stand now. Already posted, he turns for the new one
    at once. Released: he goes home, and is staffed like anyone on the next tick - back
    to the work kept for him. A pressing need he is seeing to goes on either way.
*/
bool Economy::Order(uint32_t person, int plot, const ZoneState& z, Walkers& walkers, std::string* why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    EconomyWorker* k = NULL;
    for (EconomyWorker& o : state.workers){
        if (o.id == person){
            k = &o;
        }
    }
    if (!k){
        return refuse("nobody by that id");
    }
    const ChasmWorld& w = *state.world;
    if (plot < 0){
        if (k->post < 0){
            return refuse("not posted");
        }
        k->post = -1;
        if (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST){
            GoHome(*k,z,walkers);
        }
        f_restaff = true;
        state.version++;
        return true;
    }
    if (!EconomyCanOrder(*k,now)){
        return refuse("too young");
    }
    if (plot >= (int)z.ground.size() || plot >= (int)w.grid->fine.pos.size()){
        return refuse("no such plot");
    }
    int kind = z.KindOf(plot);
    if (z.ground[plot] == ZONE_GROUND_WALL || ZoneGateShut(z,plot)
        || (kind != ZONE_KIND_NONE && kind != ZONE_KIND_CAMP && !ZoneBridgeWalkable(z,plot))){
        return refuse("something stands there");
    }
    if (!CanWalk(z,walkers,k->pos,plot)){
        return refuse("no way there");
    }
    bool f_new = k->post < 0;
    k->post = plot;
    if (k->carry > 0){
        DropLoad(*k,z);
    }
    if (k->state == WORKER_TO_POST || k->state == WORKER_ON_POST){
        GoToPost(*k,z,walkers);
    }
    f_restaff = f_restaff || f_new;
    state.version++;
    return true;
}

/*
    A PRESSING NEED (EconomyPressing) his home has none of - water, or food - and he takes on no more work
    (the user's, 2026-10-07): off to the nearest place that has some of what home lacks - a store, the
    camp's supplies, a collector's water, a field's harvest; never another family's house. Nearest by the
    straight line, the lower id on a tie; the first of the nearest few he can walk to. False with no need,
    a home that can meet it, or nowhere he can get to - then he carries on as before, since stopping work
    would save nobody, and the work may be the collector that would. `f_home` false: he cannot get home, so
    what it holds does not count. POSTED (defence_plan.md), he is not going home either: his own house is
    then a supply like any other, by its distance - another family's still never is.
    PROVISIONS (defence_plan.md): a posted person's food is any food, provisions too, the nearest place
    first. A villager's is the crops; only if no place he can get to has any does he look again, with
    provisions - his LAST RESORT.
*/
bool Economy::SeekSupply(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_home){
    GoodsMask pressing = EconomyPressing(k);
    //A baby goes nowhere for it (BIRTHS): its family's fetching is what feeds it.
    if (!pressing || k.job == WORKER_JOB_BABY){
        return false;
    }
    bool f_posted = EconomyPosted(k);
    if (f_posted){
        return SeekSupplyOf(k,z,walkers,false,pressing);
    }
    if (SeekSupplyOf(k,z,walkers,f_home,(GoodsMask)(pressing & ~GOOD_BIT(GOOD_PROVISIONS)))){
        return true;
    }
    return (pressing & GOODS_FOOD) && SeekSupplyOf(k,z,walkers,f_home,pressing);
}

//SeekSupply's search for a place with some of `want`: water, and food - provisions only where `want` has them.
bool Economy::SeekSupplyOf(EconomyWorker& k, const ZoneState& z, Walkers& walkers, bool f_home, GoodsMask want){
    bool f_posted = EconomyPosted(k);
    //What building `id` - or a crate (ECONOMY_STORE_CRATE) - holds for him: his own home all of it, a camp's
    //pantry with its supplies.
    auto held = [&](uint32_t id){
        const std::array<int,GOOD_COUNT>* s = SourceStock(id);
        return (id == k.house) ? HomeHeld(z,id) : s ? *s : std::array<int,GOOD_COUNT>{};
    };
    //What `id` has of `goods`.
    auto has = [&](uint32_t id, GoodsMask goods){
        std::array<int,GOOD_COUNT> s = held(id);
        for (int g = 0; g < GOOD_COUNT; g++){
            if ((goods & GOOD_BIT(g)) && s[g] > 0){
                return true;
            }
        }
        return false;
    };
    //What his home has he gets there, on his way back.
    if (f_home && k.house && k.house < state.stock.size()){
        if (has(k.house,GOOD_BIT(GOOD_WATER))){
            want &= (GoodsMask)~GOOD_BIT(GOOD_WATER);
        }
        if (has(k.house,GOODS_FOOD)){
            want &= (GoodsMask)~GOODS_EATEN;
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
    auto spare = [&](uint32_t id, GoodsMask goods){
        std::array<int,GOOD_COUNT> s = held(id);
        int left = 0;
        for (int g = 0; g < GOOD_COUNT; g++){
            left += (goods & GOOD_BIT(g)) ? s[g] : 0;
        }
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.store == id && (o.state == WORKER_TO_SUPPLY || o.state == WORKER_SUPPLYING)
                && (EconomyPressing(o) & goods)){
                left--;
            }
        }
        return left;
    };
    GoodsMask food = want & GOODS_EATEN;
    auto offers = [&](uint32_t id){
        return ((want & GOOD_BIT(GOOD_WATER)) && spare(id,GOOD_BIT(GOOD_WATER)) > 0)
            || (food && spare(id,food) > 0);
    };
    const ChasmWorld& w = *state.world;
    //Where he would eat there: his own house at its door, anywhere else at its plot nearest him.
    auto source = [&](uint32_t id){
        return (id == k.house) ? HomePlot(k) : SourcePlot(id,k.pos);
    };
    std::vector<std::pair<float,uint32_t>> found;
    size_t n = std::min(z.buildings.size(),state.stock.size());
    for (size_t id = 1; id < n; id++){
        bool f_own = f_posted && id == k.house;
        //Never a builders' hut: its wheat, beans and provisions are for the sites (defence_plan.md). A guard tower
        //only for the posted: its stock is its guards', and the village would empty it.
        if ((id == k.house && !f_own) || z.buildings[id].size <= 0 || (PrivateStock(z,(uint32_t)id) && !f_own)
            || z.buildings[id].kind == ZONE_KIND_BUILDERS || (z.buildings[id].kind == ZONE_KIND_TOWER && !f_posted)
            || !has((uint32_t)id,want) || !offers((uint32_t)id)){
            continue;
        }
        int plot = source((uint32_t)id);
        if (plot >= 0){
            found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
        }
    }
    //A crate is a store anyone may eat and drink at (crates_plan.md).
    for (const EconomyCrate& c : state.crates){
        uint32_t id = ECONOMY_STORE_CRATE | c.id;
        if (has(id,want) && offers(id)){
            found.push_back(std::make_pair((c.pos - k.pos).length(),id));
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = source(id);
        if (RouteTo(k,z,walkers,plot,SourcePoint(id,plot,k.pos))){
            k.store = id;
            k.prop = -1;
            k.state = WORKER_TO_SUPPLY;
            return true;
        }
    }
    return false;
}

/*
    FETCHING HOME (needs_plan.md, step 2). What his house is short of: water, food, firewood or any of them -
    water or food where it holds less than ECONOMY_HOME_DAYS of what its family drinks or eats, each counted in
    days of the family's bars, which fall a day's worth a day; firewood (step 3, HEAT) in its season, where it
    holds less than ECONOMY_HOME_DAYS of its burning or its fair share, whichever is less. `first` is the
    scarcest, in days - water on a tie, then food: thirst kills sooner. A camp counts its pantry and its
    supplies together (HomeHeld), since its families live on both, but has room only in its pantry. Nothing
    of a kind without room for a trip's worth - half a load, or half the room if that is less:
    a house too small for two days of both is always short, and was fetched for a unit at a time, every look.
    Food and water share the house's room; firewood has its woodshed.
*/
GoodsMask Economy::HomeShort(const EconomyWorker& k, const ZoneState& z, GoodsMask* first) const{
    uint32_t h = k.house;
    if (!h || h >= z.buildings.size() || h >= state.stock.size() || h >= stands.size() || !stands[h]
        || (z.buildings[h].kind != ZONE_KIND_HOUSE && z.buildings[h].kind != ZONE_KIND_CAMP)){
        return 0;
    }
    auto trip_fits = [&](int good){
        int room = (good == GOOD_WOOD) ? state.woodshed[h] : state.room[h];
        return room > 0 && HouseRoom(z,h,good) >= std::max(1,std::min((ECONOMY_LOAD + 1) / 2,(room + 1) / 2));
    };
    std::array<int,GOOD_COUNT> held = HomeHeld(z,h);
    int64_t living = std::max<int64_t>(1,Living(state.workers,h));
    int64_t enough = living * ECONOMY_HOME_DAYS * CALENDAR_DAY_TICKS;
    int64_t water = HeldTicks(held,GOOD_BIT(GOOD_WATER));
    int64_t food = HeldTicks(held,GOODS_FOOD);
    GoodsMask short_of = (GoodsMask)((water < enough && trip_fits(GOOD_WATER) ? GOOD_BIT(GOOD_WATER) : 0)
                                 | (food < enough && trip_fits(GOOD_WHEAT) ? GOODS_FOOD : 0));
    //Firewood's days are its burning's: ticks of it held, against water's and food's ticks a head.
    int64_t wood_ticks = INT64_MAX;
    if (FirewoodNow(h)){
        int wood = held[GOOD_WOOD];
        if (wood < std::min(state.burn[h] * ECONOMY_HOME_DAYS,FirewoodFair(z,h)) && trip_fits(GOOD_WOOD)){
            short_of |= GOOD_BIT(GOOD_WOOD);
            wood_ticks = (int64_t)wood * CALENDAR_DAY_TICKS / state.burn[h];
        }
    }
    if (first){
        *first = 0;
        int64_t least = INT64_MAX;
        const std::pair<GoodsMask,int64_t> kinds[3] = {{(GoodsMask)GOOD_BIT(GOOD_WATER),water / living},
                                                     {(GoodsMask)GOODS_FOOD,food / living},
                                                     {(GoodsMask)GOOD_BIT(GOOD_WOOD),wood_ticks}};
        for (const auto& kd : kinds){
            if ((short_of & kd.first) && (kd.second < least || !*first)){
                least = kd.second;
                *first = kd.first;
            }
        }
    }
    return short_of;
}

/*
    From home, before other work: off for a load for his house, if it is short (HomeShort) and he is the one to
    go - an idle member first; one with a job, between his rounds, only with no jobless member home to go
    instead - and nobody of the house is on the errand already. From the nearest place with some to spare that
    a home may fetch from (FetchFrom) - the camp's supplies are the colony's (the user's, 2026-10-07), and so,
    since 2026-10-09, are a collector's water and a woodcutter's pile; for food, a harvest lying on the house's
    own land too (land_plan.md: seen in its first run, a family fetching from the camp past its own full fields)
    - by the straight line, the lower id on a tie: the first of the nearest few he can walk to, and home from.
    The scarcer first; the other if nowhere has the scarcer. True if he set off.
*/
bool Economy::FetchFrom(const EconomyWorker& k, const ZoneState& z, uint32_t id) const{
    //A crate is a store anyone may take from (crates_plan.md), while it lies there.
    if (EconomyIsCrateStore(id)){
        return SourceStock(id) != NULL;
    }
    if (!id || id == k.house || id >= z.buildings.size() || id >= state.stock.size() || id >= stands.size()
        || z.buildings[id].size <= 0 || !stands[id]){
        return false;
    }
    switch (z.buildings[id].kind){
        case ZONE_KIND_STORE:
        case ZONE_KIND_CAMP:
        case ZONE_KIND_WATER:
        case ZONE_KIND_WOODCUTTER:
            return true;
        case ZONE_KIND_HOUSE:
            return !PrivateStock(z,id);         //nobody lives there: its stock is anyone's now
        case ZONE_KIND_FIELD:{
            const EconomyField* f = EconomyFieldOf(state,id);
            return f && f->owner == k.house && !HomeAhead(k,z);
        }
        default:
            return false;
    }
}

bool Economy::FetchHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    GoodsMask first = 0;
    GoodsMask short_of = HomeShort(k,z,&first);
    if (!short_of){
        return false;
    }
    //Idle for this: no job - or a mother caring for her baby (BIRTHS), home anyway.
    auto idle = [](const EconomyWorker& x){
        return x.job == WORKER_JOB_NONE || x.job == WORKER_JOB_CARING;
    };
    for (const EconomyWorker& o : state.workers){
        if (&o == &k || o.house != k.house){
            continue;
        }
        if (o.state == WORKER_TO_GET || o.state == WORKER_GETTING || o.state == WORKER_TO_PANTRY){
            return false;       //one errand a house at a time
        }
        if (!idle(k) && idle(o) && o.state == WORKER_AT_HOME && AtHome(o)){
            return false;       //an idle one is home: he goes, at his next look
        }
    }
    //What a place - a building or a crate - has of `goods` that nobody else fetching home already counts on: a load each.
    auto spare = [&](uint32_t id, GoodsMask goods){
        const std::array<int,GOOD_COUNT>* s = SourceStock(id);
        int left = 0;
        for (int g = 0; g < GOOD_COUNT && s; g++){
            left += (goods & GOOD_BIT(g)) ? (*s)[g] : 0;
        }
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.carry_good >= 0 && (goods & GOOD_BIT(o.carry_good))){
                left -= HomeReservation(o,id,o.carry_good);
            }
        }
        return left;
    };
    const ChasmWorld& w = *state.world;
    int home = HomePlot(k);
    size_t n = std::min(z.buildings.size(),state.stock.size());
    //The scarcest first, then the others a kind at a time: water, food, firewood.
    GoodsMask order[3] = {first,0,0};
    int kinds = 1;
    for (GoodsMask kd : {(GoodsMask)GOOD_BIT(GOOD_WATER),(GoodsMask)GOODS_FOOD,(GoodsMask)GOOD_BIT(GOOD_WOOD)}){
        if ((short_of & kd) && kd != first){
            order[kinds++] = kd;
        }
    }
    for (GoodsMask goods : order){
        if (!goods){
            continue;
        }
        std::vector<std::pair<float,uint32_t>> found;
        for (size_t id = 1; id < n; id++){
            int kind = z.buildings[id].kind;
            //A field's harvest for food only - its own land's, within its fair share (FetchFrom).
            if (!FetchFrom(k,z,(uint32_t)id) || (kind == ZONE_KIND_FIELD && goods != GOODS_FOOD)
                || spare((uint32_t)id,goods) <= 0){
                continue;
            }
            //A camp with people in it keeps its wood for its own fires (HEAT): they cannot fetch any more.
            if (kind == ZONE_KIND_CAMP && goods == GOOD_BIT(GOOD_WOOD) && Living(state.workers,(uint32_t)id) > 0){
                continue;
            }
            int plot = SourcePlot((uint32_t)id,k.pos);
            if (plot >= 0){
                found.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
            }
        }
        //...and the crates lying about (crates_plan.md): a store anyone may take from.
        for (const EconomyCrate& c : state.crates){
            uint32_t id = ECONOMY_STORE_CRATE | c.id;
            if (spare(id,goods) > 0){
                found.push_back(std::make_pair((c.pos - k.pos).length(),id));
            }
        }
        std::sort(found.begin(),found.end());
        for (size_t t = 0; t < found.size() && t < 4; t++){
            uint32_t id = found[t].second;
            int plot = SourcePlot(id,k.pos);
            vec2 at = SourcePoint(id,plot,k.pos);
            if (CanWalk(z,walkers,at,home) && RouteTo(k,z,walkers,plot,at)){
                k.store = id;
                k.carry_good = MostOf(*SourceStock(id),goods);   //the kind he is after; nothing carried yet
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
    house's room for it allow, and no more than brings the house to a day past ECONOMY_HOME_DAYS of it, so the
    next errand is a day off and water does not fill the room food needs (it did: nine water, no food, for a
    day) - nor, from its own land's field, past its fair share (FairShare) - and off home with it
    (WORKER_TO_PANTRY). Firewood the same, in days of its burning, into its woodshed, in its season, and never
    past its fair share (FirewoodFair). False, taking nothing, with none there, no room at home or no way back.
*/
bool Economy::TakeHome(EconomyWorker& k, const ZoneState& z, Walkers& walkers, uint32_t from, GoodsMask goods){
    uint32_t h = k.house;
    //A building's stock, or a crate's (crates_plan.md) - gone if it was emptied while he walked.
    std::array<int,GOOD_COUNT>* src = SourceStock(from);
    //Never from a home somebody lives in (WHOSE IT IS, Economy.h) - whoever asks.
    if (!src || !h || h >= z.buildings.size() || h >= state.room.size() || from == h
        || (z.buildings[h].kind != ZONE_KIND_HOUSE && z.buildings[h].kind != ZONE_KIND_CAMP) || PrivateStock(z,from)){
        return false;
    }
    int good = MostOf(*src,goods);
    if (good < 0){
        return false;
    }
    std::array<int,GOOD_COUNT> held = HomeHeld(z,h);
    int upto = 0;
    if (good == GOOD_WOOD){
        if (FirewoodNow(h)){
            upto = std::min(state.burn[h] * (ECONOMY_HOME_DAYS + 1),FirewoodFair(z,h)) - held[GOOD_WOOD];
        }
    }else{
        GoodsMask kind = (good == GOOD_WATER) ? (GoodsMask)GOOD_BIT(GOOD_WATER) : (GoodsMask)GOODS_FOOD;
        int worth = (good == GOOD_WATER) ? NEEDS_WATER_WORTH : EconomyFoodWorth(good);
        int64_t want = Living(state.workers,h) * (ECONOMY_HOME_DAYS + 1) * CALENDAR_DAY_TICKS - HeldTicks(held,kind);
        if (from < z.buildings.size() && z.buildings[from].kind == ZONE_KIND_FIELD){
            want = std::min(want,Living(state.workers,h) * FairShare(z) - HeldTicks(held,kind));
        }
        upto = (want > 0 && worth > 0) ? (int)((want + worth - 1) / worth) : 0;
    }
    int take = std::min(std::min(ECONOMY_LOAD,upto),std::min((*src)[good],HouseRoom(z,h,good)));
    if (take <= 0 || !RouteHome(k,z,walkers)){
        return false;
    }
    (*src)[good] -= take;
    CrateTouched(from);
    k.carry_good = good;
    k.carry = take;
    k.store = 0;
    k.prop = -1;
    k.state = WORKER_TO_PANTRY;
    state.version++;
    return true;
}

/*
    HEAT (Economy.h), at dawn: every inhabited home under the snow burns its firewood - a whole day's, or none
    - and is warm or cold until the next dawn; any other home wants no fire. The homeless (heat[0]) are cold
    under the snow at the camp's spot, where they wait.
*/
void Economy::Heat(const ZoneState& z, uint64_t tick){
    const ChasmWorld& w = *state.world;
    std::vector<int> living(state.heat.size(),0);
    for (const EconomyWorker& k : state.workers){
        if (k.house < living.size()){
            living[k.house]++;
        }
    }
    size_t n = std::min(std::min(state.heat.size(),z.buildings.size()),std::min(state.stock.size(),state.burn.size()));
    n = std::min(n,state.pantry.size());
    for (size_t id = 1; id < n; id++){
        uint8_t heat = HEAT_NONE;
        if (state.burn[id] > 0 && living[id] > 0 && id < plots_of.size() && !plots_of[id].empty()
            && UnderSnow(w,w.grid->fine.pos[plots_of[id][0]],tick)){
            //A camp's pantry first, then its supplies (STORES); a house's pantry is its stock, so this is only that.
            int& own = state.pantry[id][GOOD_WOOD];
            int& wood = state.stock[id][GOOD_WOOD];
            heat = (own + wood >= state.burn[id]) ? HEAT_WARM : HEAT_COLD;
            if (heat == HEAT_WARM){
                int first = std::min(own,state.burn[id]);
                own -= first;
                wood -= state.burn[id] - first;
            }
        }
        state.heat[id] = heat;
    }
    if (!state.heat.empty()){
        bool f_out = living[0] > 0 && state.camp_plot >= 0 && UnderSnow(w,w.grid->fine.pos[state.camp_plot],tick);
        state.heat[0] = f_out ? HEAT_COLD : HEAT_NONE;
    }
    state.version++;
}

int Economy::HouseRoom(const ZoneState& z, uint32_t h, int good) const{
    if (h >= state.stock.size() || h >= state.room.size() || h >= state.pantry.size() || good < 0 || good >= GOOD_COUNT){
        return 0;
    }
    //A camp's room is its pantry's: the supplies beside it are not its families' to fill.
    bool f_camp = h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_CAMP;
    const std::array<int,GOOD_COUNT>& s = f_camp ? state.pantry[h] : state.stock[h];
    if (good == GOOD_WOOD){
        return std::max(0,((h < state.woodshed.size()) ? state.woodshed[h] : 0) - s[GOOD_WOOD]);
    }
    return std::max(0,state.room[h] - (CarryTotal(s) - s[GOOD_WOOD]));
}

bool Economy::PrivateStock(const ZoneState& z, uint32_t id) const{
    return EconomyPrivateStock(state,z,id);
}

std::array<int,GOOD_COUNT>& Economy::HomePantry(const ZoneState& z, uint32_t h){
    bool f_camp = h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_CAMP && h < state.pantry.size();
    return f_camp ? state.pantry[h] : state.stock[h];
}

std::array<int,GOOD_COUNT> Economy::HomeHeld(const ZoneState& z, uint32_t h) const{
    std::array<int,GOOD_COUNT> held = (h < state.stock.size()) ? state.stock[h] : std::array<int,GOOD_COUNT>{};
    if (h < z.buildings.size() && z.buildings[h].kind == ZONE_KIND_CAMP && h < state.pantry.size()){
        for (int g = 0; g < GOOD_COUNT; g++){
            held[g] += state.pantry[h][g];
        }
    }
    return held;
}

/*
    A camp nobody lives in any more: what its families had fetched is nobody's, so it joins the supplies, where
    anyone may take it (the user's, 2026-10-09) - on the tick the last of them left, not carried anywhere.
*/
void Economy::OpenPantries(const ZoneState& z){
    size_t n = std::min(std::min(state.pantry.size(),state.stock.size()),z.buildings.size());
    for (size_t id = 1; id < n; id++){
        if (CarryTotal(state.pantry[id]) == 0 || Living(state.workers,(uint32_t)id) > 0){
            continue;
        }
        for (int g = 0; g < GOOD_COUNT; g++){
            state.stock[id][g] += state.pantry[id][g];
        }
        state.pantry[id] = std::array<int,GOOD_COUNT>{};
        state.version++;
    }
}

//Its family fetches firewood: a house the full winter's snow reaches, lived in, from autumn's first day to the
//snow leaving it in spring.
bool Economy::FirewoodNow(uint32_t h) const{
    if (h >= state.snowed.size() || !state.snowed[h] || state.burn[h] <= 0 || h >= plots_of.size() || plots_of[h].empty()
        || Living(state.workers,h) <= 0){
        return false;
    }
    CalendarDate d = CalendarDateOf(now);
    if (d.season == SEASON_AUTUMN || d.season == SEASON_WINTER){
        return true;
    }
    return d.season == SEASON_SPRING && UnderSnow(*state.world,state.world->grid->fine.pos[plots_of[h][0]],now);
}

/*
    FAIR SHARES of firewood (the user's, 2026-10-08): as food's, the colony's AT HOME - every house fetching
    firewood, its wood over their burning - so house h's share is its burning's part of it, and
    ECONOMY_FAIR_DAYS of its burning more. While wood is short the houses rise together, a day at a time.
*/
int Economy::FirewoodFair(const ZoneState& z, uint32_t h) const{
    int64_t wood = 0;
    int64_t burn = 0;
    size_t n = std::min(z.buildings.size(),std::min(state.stock.size(),state.burn.size()));
    for (size_t id = 1; id < n; id++){
        if (FirewoodNow((uint32_t)id)){
            wood += HomeHeld(z,(uint32_t)id)[GOOD_WOOD];
            burn += state.burn[id];
        }
    }
    if (h >= state.burn.size() || burn <= 0){
        return 0;
    }
    return (int)((wood * state.burn[h] + burn - 1) / burn) + state.burn[h] * ECONOMY_FAIR_DAYS;
}

/*
    Every person, every tick, after his round has moved on: a tick of water gone, and of health - three
    while dry - then a meal and a drink if he stands at home, or from his pack while he is posted. Health at
    nothing: he dies. He is struck from the colony with whatever he carried, his pack too; his family stays in
    its house, and on this same tick his job is given out again, and a house left empty taken by a family
    waiting for one.
*/
void Economy::Needs(const ZoneState& z, Walkers& walkers, uint64_t tick){
    bool f_died = false;
    for (EconomyWorker& k : state.workers){
        if (k.water > 0){
            k.water--;
        }
        k.health -= 1 + (EconomyDry(k) ? NEEDS_DRY_DRAIN : 0) + (EconomyCold(state,k) ? NEEDS_COLD_DRAIN : 0);
        EatAndDrink(k,z);
        if (EconomyPosted(k)){
            /*
                POSTS: a whole unit whenever it fits, from his pack - but on a tower from its stock as at home, his
                pantry, and from the pack only a kind the tower has none of: water while it has no water, food while
                it has no food. So the pack waits while the tower has any (a bean that fits where a provision does
                not was eaten from it).
            */
            if (uint32_t tower = EconomyOnTower(state,z,k)){
                std::array<int,GOOD_COUNT>& s = state.stock[tower];
                Consume(k,s);
                std::array<int,GOOD_COUNT> open = k.pack;
                if (s[GOOD_WATER] > 0){
                    open[GOOD_WATER] = 0;
                }
                if (MostOf(s,GOODS_EATEN) >= 0){
                    for (int g = 0; g < GOOD_COUNT; g++){
                        open[g] = (GOODS_EATEN & GOOD_BIT(g)) ? 0 : open[g];
                    }
                }
                std::array<int,GOOD_COUNT> before = open;
                Consume(k,open);
                for (int g = 0; g < GOOD_COUNT; g++){
                    k.pack[g] -= before[g] - open[g];
                }
            }else{
                Consume(k,k.pack);
            }
        }
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
        d.surname = k.surname;
        d.born = k.born;
        d.mother = k.mother;
        d.father = k.father;
        d.tick = tick;
        d.cause = EconomyDry(k) ? NEEDS_DIED_THIRST : EconomyCold(state,k) ? NEEDS_DIED_COLD : NEEDS_DIED_HUNGER;
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

/*
    What a kind's storey takes of a good other than wood, a plot-storey (SITE COSTS PER GOOD, defence_plan.md
    step 4) - by kind alone, not by area. A guard tower is one plot: its water and provisions, a storey's share
    (step 6) - which it KEEPS, as its stock (StoreyKept).
*/
int StoreyGoods(int kind, int good){
    if (kind == ZONE_KIND_TOWER){
        int storeys = std::max(1,ZoneKindMaxStoreys(ZONE_KIND_TOWER));
        return (good == GOOD_WATER) ? ECONOMY_TOWER_WATER / storeys
             : (good == GOOD_PROVISIONS) ? ECONOMY_TOWER_PROVISIONS / storeys : 0;
    }
    return 0;
}

//Of what a storey took, what goes into the building's stock rather than being used up: a tower's water and
//provisions, which its guards live on (defence_plan.md).
bool StoreyKept(int kind, int good){
    return kind == ZONE_KIND_TOWER && good != GOOD_WOOD;
}

}

int EconomyStoreyCost(const ChasmWorld& w, const ZoneState& z, int plot, int good){
    if (good == GOOD_WOOD){
        float wood = w.picker->PlotArea(plot) * ECONOMY_BUILD_WOOD_PER_AREA * BuildFactor(z.KindOf(plot));
        return std::max(1,(int)std::ceil(wood));
    }
    return StoreyGoods(z.KindOf(plot),good);
}

/*
    A site painted on coarse cells - a field - takes so much a cell still to build, by its kind and good: a
    field its wood. A kind on cells that takes more says so here.
*/
int EconomyCellCost(int kind, int good){
    switch (kind){
        case ZONE_KIND_FIELD:   return (good == GOOD_WOOD) ? ECONOMY_FIELD_WOOD_PER_CELL : 0;
        //An archery range (range_plan.md): wood for the stands, wheat for the bales.
        case ZONE_KIND_RANGE:   return (good == GOOD_WOOD) ? ECONOMY_RANGE_WOOD_PER_CELL
                                     : (good == GOOD_WHEAT) ? ECONOMY_RANGE_WHEAT_PER_CELL : 0;
        default:                return 0;
    }
}

//For the panel and the tools: the whole map's plots, so not for the tick (Economy::SiteNeed is).
std::array<int,GOOD_COUNT> EconomySiteCost(const ChasmWorld& w, const ZoneState& z, uint32_t id){
    std::array<int,GOOD_COUNT> cost{};
    //On cells (a field): so much a cell still a site.
    int cells = 0;
    bool f_cells = false;
    for (size_t c = 0; c < z.field.size(); c++){
        f_cells = f_cells || z.field[c] == id;
        cells += (z.field[c] == id && !z.field_built[c]) ? 1 : 0;
    }
    if (f_cells && id < z.buildings.size()){
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = cells * EconomyCellCost(z.buildings[id].kind,g);
        }
        return cost;
    }
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id && z.storeys[v] > z.standing[v]){
            for (int g = 0; g < GOOD_COUNT; g++){
                cost[g] += (z.storeys[v] - z.standing[v]) * EconomyStoreyCost(w,z,(int)v,g);
            }
        }
    }
    return cost;
}

int EconomySiteBrought(const EconomyState& e, uint32_t id, int good){
    return (id < e.site.size() && good >= 0 && good < GOOD_COUNT) ? e.site[id][good] : 0;
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

//What has been brought to a site, by good: a garden's, lot's or wall's plot takes wood only.
std::array<int,GOOD_COUNT> Economy::SiteBrought(uint32_t id) const{
    std::array<int,GOOD_COUNT> brought{};
    if (EconomyIsGroundSite(id)){
        brought[GOOD_WOOD] = EconomyGroundBrought(state,EconomyGroundSitePlot(id));
    }else if (id < state.site.size()){
        brought = state.site[id];
    }
    return brought;
}

//What a site still takes to finish, by good - all of it, whatever has been brought.
std::array<int,GOOD_COUNT> Economy::SiteNeed(const ZoneState& z, uint32_t id) const{
    std::array<int,GOOD_COUNT> need{};
    if (EconomyIsGroundSite(id)){
        int v = EconomyGroundSitePlot(id);
        need[GOOD_WOOD] = SiteAlive(z,id) ? EconomyGroundCost(*state.world,z,v) : 0;
        return need;
    }
    if (id >= plots_of.size()){
        return need;
    }
    //A field site: so much a cell still to build (fields_plan.md; EconomyCellCost).
    if (id < site_cells.size() && site_cells[id] > 0){
        for (int g = 0; g < GOOD_COUNT; g++){
            need[g] = site_cells[id] * EconomyCellCost(z.buildings[id].kind,g);
        }
        return need;
    }
    for (int v : plots_of[id]){
        if (z.storeys[v] > z.standing[v]){
            for (int g = 0; g < GOOD_COUNT; g++){
                need[g] += (z.storeys[v] - z.standing[v]) * EconomyStoreyCost(*state.world,z,v,g);
            }
        }
    }
    return need;
}

/*
    What a site still wants brought, by good: what it needs, less what it has and what is on its way - a
    load for each carrier still going to fetch it (he takes less if less is wanted), what each carries for
    it. `except` is left out, so a carrier can ask what is left for himself.
*/
std::array<int,GOOD_COUNT> Economy::SiteLeft(const ZoneState& z, uint32_t id, const EconomyWorker* except) const{
    std::array<int,GOOD_COUNT> left = SiteNeed(z,id);
    std::array<int,GOOD_COUNT> brought = SiteBrought(id);
    for (int g = 0; g < GOOD_COUNT; g++){
        left[g] -= brought[g];
    }
    for (const EconomyWorker& o : state.workers){
        if (&o == except || o.site != id || o.carry_good < 0 || o.carry_good >= GOOD_COUNT){
            continue;
        }
        if (o.state == WORKER_TO_FETCH || o.state == WORKER_FETCHING){
            left[o.carry_good] -= ECONOMY_LOAD;
        }else if (o.state == WORKER_TO_SITE){
            left[o.carry_good] -= o.carry;
        }
    }
    return left;
}

//A good at a building - or a crate - nobody is already on his way to take: a load for each carrier going there for it.
int Economy::GoodFree(uint32_t id, int good) const{
    const std::array<int,GOOD_COUNT>* s = SourceStock(id);
    int free = s ? (*s)[good] : 0;
    for (const EconomyWorker& o : state.workers){
        free -= SiteReservation(o,id,good);
    }
    return free;
}

//Where goods are taken from building `id`: a woodcutter's on his pile, the rest at its plot nearest `to`; a
//crate's (ECONOMY_STORE_CRATE) on the plot it lies on.
int Economy::SourcePlot(uint32_t id, const vec2& to) const{
    if (EconomyIsCrateStore(id)){
        const EconomyCrate* c = CrateById(EconomyCrateOfStore(id));
        return c ? c->plot : -1;
    }
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
    An idle person's errand: of every site that still wants a good and every building with that good to
    spare - a store, the camp's supplies, a house nobody lives in, a woodcutter's pile - or crate lying about
    with it (crates_plan.md), the pair with the
    shortest walk, by the straight line, from him to the good and on to the site (the lower ids, then the
    lower good, on a tie), in the player's order where the walks are close (below). The first of the nearest
    few he can walk to, and from whose goods the site can be walked to. Each good a site wants is an errand
    of its own (SITE COSTS PER GOOD), so a site wanting wood and water has them brought from wherever each
    is nearest.
*/
bool Economy::FindSiteWork(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    size_t n = std::min(z.buildings.size(),state.site.size());
    struct Wanting{
        uint32_t site;
        std::array<int,GOOD_COUNT> left;    //what it still wants of each good
    };
    struct Holding{
        uint32_t id;
        std::array<int,GOOD_COUNT> free;    //what it has of each good nobody is on his way to take
    };
    auto wants = [](const std::array<int,GOOD_COUNT>& a){
        return std::any_of(a.begin(),a.end(),[](int x){ return x > 0; });
    };
    std::vector<Wanting> sites;
    std::vector<Holding> sources;
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0){
            continue;
        }
        //A building's storeys still to stand, or a field's cells still a site (fields_plan.md).
        if (site_storeys[id] > 0 || site_cells[id] > 0){
            Wanting s{(uint32_t)id,SiteLeft(z,(uint32_t)id,&k)};
            if (wants(s.left)){
                sites.push_back(s);
            }
        }
        //A camp's supplies are the colony's; a house's stock only once nobody lives there (WHOSE IT IS, Economy.h).
        //A builders' hut's PROVISIONS too, made for the sites - never its wheat and beans (defence_plan.md).
        bool f_hut = b.kind == ZONE_KIND_BUILDERS;
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP || b.kind == ZONE_KIND_WOODCUTTER || f_hut
                       || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
        if (f_holds && stands[id]){
            Holding h{(uint32_t)id,{}};
            for (int g = 0; g < GOOD_COUNT; g++){
                h.free[g] = (!f_hut || g == GOOD_PROVISIONS) ? GoodFree((uint32_t)id,g) : 0;
            }
            if (wants(h.free)){
                sources.push_back(h);
            }
        }
    }
    //The crates (crates_plan.md): goods lying on the ground are a store's to anyone carrying for a site.
    for (const EconomyCrate& c : state.crates){
        Holding h{ECONOMY_STORE_CRATE | c.id,{}};
        for (int g = 0; g < GOOD_COUNT; g++){
            h.free[g] = GoodFree(h.id,g);
        }
        if (wants(h.free)){
            sources.push_back(h);
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
        Wanting s{site,SiteLeft(z,site,&k)};
        if (wants(s.left)){
            sites.push_back(s);
        }
    }
    struct Errand{
        float walk;
        uint32_t site, from;
        int from_plot;
        uint32_t key;       //its place in the placing order: a building's id, a road's or wall's line - 0 none
        int job;            //a piece of line work, its index in `jobs`; -1 for a carry
        int good;           //a carry's good
        bool operator<(const Errand& o) const{
            return (walk != o.walk) ? walk < o.walk : (site != o.site) ? site < o.site : (from != o.from) ? from < o.from
                 : good < o.good;
        }
    };
    std::vector<Errand> found;
    auto site_plot = [&](uint32_t s, const vec2& to){
        return EconomyIsGroundSite(s) ? GroundSiteApproach(z,EconomyGroundSitePlot(s)) : NearestPlot(s,to,true);
    };
    for (const Wanting& s : sites){
        for (const Holding& f : sources){
            int fp = -1;
            for (int g = 0; g < GOOD_COUNT; g++){
                if (s.left[g] <= 0 || f.free[g] <= 0){
                    continue;
                }
                if (fp < 0){
                    fp = SourcePlot(f.id,k.pos);
                }
                int sp = site_plot(s.site,w.grid->fine.pos[fp]);
                float walk = (w.grid->fine.pos[fp] - k.pos).length() + (w.grid->fine.pos[sp] - w.grid->fine.pos[fp]).length();
                uint32_t key = EconomyIsGroundSite(s.site) ? z.line[EconomyGroundSitePlot(s.site)] : s.site;
                found.push_back(Errand{walk,s.site,f.id,fp,key,-1,g});
            }
        }
    }
    //Line work - a tree in a road's or wall's way, a road plot to lay - in the same ranking, by the walk to it
    //and its line's place (Economy.h, THE QUEUE): no longer only what is left when there is no wood to carry.
    std::vector<LineJob> jobs;
    LineJobs(k,z,false,jobs);
    for (size_t j = 0; j < jobs.size(); j++){
        found.push_back(Errand{jobs[j].walk,ECONOMY_SITE_GROUND | (uint32_t)jobs[j].plot,(uint32_t)j,-1,
                               z.line[jobs[j].plot],(int)j,-1});
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
                 : (x.site != y.site) ? x.site < y.site : (x.from != y.from) ? x.from < y.from : x.good < y.good;
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
        vec2 at = SourcePoint(e.from,e.from_plot,k.pos);
        if (!CanWalk(z,walkers,at,site_plot(e.site,at))){
            continue;
        }
        if (RouteTo(k,z,walkers,e.from_plot,at)){
            k.site = e.site;
            k.store = e.from;
            k.prop = -1;
            k.carry_good = e.good;      //the kind he goes for, nothing carried yet
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
    A builder has finished with his load: every storey the site's goods now pay for stands - the lowest
    first, over the whole building, so it rises level by level; the lower plot index on a tie. Raised
    through the zones by the app, on this tick (Raises). What it took is used up.
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
    //A field site goes down all at once, every cell of it, when all it takes is there (fields_plan.md).
    if (id < site_cells.size() && site_cells[id] > 0){
        bool f_raised = false;
        for (int c : cells_of[id]){
            f_raised = f_raised || std::count(field_raises.begin(),field_raises.end(),c) > 0;
        }
        std::array<int,GOOD_COUNT> cost;
        bool f_all = true;
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = site_cells[id] * EconomyCellCost(z.buildings[id].kind,g);
            f_all = f_all && state.site[id][g] >= cost[g];
        }
        if (!f_raised && f_all){
            for (int g = 0; g < GOOD_COUNT; g++){
                state.site[id][g] -= cost[g];
            }
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
        //All it takes of every good, or it waits for the rest (SITE COSTS PER GOOD).
        std::array<int,GOOD_COUNT> cost;
        for (int g = 0; g < GOOD_COUNT; g++){
            cost[g] = EconomyStoreyCost(*state.world,z,best,g);
            if (state.site[id][g] < cost[g]){
                return;
            }
        }
        for (int g = 0; g < GOOD_COUNT; g++){
            state.site[id][g] -= cost[g];
            if (StoreyKept(z.buildings[id].kind,g)){
                state.stock[id][g] += cost[g];
            }
        }
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
    ground (not up or down a cliff) - and not COVERED (EconomyPropCovered): a tree in a woodcutter's fenced
    lot beside a road, which the view no longer drew and nobody could walk in to fell, held three plots of
    road unlaid for good (seen 2026-10-08, chasm_roadwork.rec). The edges WHOLE, not halves to their midpoints: the neighbour's half is
    felled for it too, which costs nothing, and leaves no tree under a road drawn later from that side.
    A wall plot's the same, along its edges toward the wall plots and gates beside it - its palisade's line
    (RoadMesh.h, WALLS). In bucket and index order, so the same trees in the same order every run.
*/
bool EconomyPropCovered(const ZoneState& z, int plot, int coarse){
    if (plot >= 0 && plot < (int)z.storeys.size() && (z.storeys[plot] > 0 || ZoneEnclosesGround(z.ground[plot]))){
        return true;
    }
    return coarse >= 0 && coarse < (int)z.field.size() && z.field[coarse];
}

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
                if (state.prop_state[i] != PROP_STATE_STANDING || pr.plot < 0 || !w.terrain->SameGround(pr.plot,v)
                    || EconomyPropCovered(z,pr.plot,pr.coarse)){
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

//--- Crates (crates_plan.md) -----------------------------------------------------------------------

const char* EconomyCrateOriginName(int origin){
    return (origin == CRATE_FOUND) ? "found" : "dropped";
}

bool EconomyCrateGround(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& at){
    if (plot < 0 || plot >= (int)z.building.size() || plot >= (int)w.grid->fine.pos.size()){
        return false;
    }
    if (z.building[plot] || z.storeys[plot] > 0 || (z.ground[plot] != ZONE_GROUND_NONE && z.ground[plot] != ZONE_GROUND_ROAD)
        || w.terrain->wet[plot] || w.terrain->Mountain(plot)){
        return false;
    }
    //A field is painted on coarse cells, not plots: the one under where the crate lies.
    GridPick p = w.picker->Pick(at);
    return !(p.f_hit && p.coarse_quad >= 0 && p.coarse_quad < (int)z.field.size() && z.field[p.coarse_quad]);
}

const EconomyCrate* EconomyCrateOn(const EconomyState& e, int plot){
    for (const EconomyCrate& c : e.crates){
        if (c.plot == plot){
            return &c;
        }
    }
    return NULL;
}

bool EconomyCrateInTheWay(const EconomyState& e, const ChasmWorld& w, int op, int index, int value){
    switch (op){
        case ZONE_OP_BUILD_PAINT:
        case ZONE_OP_BUILD_ADD:
            return EconomyCrateOn(e,index) != NULL;
        case ZONE_OP_GROUND_PAINT:
            return value != ZONE_GROUND_ROAD && EconomyCrateOn(e,index) != NULL;
        case ZONE_OP_FIELD_PAINT:
        case ZONE_OP_RANGE_PAINT:
            //A cell holds a crate whose point lies in it (EconomyCrateGround asks the same).
            for (const EconomyCrate& c : e.crates){
                GridPick p = w.picker->Pick(c.pos);
                if (p.f_hit && p.coarse_quad == index){
                    return true;
                }
            }
            return false;
        default:
            return false;     //a bridge's plots are wet, where no crate lies; the rest put nothing down
    }
}

void Economy::AddToCrate(int plot, const vec2& at, int good, int amount, int origin, uint32_t by){
    for (EconomyCrate& c : state.crates){
        if (c.plot == plot){
            c.stock[good] += amount;
            state.crates_version++;
            state.version++;
            return;
        }
    }
    EconomyCrate c;
    c.id = state.next_crate++;
    c.plot = plot;
    c.pos = at;
    c.stock[good] = amount;
    c.origin = origin;
    c.by = by;
    c.tick = state.tick;
    state.crates.push_back(c);      //ids rise, so the list stays in id order
    state.crates_version++;
    state.version++;
}

/*
    His load put down (CRATES). Standing at a store that takes it - unloading there, or just arrived - it goes
    in, as far as the store has room: that is where it was going. The rest is a crate: on his own plot if that is
    open ground, at his feet; else on the open plot nearest him within ECONOMY_CRATE_RINGS steps - stepping on
    only across ground people walk over (open ground, a camp among its tents, a standing bridge) and on his own
    level, so it never lands behind a wall, past a house or down a cliff from him - at the plot's point, the
    lower index on a tie. In the middle of the camp, its tents all round him, that is a few steps out (found in
    its first test: stepping only over open ground, a load put down there was lost). With no open ground that
    near, which a person standing anywhere he can walk to should never meet, the load is lost, as every load was
    before crates.
*/
void Economy::DropLoad(EconomyWorker& k, const ZoneState& z){
    const ChasmWorld& w = *state.world;
    int good = k.carry_good;
    int amount = k.carry;
    k.carry = 0;
    k.carry_good = -1;
    if (amount <= 0 || good < 0 || good >= GOOD_COUNT){
        return;
    }
    GridPick here = w.picker->Pick(k.pos);
    int start = here.f_hit ? here.plot : -1;
    if (start < 0 || start >= (int)z.building.size()){
        return;
    }
    uint32_t s = z.building[start];
    if (s && s < state.accepts.size() && (state.accepts[s] & GOOD_BIT(good))){
        int put = std::min(amount,std::max(0,state.room[s] - CarryTotal(state.stock[s])));
        state.stock[s][good] += put;
        amount -= put;
        state.version++;
    }
    if (amount <= 0){
        return;
    }
    if (EconomyCrateGround(w,z,start,k.pos)){
        //Where a crate already lies on his plot, the load goes into it, which stays where it is.
        AddToCrate(start,k.pos,good,amount,CRATE_DROPPED,k.id);
        return;
    }
    std::vector<int> seen(1,start);
    std::vector<int> ring(1,start);
    std::vector<int> next;
    int best = -1;
    float best_d = 1e30f;
    for (int r = 0; r < ECONOMY_CRATE_RINGS && best < 0; r++){
        next.clear();
        for (int v : ring){
            int nb[8];
            int n = ZonePlotNeighbours(w,v,nb,8);
            for (int i = 0; i < n; i++){
                int o = nb[i];
                if (std::find(seen.begin(),seen.end(),o) != seen.end() || !w.terrain->SameGround(o,start)){
                    continue;
                }
                seen.push_back(o);
                bool f_open = EconomyCrateGround(w,z,o,w.grid->fine.pos[o]);
                if (f_open || z.KindOf(o) == ZONE_KIND_CAMP || ZoneBridgeWalkable(z,o)){
                    next.push_back(o);
                }
                if (!f_open){
                    continue;
                }
                float d = (w.grid->fine.pos[o] - k.pos).length();
                if (d < best_d || (d == best_d && o < best)){
                    best_d = d;
                    best = o;
                }
            }
        }
        ring.swap(next);
    }
    if (best < 0){
        return;
    }
    AddToCrate(best,w.grid->fine.pos[best],good,amount,CRATE_DROPPED,k.id);
}

//A crate whose ground has been built over - a debug placement, which the play rules refuse - is lost with it.
void Economy::LoseCoveredCrates(const ZoneState& z){
    const ChasmWorld& w = *state.world;
    size_t before = state.crates.size();
    state.crates.erase(std::remove_if(state.crates.begin(),state.crates.end(),[&](const EconomyCrate& c){
        return !EconomyCrateGround(w,z,c.plot,c.pos);
    }),state.crates.end());
    if (state.crates.size() != before){
        state.crates_version++;
        state.version++;
    }
}

bool Economy::SpawnCrate(int plot, int good, int amount, const ZoneState& z, std::string* why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!state.world || z.world != state.world){
        return refuse("no colony");
    }
    if (good < 0 || good >= GOOD_COUNT || amount <= 0){
        return refuse("no goods");
    }
    const ChasmWorld& w = *state.world;
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size()){
        return refuse("no such plot");
    }
    if (!EconomyCrateGround(w,z,plot,w.grid->fine.pos[plot])){
        return refuse("not open ground");
    }
    AddToCrate(plot,w.grid->fine.pos[plot],good,amount,CRATE_FOUND,0);
    return true;
}

const EconomyCrate* Economy::CrateById(uint32_t id) const{
    for (const EconomyCrate& c : state.crates){
        if (c.id == id){
            return &c;
        }
    }
    return NULL;
}

std::array<int,GOOD_COUNT>* Economy::SourceStock(uint32_t id){
    return const_cast<std::array<int,GOOD_COUNT>*>(static_cast<const Economy*>(this)->SourceStock(id));
}

const std::array<int,GOOD_COUNT>* Economy::SourceStock(uint32_t id) const{
    if (EconomyIsCrateStore(id)){
        const EconomyCrate* c = CrateById(EconomyCrateOfStore(id));
        return c ? &c->stock : NULL;
    }
    return (id && id < state.stock.size()) ? &state.stock[id] : NULL;
}

//A crate taken from: the view told, and - emptied - off the map (the user's: gone when empty).
void Economy::CrateTouched(uint32_t store){
    if (!EconomyIsCrateStore(store)){
        return;
    }
    uint32_t id = EconomyCrateOfStore(store);
    state.crates.erase(std::remove_if(state.crates.begin(),state.crates.end(),[&](const EconomyCrate& c){
        return c.id == id && CarryTotal(c.stock) <= 0;
    }),state.crates.end());
    state.crates_version++;
    state.version++;
}

//Beside it, on the side he comes from - not in it, as he would stand at the foot of a tree (GoForWood).
vec2 Economy::CrateStand(const EconomyCrate& c, const vec2& from) const{
    vec2 to = c.pos - from;
    float l = to.length();
    return (l > ECONOMY_CRATE_STAND * 2.0f) ? c.pos - to * (ECONOMY_CRATE_STAND / l) : c.pos;
}

vec2 Economy::SourcePoint(uint32_t id, int plot, const vec2& from) const{
    const EconomyCrate* c = EconomyIsCrateStore(id) ? CrateById(EconomyCrateOfStore(id)) : NULL;
    return c ? CrateStand(*c,from) : state.world->grid->fine.pos[plot];
}

/*
    THE CARETAKERS' LAST ERRAND (crates_plan.md, the user's): a crate's goods to a storehouse - only a builder's,
    and only once there is nothing to carry or build and no line work (TickWorker). Of the crates holding a good
    some store takes a load of - its whole load, with the room - and that he could walk on to that store from,
    the nearest by the straight line, the lower id on a tie: the first of the nearest few he can walk to. A
    crate another caretaker is already on his way to only while it holds more than that one's load.
*/
bool Economy::MoveCrate(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    //The nearest store that takes `amount` of `good` whole, by the straight line from `at` - its plot nearest
    //there - or -1.
    auto store_plot = [&](int good, int amount, const vec2& at){
        int best = -1;
        float best_d = 1e30f;
        for (size_t s = 1; s < state.accepts.size() && s < plots_of.size(); s++){
            if (!(state.accepts[s] & GOOD_BIT(good)) || state.room[s] - CarryTotal(state.stock[s]) < amount){
                continue;
            }
            for (int v : plots_of[s]){
                float d = (w.grid->fine.pos[v] - at).length();
                if (d < best_d){
                    best_d = d;
                    best = v;
                }
            }
        }
        return best;
    };
    std::vector<std::pair<float,uint32_t>> found;
    for (const EconomyCrate& c : state.crates){
        uint32_t id = ECONOMY_STORE_CRATE | c.id;
        int left = CarryTotal(c.stock);
        for (const EconomyWorker& o : state.workers){
            if (&o != &k && o.store == id && (o.state == WORKER_TO_CRATE || o.state == WORKER_LIFTING)){
                left -= ECONOMY_LOAD;
            }
        }
        if (left > 0){
            found.push_back(std::make_pair((c.pos - k.pos).length(),id));
        }
    }
    std::sort(found.begin(),found.end());
    for (size_t t = 0; t < found.size() && t < 4; t++){
        const EconomyCrate* c = CrateById(EconomyCrateOfStore(found[t].second));
        int good = -1;
        for (int g = 0; g < GOOD_COUNT && good < 0; g++){
            int sp = (c->stock[g] > 0) ? store_plot(g,std::min(ECONOMY_LOAD,c->stock[g]),c->pos) : -1;
            if (sp >= 0 && CanWalk(z,walkers,c->pos,sp)){
                good = g;
            }
        }
        if (good < 0){
            continue;
        }
        if (RouteTo(k,z,walkers,c->plot,CrateStand(*c,k.pos))){
            k.store = found[t].second;
            k.carry_good = good;        //the kind he goes for; nothing carried yet
            k.carry = 0;
            k.prop = -1;
            k.state = WORKER_TO_CRATE;
            return true;
        }
    }
    return false;
}

//--- Provisions (docs/defence_plan.md, step 5) -------------------------------------------------------

/*
    How many provisions are still wanted beyond what is made: what the sites still lack of them (SiteLeft,
    less what is on its way) - towers short of them come with step 7 - less what lies made where the sites'
    carriers take from (a store, the camp's supplies, a house nobody lives in, a builders' hut, a crate) and
    what is being made. Below 0: made and wanted by nobody. Wheat and beans at the huts are not counted -
    they are made into provisions only while this is above 0.
*/
int Economy::ProvisionsLacking(const ZoneState& z) const{
    int lacking = 0;
    size_t n = std::min(z.buildings.size(),state.site.size());
    for (size_t id = 1; id < n; id++){
        const ZoneBuilding& b = z.buildings[id];
        if (b.size <= 0){
            continue;
        }
        if (site_storeys[id] > 0 || site_cells[id] > 0){
            lacking += std::max(0,SiteLeft(z,(uint32_t)id,NULL)[GOOD_PROVISIONS]);
        }
        bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP || b.kind == ZONE_KIND_BUILDERS
                       || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
        if (f_holds && stands[id]){
            lacking -= std::max(0,GoodFree((uint32_t)id,GOOD_PROVISIONS));
        }
    }
    for (const EconomyCrate& c : state.crates){
        lacking -= std::max(0,GoodFree(ECONOMY_STORE_CRATE | c.id,GOOD_PROVISIONS));
    }
    for (const EconomyWorker& o : state.workers){
        lacking -= (o.state == WORKER_MAKING) ? 1 : 0;
    }
    return lacking;
}

/*
    A builder's provisions errand, from home - before his building work (the user's order: towers, then
    provisions, then building, then crates). While some are wanted (ProvisionsLacking): to his hut to make
    them, with a wheat and a beans there; else, with too few on hand and on their way at every hut for what is
    wanted, for a load of the scarcer of the two at his own hut - from the nearest store, camp's supplies,
    house nobody lives in or crate with some to spare. With none wanted, to his hut for a load of what lies
    there for nobody, to a store that takes it. False if none of that is to do, or he can get to none of it.
*/
bool Economy::MakeProvisions(EconomyWorker& k, const ZoneState& z, Walkers& walkers){
    const ChasmWorld& w = *state.world;
    uint32_t h = k.building;
    if (k.job != WORKER_JOB_BUILDER || !Workshop(z,stands,h)){
        return false;
    }
    const std::array<int,GOOD_COUNT>& hut = state.stock[h];
    int lacking = ProvisionsLacking(z);
    auto to_hut = [&](){
        int plot = NearestPlot(h,k.pos);
        if (plot < 0 || !RouteTo(k,z,walkers,plot,w.grid->fine.pos[plot])){
            return false;
        }
        k.store = 0;
        k.site = 0;
        k.prop = -1;
        k.stint = 0;
        k.state = WORKER_TO_WORKSHOP;
        return true;
    };
    if (lacking > 0 && hut[GOOD_WHEAT] > 0 && hut[GOOD_BEANS] > 0){
        return to_hut();
    }
    if (lacking <= 0){
        //Something there nobody wants any more, and a store with room for a load of it (as AtWorkshop takes it).
        for (int g : {GOOD_PROVISIONS,GOOD_WHEAT,GOOD_BEANS}){
            int load = std::min(ECONOMY_LOAD,(g == GOOD_PROVISIONS) ? std::min(hut[g],-lacking) : hut[g]);
            for (size_t s = 1; load > 0 && s < state.accepts.size(); s++){
                if ((state.accepts[s] & GOOD_BIT(g)) && state.room[s] - CarryTotal(state.stock[s]) >= load){
                    return to_hut();
                }
            }
        }
        return false;
    }
    //What every hut will make with what it has and what is on its way to it: its wheat and beans, in pairs.
    auto coming = [&](uint32_t hh, int good){
        int n = 0;
        for (const EconomyWorker& o : state.workers){
            if (o.building != hh || o.carry_good != good){
                continue;
            }
            n += (o.state == WORKER_TO_INPUT || o.state == WORKER_TAKING_INPUT) ? o.stint
               : (o.state == WORKER_TO_WORKSHOP) ? o.carry : 0;
        }
        return n;
    };
    int will_make = 0;
    int mine[2] = {0,0};
    for (size_t hh = 1; hh < z.buildings.size() && hh < state.stock.size(); hh++){
        if (!Workshop(z,stands,(uint32_t)hh)){
            continue;
        }
        int wheat = state.stock[hh][GOOD_WHEAT] + coming((uint32_t)hh,GOOD_WHEAT);
        int beans = state.stock[hh][GOOD_BEANS] + coming((uint32_t)hh,GOOD_BEANS);
        will_make += std::min(wheat,beans);
        if (hh == h){
            mine[0] = wheat;
            mine[1] = beans;
        }
    }
    int pairs = lacking - will_make;
    if (pairs <= 0){
        return false;
    }
    //Where wheat or beans can be had: a store, the camp's supplies, a house nobody lives in, a crate - with
    //some nobody else is on his way for. By the straight line from him.
    auto sources = [&](int good, std::vector<std::pair<float,uint32_t>>& out){
        out.clear();
        for (size_t id = 1; id < z.buildings.size() && id < stands.size(); id++){
            const ZoneBuilding& b = z.buildings[id];
            bool f_holds = b.kind == ZONE_KIND_STORE || b.kind == ZONE_KIND_CAMP
                           || (b.kind == ZONE_KIND_HOUSE && !PrivateStock(z,(uint32_t)id));
            int plot = (b.size > 0 && f_holds && stands[id] && GoodFree((uint32_t)id,good) > 0) ? SourcePlot((uint32_t)id,k.pos) : -1;
            if (plot >= 0){
                out.push_back(std::make_pair((w.grid->fine.pos[plot] - k.pos).length(),(uint32_t)id));
            }
        }
        for (const EconomyCrate& c : state.crates){
            uint32_t id = ECONOMY_STORE_CRATE | c.id;
            if (GoodFree(id,good) > 0){
                out.push_back(std::make_pair((c.pos - k.pos).length(),id));
            }
        }
        std::sort(out.begin(),out.end());
    };
    /*
        The scarcer at his hut - wheat on a tie - as many as the pairs still wanted (each pair takes one more of
        it). Only as many as pair with what is there of the other, where none of the other is to be had: no
        wheat or no beans, no provisions (defence_plan.md), and nothing fetched to wait at the hut for its half.
    */
    int good = (mine[0] <= mine[1]) ? GOOD_WHEAT : GOOD_BEANS;
    int other = (good == GOOD_WHEAT) ? GOOD_BEANS : GOOD_WHEAT;
    int diff = std::abs(mine[0] - mine[1]);
    std::vector<std::pair<float,uint32_t>> found;
    sources(other,found);
    int amount = std::min(ECONOMY_LOAD,found.empty() ? std::min(diff,pairs) : pairs);
    if (amount <= 0){
        return false;
    }
    sources(good,found);
    for (size_t t = 0; t < found.size() && t < 4; t++){
        uint32_t id = found[t].second;
        int plot = SourcePlot(id,k.pos);
        if (plot >= 0 && RouteTo(k,z,walkers,plot,SourcePoint(id,plot,k.pos))){
            k.store = id;
            k.carry_good = good;        //the kind he goes for; nothing carried yet
            k.carry = 0;
            k.stint = std::min(amount,GoodFree(id,good));
            k.site = 0;
            k.prop = -1;
            k.state = WORKER_TO_INPUT;
            return true;
        }
    }
    return false;
}

/*
    At his builders' hut (WORKER_TO_WORKSHOP arrived, or a provision made): wheat or beans he brought go into
    its stock; then, while provisions are wanted and it has a wheat and a beans, he makes one - taking them at
    the start - up to a load a round; else, with none wanted, a load of what lies there for nobody goes to a
    store that takes it; else home. His hut gone, or no longer his: what he holds to a store.
*/
void Economy::AtWorkshop(EconomyWorker& k, const ZoneState& z, Walkers& walkers, float pace){
    uint32_t h = k.building;
    if (k.job != WORKER_JOB_BUILDER || !Workshop(z,stands,h)){
        if (k.carry > 0 && !FindStore(k,z,walkers)){
            k.state = WORKER_HOLDING;
        }else if (k.carry <= 0){
            GoHome(k,z,walkers);
        }
        return;
    }
    std::array<int,GOOD_COUNT>& hut = state.stock[h];
    if (k.carry > 0 && k.carry_good >= 0){
        hut[k.carry_good] += k.carry;
        k.carry = 0;
        k.carry_good = -1;
        k.stint = 0;
        state.version++;
    }
    int lacking = ProvisionsLacking(z);
    if (lacking > 0 && hut[GOOD_WHEAT] > 0 && hut[GOOD_BEANS] > 0 && k.stint < ECONOMY_LOAD){
        hut[GOOD_WHEAT]--;
        hut[GOOD_BEANS]--;
        k.state = WORKER_MAKING;
        k.timer = SecondsToTicks(ECONOMY_PROVISION_SECONDS / pace);
        state.version++;
        return;
    }
    if (lacking <= 0){
        for (int g : {GOOD_PROVISIONS,GOOD_WHEAT,GOOD_BEANS}){
            int spare = (g == GOOD_PROVISIONS) ? std::min(hut[g],-lacking) : hut[g];
            if (spare <= 0){
                continue;
            }
            k.carry = std::min(ECONOMY_LOAD,spare);
            k.carry_good = g;
            hut[g] -= k.carry;
            if (FindStore(k,z,walkers)){
                state.version++;
                return;
            }
            hut[g] += k.carry;      //no store takes it: it stays at the hut
            k.carry = 0;
            k.carry_good = -1;
        }
    }
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
    for (size_t id = 1; id < state.pantry.size(); id++){
        if (CarryTotal(state.pantry[id]) > 0){
            s.pantries.push_back(std::make_pair((uint32_t)id,state.pantry[id]));
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
        if (std::any_of(state.site[id].begin(),state.site[id].end(),[](int x){ return x > 0; })){
            s.sites.push_back(std::make_pair((uint32_t)id,state.site[id]));
        }
    }
    for (size_t v = 0; v < state.ground_site.size(); v++){
        if (state.ground_site[v] > 0){
            s.ground_sites.push_back(std::make_pair((int)v,state.ground_site[v]));
        }
    }
    for (size_t id = 0; id < state.heat.size(); id++){
        if (state.heat[id] != HEAT_NONE){
            s.heats.push_back(std::make_pair((uint32_t)id,(int)state.heat[id]));
        }
    }
    s.crates = state.crates;
    s.next_crate = state.next_crate;
    s.next_person = state.next_person;
    s.dead = state.dead;
    return s;
}

/*
    Put back exactly as saved - the workers on their routes, not planned again (a re-plan could find
    another route of the same cost, and a replay has to be the same walk). Then what follows from the
    zones is worked out, with no stock shared out: the save's already is.
*/
void Economy::Restore(std::shared_ptr<const ChasmWorld> world, const EconomySaved& saved, const ZoneState& z,
                      Walkers& walkers, uint64_t calendar_tick){
    Reset(world);
    state.tick = calendar_tick;
    now = calendar_tick;
    if (!world || z.world != world){
        return;
    }
    state.stock.assign(z.buildings.size(),std::array<int,GOOD_COUNT>{});
    for (const auto& st : saved.stocks){
        if (st.first < state.stock.size()){
            state.stock[st.first] = st.second;
        }
    }
    state.pantry.assign(z.buildings.size(),std::array<int,GOOD_COUNT>{});
    for (const auto& st : saved.pantries){
        if (st.first < state.pantry.size()){
            state.pantry[st.first] = st.second;
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
        //A save from before site costs per good: a carrier on his way to fetch named no good - it was wood.
        for (EconomyWorker& k : state.workers){
            if ((k.state == WORKER_TO_FETCH || k.state == WORKER_FETCHING) && k.carry_good < 0){
                k.carry_good = GOOD_WOOD;
            }
        }
    }
    state.fields = saved.fields;
    state.site.assign(z.buildings.size(),std::array<int,GOOD_COUNT>{});
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
    state.heat.assign(std::max<size_t>(z.buildings.size(),1),HEAT_NONE);
    for (const auto& h : saved.heats){
        if (h.first < state.heat.size()){
            state.heat[h.first] = (uint8_t)h.second;
        }
    }
    //CRATES as they lay. A save from before them has none.
    state.crates = saved.crates;
    state.next_crate = saved.next_crate;
    for (const EconomyCrate& c : state.crates){
        state.next_crate = std::max(state.next_crate,c.id + 1);
    }
    state.crates_version++;
    Derive(z,walkers,false);
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

GoodsMask EconomyAccepts(const EconomyState& e, uint32_t id){
    return (id < e.accepts.size()) ? e.accepts[id] : 0;
}

int EconomyStock(const EconomyState& e, uint32_t id, int good){
    return (id < e.stock.size() && good >= 0 && good < GOOD_COUNT) ? e.stock[id][good] : 0;
}

int EconomyStockTotal(const EconomyState& e, uint32_t id){
    return (id < e.stock.size()) ? CarryTotal(e.stock[id]) : 0;
}

bool EconomyPrivateStock(const EconomyState& e, const ZoneState& z, uint32_t id){
    return id && id < z.buildings.size() && z.buildings[id].kind == ZONE_KIND_HOUSE && Living(e.workers,id) > 0;
}

std::array<int,GOOD_COUNT> EconomyReserved(const EconomyState& e, uint32_t id){
    std::array<int,GOOD_COUNT> r{};
    for (const EconomyWorker& o : e.workers){
        if (o.store == id && o.carry_good >= 0 && o.carry_good < GOOD_COUNT){
            r[o.carry_good] += SiteReservation(o,id,o.carry_good) + HomeReservation(o,id,o.carry_good);
        }
    }
    return r;
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

namespace {

/*
    What AUTO ranks crops by (fields_plan.md, "AUTO by food", the user's 2026-10-08): the health a crop
    gives per day it grows, sown after `last` - its yield per area x the rotation's percent x what a unit
    is worth eaten, over its growing days. By percent alone it never sowed beans, the most food there is:
    they tied with wheat or greens at 100% and lost on table order. Area and pace are the same for
    every crop, so they are left out.
*/
double CropFood(int last, int crop){
    return (double)crop_yield[crop] * (double)EconomyCropYield(last,crop)
           * (double)EconomyFoodWorth(GoodOfCrop(crop)) / (double)crop_days[crop];
}

}

int EconomyAutoCrop(int last){
    int best = 0;
    for (int c = 1; c < ZONE_CROP_COUNT; c++){
        if (CropFood(last,c) > CropFood(last,best)){
            best = c;
        }
    }
    return best;
}

int EconomyFieldSowCrop(const EconomyField& f, const ZoneBuilding& b){
    if (f.last < 0){
        //Its first: the player's choice - AUTO too, his or the countdown's (issues_plan.md) - or nothing yet.
        return (b.crop < ZONE_CROP_COUNT) ? (int)b.crop : (b.crop == ZONE_CROP_AUTO) ? EconomyAutoCrop(-1) : -1;
    }
    return (b.next_crop < ZONE_CROP_COUNT) ? (int)b.next_crop : EconomyAutoCrop(f.last);
}

int EconomyGrowingTicksLeft(const ChasmWorld& w, const vec2& p, uint64_t tick){
    CalendarDate d = CalendarDateOf(tick);
    if (d.season == SEASON_WINTER || UnderSnow(w,p,tick)){
        return 0;
    }
    //This year's winter - and before it, the snow, where it comes early: looked for a quarter day at a time,
    //as the front only ever reaches a place over autumn's last days.
    const uint64_t year = (uint64_t)CALENDAR_SEASONS * CALENDAR_SEASON_TICKS;
    uint64_t end = (tick / year) * year + (uint64_t)SEASON_WINTER * CALENDAR_SEASON_TICKS;
    const uint64_t step = CALENDAR_DAY_TICKS / 4;
    for (uint64_t t = tick + step; t < end; t += step){
        if (UnderSnow(w,p,t)){
            end = t;
            break;
        }
    }
    return (int)(end - tick);
}

int EconomyGrowingSeasonTicks(const ChasmWorld& w, const vec2& p, uint64_t tick){
    const uint64_t year = (uint64_t)CALENDAR_SEASONS * CALENDAR_SEASON_TICKS;
    uint64_t t = (tick / year + 1) * year;      //next spring's first tick
    const uint64_t thaw = t + CALENDAR_SEASON_TICKS;
    while (t < thaw && UnderSnow(w,p,t)){
        t += CALENDAR_DAY_TICKS / 4;
    }
    return EconomyGrowingTicksLeft(w,p,t);
}

int EconomyRipenTicks(int crop, int grown, float pace){
    int per = (int)std::lround(pace * 100.0f);      //what a tick grows, as the fields' loop adds it
    if (crop < 0 || crop >= ZONE_CROP_COUNT || per <= 0){
        return 1 << 30;
    }
    int need = std::max(0,crop_days[crop] * CALENDAR_DAY_TICKS * 100 - grown);
    return (need + per - 1) / per;
}

int EconomyFieldSowNow(const EconomyField& f, const ZoneBuilding& b, float pace, int ticks_left){
    auto ripens = [&](int c){
        return EconomyRipenTicks(c,0,pace) <= ticks_left;
    };
    bool f_auto = (f.last >= 0) ? b.next_crop >= ZONE_CROP_COUNT : b.crop == ZONE_CROP_AUTO;
    if (f_auto){
        //AUTO: the most food of the crops still in time, the earlier crop on a tie.
        int best = -1;
        for (int c = 0; c < ZONE_CROP_COUNT; c++){
            if (ripens(c) && (best < 0 || CropFood(f.last,c) > CropFood(f.last,best))){
                best = c;
            }
        }
        return best;
    }
    int crop = EconomyFieldSowCrop(f,b);
    return (crop >= 0 && ripens(crop)) ? crop : -1;
}

float EconomyFieldPace(const EconomyState& e, uint32_t id){
    uint32_t farmer = (id < e.farmer_of.size()) ? e.farmer_of[id] : 0;
    for (const EconomyWorker& k : e.workers){
        if (farmer && k.id == farmer){
            return (k.post >= 0) ? 0.0f : EconomySkillFactor(k.skills[SKILL_HUSBANDRY]);    //posted: it waits (Tending)
        }
    }
    return 0.0f;
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

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
        default:                return "?";
    }
}

const char* EconomyJobName(int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER: return "woodcutter";
        case WORKER_JOB_FARMER:     return "farmer";
        case WORKER_JOB_WATER:      return "water_carrier";
        default:                    return "?";
    }
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
    int level = (end_plot_a >= 0) ? t.level[end_plot_a] : -1;
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
        if (level < 0){
            level = t.level[v];
        }
        bool f_end = (v == end_plot_a || v == end_plot_b);
        if (t.level[v] != level){
            return false;
        }
        if (!f_end){
            if (t.wet[v] || t.Mountain(v) || z.storeys[v] > 0 || AllInField(w,z,v)){
                return false;
            }
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
            if (from && from < known && z.buildings[id].kind == ZONE_KIND_STORE){
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
    //Gone buildings hold nothing: a store pulled down loses what it had.
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].size <= 0){
            state.stock[id] = std::array<int,GOOD_COUNT>{};
        }
    }
    /*
        What each store takes, and its room. Next to a workplace: that workplace's goods - a woodcutter
        or a collector sharing a fine edge with a store plot, or a field one plot away (buildings keep a
        plot off fields, so beside one is as close as a store gets).
    */
    state.accepts.assign(n,0);
    state.attached.assign(n,0);
    state.room.assign(n,0);
    for (size_t id = 1; id < n; id++){
        if (z.buildings[id].kind != ZONE_KIND_STORE || z.buildings[id].size <= 0){
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
        state.room[id] = (int)std::floor(ZoneBuildingFigures(w,z,(uint32_t)id).floor_area * ECONOMY_STORE_ROOM);
    }
    /*
        A worker for every workplace and none for anything else, and a growing record for every field -
        both kept in id order. A worker whose workplace is gone goes with it; what he carried is lost,
        though a log he felled stays where it fell for another to take.
    */
    std::vector<EconomyWorker> workers;
    size_t at = 0;
    for (size_t id = 1; id < n; id++){
        while (at < state.workers.size() && state.workers[at].building < id){
            at++;
        }
        int job = (z.buildings[id].size > 0) ? JobOf(z.buildings[id].kind) : -1;
        if (job < 0 || home_plot[id] < 0){
            continue;
        }
        if (at < state.workers.size() && state.workers[at].building == id && state.workers[at].job == job){
            workers.push_back(state.workers[at]);
        }else{
            EconomyWorker k;
            k.building = (uint32_t)id;
            k.job = job;
            k.pos = g.fine.pos[home_plot[id]];
            k.route.push_back(k.pos);
            workers.push_back(k);
        }
    }
    state.workers.swap(workers);
    std::vector<EconomyField> fields;
    at = 0;
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
        if (k.state != WORKER_TO_WORK && k.state != WORKER_TO_STORE && k.state != WORKER_TO_HOME){
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
        }else if (k.state == WORKER_TO_HOME && k.building < home_plot.size()){
            goal_plot = home_plot[k.building];
        }
        if (!RouteTo(k,z,walkers,goal_plot,goal)){
            //No way now: stop where he is and think again at the next look.
            k.route.assign(1,k.pos);
            k.speed.clear();
            k.seg = 0;
            k.along = 0.0f;
            k.state = (k.carry > 0) ? WORKER_HOLDING : WORKER_AT_HOME;
            k.prop = -1;
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
    int home = home_plot[k.building];
    k.prop = -1;
    k.store = 0;
    if (RouteTo(k,z,walkers,home,state.world->grid->fine.pos[home])){
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
    bool f_look = ((tick + k.building) % ECONOMY_LOOK_TICKS) == 0;
    std::array<int,GOOD_COUNT>& own = state.stock[k.building];
    //Is there a store that takes `amount` of `good` and has the room? Asked before a load is picked up.
    auto store_takes = [&](int good, int amount){
        for (size_t id = 1; id < state.accepts.size(); id++){
            if ((state.accepts[id] & GOOD_BIT(good)) && state.room[id] - CarryTotal(state.stock[id]) >= amount){
                return true;
            }
        }
        return false;
    };
    switch (k.state){
        case WORKER_AT_HOME:
            if (!f_look){
                break;
            }
            if (k.job == WORKER_JOB_WOODCUTTER){
                //The pile first: wood waiting at the hut goes to a store as soon as one takes it.
                int load = std::min(ECONOMY_WOOD_PER_TREE,own[GOOD_WOOD]);
                if (load > 0 && store_takes(GOOD_WOOD,load)){
                    k.carry_good = GOOD_WOOD;
                    k.carry = load;
                    own[GOOD_WOOD] -= load;
                    if (FindStore(k,z,walkers)){
                        state.version++;
                        break;
                    }
                    own[GOOD_WOOD] += load;     //none he can get to: it stays on the pile
                    k.carry = 0;
                    k.carry_good = -1;
                }
                //Then the wood, while the pile has room for what a tree gives; full, he waits at home.
                if (own[GOOD_WOOD] + ECONOMY_WOOD_PER_TREE <= ECONOMY_HUT_PILE){
                    FindTree(k,z,walkers);
                }
            }else{
                //A farmer takes the field's food, the most of any one good first; a carrier the water -
                //and only when a store will take it, so nobody stands about holding a load.
                int good = -1;
                for (int gd = 0; gd < GOOD_COUNT; gd++){
                    if (own[gd] > 0 && (good < 0 || own[gd] > own[good])){
                        good = gd;
                    }
                }
                int least = (k.job == WORKER_JOB_WATER) ? ECONOMY_LOAD : 1;
                if (good >= 0 && own[good] >= least && store_takes(good,std::min(ECONOMY_LOAD,own[good]))){
                    k.carry_good = good;
                    k.carry = std::min(ECONOMY_LOAD,own[good]);
                    own[good] -= k.carry;
                    if (FindStore(k,z,walkers)){
                        state.version++;
                    }else{
                        own[good] += k.carry;
                        k.carry = 0;
                        k.carry_good = -1;
                    }
                }
            }
            break;
        case WORKER_TO_WORK:
            Walk(k,dt);
            if (Arrived(k)){
                int ps = (k.prop >= 0) ? state.prop_state[k.prop] : PROP_STATE_STUMP;
                if (ps == PROP_STATE_STUMP){
                    GoHome(k,z,walkers);    //someone was quicker
                }else{
                    k.state = WORKER_WORKING;
                    k.timer = SecondsToTicks(ps == PROP_STATE_STANDING ? ECONOMY_FELL_SECONDS : ECONOMY_PICK_SECONDS);
                }
            }
            break;
        case WORKER_WORKING:
            if (--k.timer > 0){
                break;
            }
            if (k.prop >= 0 && state.prop_state[k.prop] == PROP_STATE_STANDING){
                //Felled: it lies as a log, which he now picks up.
                state.prop_state[k.prop] = PROP_STATE_LOG;
                state.felled_version++;
                k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
            }else if (k.prop >= 0 && state.prop_state[k.prop] == PROP_STATE_LOG){
                state.prop_state[k.prop] = PROP_STATE_STUMP;
                state.felled_version++;
                k.prop = -1;
                k.carry_good = GOOD_WOOD;
                k.carry = ECONOMY_WOOD_PER_TREE;
                //To a store if one takes it - otherwise home, to the pile outside the hut. Never left
                //standing in the wood with it.
                if (!FindStore(k,z,walkers)){
                    GoHome(k,z,walkers);
                }
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
                //At his own workplace: onto its pile, as far as the pile has room.
                int cap = (k.job == WORKER_JOB_WOODCUTTER) ? ECONOMY_HUT_PILE : (1 << 30);
                int put = std::min(k.carry,std::max(0,cap - own[k.carry_good]));
                own[k.carry_good] += put;
                k.carry -= put;
                if (k.carry > 0){
                    k.state = WORKER_HOLDING;   //the pile is full: he waits by it
                }else{
                    k.carry_good = -1;
                    k.state = WORKER_AT_HOME;
                }
                break;
            }
            //The store may have changed while he walked: put down what it still takes and has room for.
            if (s < state.accepts.size() && (state.accepts[s] & GOOD_BIT(k.carry_good))){
                int put = std::min(k.carry,std::max(0,state.room[s] - CarryTotal(state.stock[s])));
                state.stock[s][k.carry_good] += put;
                k.carry -= put;
            }
            if (k.carry > 0){
                if (!FindStore(k,z,walkers)){
                    GoHome(k,z,walkers);    //back to his workplace with what is left
                }
            }else{
                k.carry_good = -1;
                GoHome(k,z,walkers);
            }
            break;
        }
        case WORKER_TO_HOME:
            Walk(k,dt);
            if (Arrived(k)){
                if (k.carry > 0){
                    //Home with a load nobody took: it goes on the workplace's pile.
                    k.state = WORKER_UNLOADING;
                    k.store = 0;
                    k.timer = SecondsToTicks(ECONOMY_PICK_SECONDS);
                }else{
                    k.state = WORKER_AT_HOME;
                }
            }
            break;
        case WORKER_HOLDING:
            //Holding a load: a store that takes it, or room on his pile at home, whichever comes first.
            if (!f_look || FindStore(k,z,walkers)){
                break;
            }
            if (k.job != WORKER_JOB_WOODCUTTER || own[k.carry_good] + k.carry <= ECONOMY_HUT_PILE){
                GoHome(k,z,walkers);
            }
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
    if (z.version != state.zones_version){
        Derive(z,true);
        Replan(z,walkers);
        state.version++;
    }
    CalendarDate date = CalendarDateOf(tick);
    //The fields grow in any weather but winter's and the snow's.
    for (EconomyField& f : state.fields){
        int home = home_plot[f.building];
        if (home < 0 || date.season == SEASON_WINTER || UnderSnow(w,w.grid->fine.pos[home],tick)){
            continue;
        }
        int crop = z.buildings[f.building].crop;
        if (f.crop != crop){
            //Another crop sown: it grows from nothing, whatever the last had reached.
            f.crop = crop;
            f.grown_ticks = 0;
        }
        if (++f.grown_ticks >= crop_days[crop] * CALENDAR_DAY_TICKS){
            float area = ZoneBuildingFigures(w,z,f.building).floor_area;
            state.stock[f.building][GoodOfCrop(crop)] += (int)std::floor(area * crop_yield[crop]);
            f.grown_ticks = 0;
            state.version++;
        }
    }
    //A collector fills, a unit at a time, up to what it holds - not frozen under the snow.
    int water_ticks = SecondsToTicks(ECONOMY_WATER_SECONDS);
    if (tick % (uint64_t)water_ticks == 0){
        for (size_t id = 1; id < z.buildings.size(); id++){
            if (z.buildings[id].kind != ZONE_KIND_WATER || z.buildings[id].size <= 0 || home_plot[id] < 0){
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
    state.workers = saved.workers;
    state.fields = saved.fields;
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
        if (z.buildings[id].kind == ZONE_KIND_STORE){
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
            return std::min(1.0f,(float)f.grown_ticks / (crop_days[crop] * CALENDAR_DAY_TICKS));
        }
    }
    return 0.0f;
}

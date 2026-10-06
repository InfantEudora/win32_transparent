#include "Walkers.h"
#include "ChasmWorld.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace {

std::shared_ptr<const WalkGraph> BuildWalkGraph(std::shared_ptr<const ChasmWorld> w){
    std::shared_ptr<WalkGraph> gr = std::make_shared<WalkGraph>();
    gr->world = w;
    const GridLevel& f = w->grid->fine;
    size_t n = f.pos.size();
    gr->start.assign(n + 1,0);
    for (const auto& e : f.edges){
        gr->start[e.first + 1]++;
        gr->start[e.second + 1]++;
    }
    for (size_t i = 0; i < n; i++){
        gr->start[i + 1] += gr->start[i];
    }
    gr->next.assign(gr->start[n],-1);
    gr->length.assign(gr->start[n],0.0f);
    //The edges are sorted (Grid.h), so every vertex's neighbours come out in a fixed order - which
    //is what makes A*'s ties fall the same way every run.
    std::vector<int> fill(gr->start.begin(),gr->start.end() - 1);
    for (const auto& e : f.edges){
        float len = (f.pos[e.first] - f.pos[e.second]).length();
        gr->next[fill[e.first]] = e.second;
        gr->length[fill[e.first]++] = len;
        gr->next[fill[e.second]] = e.first;
        gr->length[fill[e.second]++] = len;
    }
    return gr;
}

}

void Walkers::Reset(std::shared_ptr<const ChasmWorld> world){
    uint32_t version = set.version;
    set = WalkerSet();
    set.world = world;
    set.version = version + 1;
    graph.reset();
    last_refusal.clear();
}

void Walkers::EnsureGraph(){
    if (set.world && (!graph || graph->world != set.world)){
        graph = BuildWalkGraph(set.world);
        links_version = 0xFFFFFFFFu;
    }
}

void Walkers::EnsureLinks(const ZoneState& z){
    if (links_version != z.version && set.world && z.world == set.world){
        ZoneWinchLinks(*set.world,z,links);
        links_version = z.version;
    }
}

bool Walkers::IsLink(int a, int b) const{
    for (const auto& l : links){
        if ((l.first == a && l.second == b) || (l.first == b && l.second == a)){
            return true;
        }
    }
    return false;
}

float Walkers::EdgeLength(int a, int b) const{
    const GridLevel& f = set.world->grid->fine;
    return (f.pos[a] - f.pos[b]).length();
}

//Whether a walk from `from` to `to` may use the edge a -> b.
bool Walkers::EdgeOpen(const ZoneState& z, int a, int b, int from, int to) const{
    const Terrain& t = *set.world->terrain;
    if (t.level[a] != t.level[b]){
        //A cliff - unless a winch rides it, with its landing clear (or the walk ending on it).
        if (!IsLink(a,b)){
            return false;
        }
        int landing = (t.level[a] == TERRAIN_BALCONY) ? a : b;
        return z.storeys[landing] == 0 || landing == to || landing == from;
    }
    if (t.wet[a] || t.wet[b]){
        return false;   //a river - until there are bridges
    }
    if (t.Mountain(a) || t.Mountain(b)){
        return false;   //the north mountain, which nothing crosses (biomes_plan.md)
    }
    //Too steep to walk: past a rise of WALKER_STEEPEST over the edge's run (step 2's relief).
    if (std::fabs(t.ground[b] - t.ground[a]) > WALKER_STEEPEST * EdgeLength(a,b)){
        return false;
    }
    //A house is walked into only where the walk ends, and out of only where it starts. A winch is
    //walked through: its plot is the way on to the rope.
    if (z.storeys[b] > 0 && b != to && z.KindOf(b) != ZONE_KIND_WINCH){
        return false;
    }
    if (z.storeys[a] > 0 && a != from && z.KindOf(a) != ZONE_KIND_WINCH){
        return false;
    }
    //A wall or palisade is crossed at its gate and nowhere else.
    return ZoneBoundaryBetween(z,a,b) == ZONE_BOUNDARY_NONE || ZoneGateBetween(*set.world,z,a,b);
}

//A field is the coarse cells round a vertex: walking through one is slow, along its edge is not.
static bool InField(const ChasmWorld& w, const ZoneState& z, int v){
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        if (!z.field[w.grid->fine.quads[p.PlotQuadCorner(v,i) / 4].parent]){
            return false;
        }
    }
    return true;
}

float Walkers::EdgeSpeed(const ZoneState& z, int a, int b) const{
    //A winch's ride takes its time however far the landing is across.
    if (set.world->terrain->level[a] != set.world->terrain->level[b]){
        return std::max(1e-3f,EdgeLength(a,b)) / WINCH_RIDE_SECONDS;
    }
    float speed = WALKER_SPEED_GROUND;
    if (z.ground[a] == ZONE_GROUND_ROAD && z.ground[b] == ZONE_GROUND_ROAD){
        speed = WALKER_SPEED_ROAD;
    }else if (InField(*set.world,z,a) && InField(*set.world,z,b)){
        speed = WALKER_SPEED_FIELD;
    }else if (set.world->terrain->biome[a] == TERRAIN_BIOME_SWAMP && set.world->terrain->biome[b] == TERRAIN_BIOME_SWAMP){
        speed = WALKER_SPEED_SWAMP;     //off a road the swamp is mud
    }
    //Slower up and down a slope, by how steep it is - never faster, so A*'s estimate (the straight
    //line at road speed) still never overestimates.
    const Terrain& t = *set.world->terrain;
    float grade = std::fabs(t.ground[b] - t.ground[a]) / std::max(1e-3f,EdgeLength(a,b));
    return speed / (1.0f + WALKER_SLOPE_COST * grade);
}

/*
    A* over the plots. The estimate is the straight line at road speed, the fastest anything is
    walked, so it never overestimates and the path found is the cheapest. The open set is ordered
    by estimate and then by plot index, and neighbours come in the graph's fixed order, so the same
    question always gets the same path.
*/
std::vector<int> Walkers::FindPath(const ZoneState& z, int from, int to) const{
    std::vector<int> path;
    if (!graph || from < 0 || to < 0){
        return path;
    }
    if (from == to){
        path.push_back(from);
        return path;
    }
    const GridLevel& f = set.world->grid->fine;
    size_t n = f.pos.size();
    std::vector<float> cost(n,INFINITY);
    std::vector<int> came(n,-1);
    std::vector<uint8_t> closed(n,0);
    typedef std::pair<float,int> Entry;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    const vec2 target = f.pos[to];
    auto estimate = [&](int v){
        return (f.pos[v] - target).length() / WALKER_SPEED_ROAD;
    };
    cost[from] = 0.0f;
    open.push(Entry(estimate(from),from));
    while (!open.empty()){
        int v = open.top().second;
        open.pop();
        if (closed[v]){
            continue;
        }
        closed[v] = 1;
        if (v == to){
            break;
        }
        for (int i = graph->start[v]; i < graph->start[v + 1]; i++){
            int u = graph->next[i];
            if (closed[u] || !EdgeOpen(z,v,u,from,to)){
                continue;
            }
            float c = cost[v] + graph->length[i] / EdgeSpeed(z,v,u);
            if (c < cost[u]){
                cost[u] = c;
                came[u] = v;
                open.push(Entry(c + estimate(u),u));
            }
        }
        //And down or up a winch, in the links' fixed order.
        for (const auto& l : links){
            int u = (l.first == v) ? l.second : ((l.second == v) ? l.first : -1);
            if (u < 0 || closed[u] || !EdgeOpen(z,v,u,from,to)){
                continue;
            }
            float c = cost[v] + WINCH_RIDE_SECONDS;
            if (c < cost[u]){
                cost[u] = c;
                came[u] = v;
                open.push(Entry(c + estimate(u),u));
            }
        }
    }
    if (!closed[to]){
        return path;
    }
    for (int v = to; v != -1; v = came[v]){
        path.push_back(v);
    }
    std::reverse(path.begin(),path.end());
    return path;
}

bool Walkers::Spawn(const ZoneState& z, int home, int goal){
    last_refusal.clear();
    if (!set.world){
        last_refusal = "no world";
        return false;
    }
    int n = (int)set.world->grid->fine.pos.size();
    if (home < 0 || home >= n || goal < 0 || goal >= n){
        last_refusal = "no such plot";
        return false;
    }
    if ((int)set.walkers.size() >= WALKER_MAX){
        last_refusal = "as many walkers as there may be";
        return false;
    }
    EnsureGraph();
    EnsureLinks(z);
    Walker k;
    k.home = home;
    k.goal = goal;
    k.path.push_back(home);
    Replan(z,k);
    set.walkers.push_back(k);
    set.zones_version = z.version;
    set.version++;
    return true;
}

void Walkers::Clear(){
    set.walkers.clear();
    set.version++;
}

/*
    Plans the rest of k's walk to its target. On an edge, it keeps the edge and plans from where the
    edge leads - unless that plot is now closed to it, when it turns back down the edge and plans from
    where it came from.
*/
void Walkers::Replan(const ZoneState& z, Walker& k){
    int target = k.f_outward ? k.goal : k.home;
    bool f_on_edge = k.seg + 1 < (int)k.path.size();
    std::vector<int> head;  //what the new path starts with: the edge kept, or the plot stood on
    float along = 0.0f;
    if (f_on_edge){
        int a = k.path[k.seg];
        int b = k.path[k.seg + 1];
        if (EdgeOpen(z,a,b,a,target)){
            head = {a,b};
            along = k.along;
        }else{
            head = {b,a};
            along = std::max(0.0f,EdgeLength(a,b) - k.along);
        }
    }else{
        head = {k.path[k.seg]};
    }
    std::vector<int> rest = FindPath(z,head.back(),target);
    k.f_stuck = rest.empty();
    k.path = head;
    for (size_t i = 1; i < rest.size(); i++){
        k.path.push_back(rest[i]);
    }
    k.seg = 0;
    k.along = along;
}

void Walkers::Tick(const ZoneState& z, float dt){
    if (!set.world || z.world != set.world || set.walkers.empty()){
        return;
    }
    EnsureGraph();
    EnsureLinks(z);
    bool f_changed = false;
    if (set.zones_version != z.version){
        set.zones_version = z.version;
        for (Walker& k : set.walkers){
            Replan(z,k);
        }
        f_changed = true;
    }
    for (Walker& k : set.walkers){
        //At the end of the path: a target reached turns it round; a stuck walker waits for a change.
        if (k.seg + 1 >= (int)k.path.size()){
            if (k.f_stuck){
                continue;
            }
            k.f_outward = !k.f_outward;
            k.legs++;
            k.path = {k.path.back()};
            k.seg = 0;
            k.along = 0.0f;
            Replan(z,k);
            f_changed = true;
            continue;
        }
        //One tick's distance at each edge's own speed, the leftover carried into the next edge.
        float time = dt;
        while (time > 0.0f && k.seg + 1 < (int)k.path.size()){
            int a = k.path[k.seg];
            int b = k.path[k.seg + 1];
            float len = EdgeLength(a,b);
            float speed = EdgeSpeed(z,a,b);
            float left = (len - k.along) / speed;
            if (left > time){
                k.along += time * speed;
                time = 0.0f;
            }else{
                time -= left;
                k.seg++;
                k.along = 0.0f;
                f_changed = true;
            }
        }
    }
    if (f_changed){
        set.version++;
    }
}

void Walkers::Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<Walker>& walkers,
                      uint32_t zones_version){
    Reset(world);
    if (!world){
        return;
    }
    int n = (int)world->grid->fine.pos.size();
    for (const Walker& k : walkers){
        bool f_ok = k.home >= 0 && k.home < n && k.goal >= 0 && k.goal < n && !k.path.empty() &&
                    k.seg >= 0 && k.seg < (int)k.path.size();
        for (int v : k.path){
            f_ok = f_ok && v >= 0 && v < n;
        }
        if (f_ok && (int)set.walkers.size() < WALKER_MAX){
            set.walkers.push_back(k);
        }
    }
    set.zones_version = zones_version;
    EnsureGraph();
}

/*
    Where a walker is drawn. Not straight down the edges: through each plot it passes, it takes the same
    curve a road does (RoadMesh.h) - from the last edge's midpoint to the next one's, the plot pulling
    it - so it rounds a corner instead of turning on the spot, and stays on a road where the road bends.
    View only: the simulation's state is the edge and the distance down it, and nothing reads this.
*/
vec2 Walkers::Position(const ChasmWorld& w, const Walker& k, vec2* facing){
    const GridLevel& f = w.grid->fine;
    if (k.path.empty()){
        return vec2();
    }
    if (k.seg + 1 >= (int)k.path.size()){
        return f.pos[k.path[k.seg]];
    }
    vec2 a = f.pos[k.path[k.seg]];
    vec2 b = f.pos[k.path[k.seg + 1]];
    float len = (b - a).length();
    if (len < 1e-6f){
        return a;
    }
    /*
        On a winch's rope: out from the rim to above the landing first, then straight down - or up
        first and then in. The rope hangs over the landing, so on the way down the walker is over it
        after the first WINCH_STEP_OUT of the ride, and on the way up until the last.
    */
    const Terrain& terrain = *w.terrain;
    if (terrain.level[k.path[k.seg]] != terrain.level[k.path[k.seg + 1]]){
        float s = std::max(0.0f,std::min(1.0f,k.along / len));
        bool f_down = terrain.level[k.path[k.seg]] == TERRAIN_PLATEAU;
        vec2 rim = f_down ? a : b;
        vec2 rope = f_down ? b : a;
        float out = f_down ? std::min(1.0f,s / WINCH_STEP_OUT) : std::max(0.0f,(s - (1.0f - WINCH_STEP_OUT)) / WINCH_STEP_OUT);
        if (facing){
            *facing = (rope - rim) / std::max(1e-6f,(rope - rim).length()) * (f_down ? 1.0f : -1.0f);
        }
        return f_down ? rim + (rope - rim) * out : rope + (rim - rope) * out;
    }
    //The corner to round: the plot behind in the first half of the edge, the one ahead in the second.
    vec2 p0, c, p1;
    float t;
    float half = len * 0.5f;
    if (k.along < half && k.seg > 0){
        vec2 prev = f.pos[k.path[k.seg - 1]];
        float back = (a - prev).length() * 0.5f;
        p0 = (prev + a) * 0.5f;
        c = a;
        p1 = (a + b) * 0.5f;
        t = (k.along + back) / (back + half);
    }else if (k.along >= half && k.seg + 2 < (int)k.path.size()){
        vec2 next = f.pos[k.path[k.seg + 2]];
        float ahead = (next - b).length() * 0.5f;
        p0 = (a + b) * 0.5f;
        c = b;
        p1 = (b + next) * 0.5f;
        t = (k.along - half) / (half + ahead);
    }else{
        if (facing){
            *facing = (b - a) / len;
        }
        return a + (b - a) * std::min(1.0f,k.along / len);
    }
    t = std::max(0.0f,std::min(1.0f,t));
    float s = 1.0f - t;
    if (facing){
        vec2 d = (c - p0) * s + (p1 - c) * t;
        float dl = d.length();
        if (dl > 1e-6f){
            *facing = d / dl;
        }
    }
    return p0 * (s * s) + c * (2.0f * s * t) + p1 * (t * t);
}

float Walkers::Height(const ChasmWorld& w, const Walker& k, const vec2& at){
    const Terrain& t = *w.terrain;
    if (k.path.empty()){
        return 0.0f;
    }
    int a = k.path[k.seg];
    if (k.seg + 1 < (int)k.path.size()){
        int b = k.path[k.seg + 1];
        if (t.level[a] != t.level[b]){
            //The rope: at the rim's height while stepping out, then down to the landing's (or the
            //other way round).
            float len = std::max(1e-6f,(w.grid->fine.pos[b] - w.grid->fine.pos[a]).length());
            float s = std::max(0.0f,std::min(1.0f,k.along / len));
            bool f_down = t.level[a] == TERRAIN_PLATEAU;
            int rim = f_down ? a : b;
            int landing = f_down ? b : a;
            float top = t.ground[rim];
            float bottom = t.ground[landing];
            float drop = f_down ? std::max(0.0f,(s - WINCH_STEP_OUT) / (1.0f - WINCH_STEP_OUT))
                                : 1.0f - std::min(1.0f,s / (1.0f - WINCH_STEP_OUT));
            return top + (bottom - top) * drop;
        }
    }
    return t.GroundHeight(at,t.Height(a));
}

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

void Walkers::Reset(std::shared_ptr<const ChasmWorld> w){
    world = w;
    graph.reset();
    links.clear();
    links_version = 0xFFFFFFFFu;
}

void Walkers::EnsureGraph(){
    if (world && (!graph || graph->world != world)){
        graph = BuildWalkGraph(world);
        links_version = 0xFFFFFFFFu;
    }
}

void Walkers::EnsureLinks(const ZoneState& z){
    if (links_version != z.version && world && z.world == world){
        ZoneWinchLinks(*world,z,links);
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
    const GridLevel& f = world->grid->fine;
    return (f.pos[a] - f.pos[b]).length();
}

//Whether a walk from `from` to `to` may use the edge a -> b.
bool Walkers::EdgeOpen(const ZoneState& z, int a, int b, int from, int to) const{
    const Terrain& t = *world->terrain;
    if (!t.SameGround(a,b)){
        //A cliff - unless a winch rides it, with its landing clear (or the walk ending on it).
        if (!IsLink(a,b)){
            return false;
        }
        int landing = (t.steps[a] > t.steps[b]) ? a : b;
        return z.storeys[landing] == 0 || landing == to || landing == from;
    }
    //A river and its wet bank are crossed only on a bridge that stands (bridge_plan.md). A water
    //collector stands on the dry edge of the bank, so its carrier needs no way onto the wet.
    if ((t.wet[a] && !ZoneBridgeWalkable(z,a)) || (t.wet[b] && !ZoneBridgeWalkable(z,b))){
        return false;
    }
    if (t.Mountain(a) || t.Mountain(b)){
        return false;   //the north mountain, which nothing crosses (biomes_plan.md)
    }
    //Too steep to walk: past a rise of WALKER_STEEPEST over the edge's run (step 2's relief).
    if (std::fabs(t.ground[b] - t.ground[a]) > WALKER_STEEPEST * EdgeLength(a,b)){
        return false;
    }
    //A house is walked into only where the walk ends, and out of only where it starts. A winch is
    //walked through - its plot is the way on to the rope - and so is a camp, open ground between tents,
    //and a bridge that stands.
    auto passage = [&](int v){
        int kind = z.KindOf(v);
        return kind == ZONE_KIND_WINCH || kind == ZONE_KIND_CAMP || ZoneBridgeWalkable(z,v);
    };
    if (z.storeys[b] > 0 && b != to && !passage(b)){
        return false;
    }
    if (z.storeys[a] > 0 && a != from && !passage(a)){
        return false;
    }
    //A standing palisade is never walked onto, nor through; off one only by someone the builders raised
    //it round, where his walk starts (Zones.h, WALLS). A gate is road, and open.
    if (ZoneWallStands(z,b) || (ZoneWallStands(z,a) && a != from)){
        return false;
    }
    //A garden's wall or a lot's fence is crossed at its gate and nowhere else.
    return ZoneBoundaryBetween(z,a,b) == ZONE_BOUNDARY_NONE || ZoneGateBetween(*world,z,a,b);
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
    if (!world->terrain->SameGround(a,b)){
        return std::max(1e-3f,EdgeLength(a,b)) / WINCH_RIDE_SECONDS;
    }
    float speed = WALKER_SPEED_GROUND;
    //A road, or a bridge's deck - and from one onto the other, so a road over a bridge is all road.
    if (ZoneRoadStands(z,a) && ZoneRoadStands(z,b)){
        speed = WALKER_SPEED_ROAD;
    }else if (InField(*world,z,a) && InField(*world,z,b)){
        speed = WALKER_SPEED_FIELD;
    }else if (world->terrain->biome[a] == TERRAIN_BIOME_SWAMP && world->terrain->biome[b] == TERRAIN_BIOME_SWAMP){
        speed = WALKER_SPEED_SWAMP;     //off a road the swamp is mud
    }
    //Slower up and down a slope, by how steep it is - never faster, so A*'s estimate (the straight
    //line at road speed) still never overestimates.
    const Terrain& t = *world->terrain;
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
    const GridLevel& f = world->grid->fine;
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

//The economy's people (Economy.h) walk these: the same graph, rules and A* the debug walker had.
bool Walkers::PlanRoute(const ZoneState& z, int from, int to, std::vector<int>& plots, std::vector<float>& speeds){
    plots.clear();
    speeds.clear();
    if (!world || world != z.world){
        return false;
    }
    EnsureGraph();
    EnsureLinks(z);
    plots = FindPath(z,from,to);
    for (size_t i = 0; i + 1 < plots.size(); i++){
        speeds.push_back(EdgeSpeed(z,plots[i],plots[i + 1]));
    }
    return !plots.empty();
}

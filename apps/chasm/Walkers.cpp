#include "Walkers.h"
#include "ChasmWorld.h"
#include "Calendar.h"

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

/*
    A stair's way up: toward the neighbours' decks its top meets, on its ground - (0,0) with none, when it takes any
    side. The stair's own judgement, by layers alone, so asking it never asks WalkJoin back.
*/
vec2 StairUp(const ChasmWorld& w, const ZoneState& z, int v, int layer){
    const Terrain& t = *w.terrain;
    vec2 up(0.0f,0.0f);
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    for (int i = 0; i < n; i++){
        int ls[WALK_MOST_DECKS];
        int c = t.SameGround(v,nb[i]) ? WalkDeckLayers(z,nb[i],ls) : 0;
        for (int j = 0; j < c; j++){
            if (WalkLayersMeet(layer,ls[j]) == layer + 1){
                vec2 d = w.grid->fine.pos[nb[i]] - w.grid->fine.pos[v];
                float l = d.length();
                up = up + ((l > 1e-5f) ? d * (1.0f / l) : vec2(0.0f,0.0f));
            }
        }
    }
    float l = up.length();
    return (l > 1e-5f) ? up * (1.0f / l) : vec2(0.0f,0.0f);
}

#define STAIR_FOOT_ACROSS   -0.3f   //a stair's foot: a side at least this much across from its way up (cosine)

/*
    Whether stair (v, layer) takes the side toward node (other, other_layer), which it meets at `meet`. Its top, a
    stair beside it, and a piece below it are taken wherever they are - a flight may turn - but the GROUND only across
    from its way up: taking every side the ground is on made a first stair a mound, sloping down three sides.
*/
bool StairTakes(const ChasmWorld& w, const ZoneState& z, int v, int layer, int other, int other_layer, int meet){
    if (meet != layer - 1 || other_layer != WALK_GROUND){
        return true;
    }
    vec2 up = StairUp(w,z,v,layer);
    if (up.dot(up) < 1e-6f){
        return true;
    }
    vec2 d = w.grid->fine.pos[other] - w.grid->fine.pos[v];
    float l = d.length();
    return l > 1e-5f && d.dot(up) / l < STAIR_FOOT_ACROSS;
}

}

int WalkDeckLayers(const ZoneState& z, int v, int* out){
    int n = 0;
    if (ZoneCastleLayer(z,v) > 0){
        if (!ZoneCastleOverSits(z,v)){
            out[n++] = ZoneCastleLayer(z,v);    //a stair on an arch covers its deck
        }
        if (ZoneCastleOverLayer(z,v) > 0){
            out[n++] = ZoneCastleOverLayer(z,v);
        }
    }else if (z.KindOf(v) == ZONE_KIND_TOWER && z.standing[v] > 0 && z.standing[v] >= z.storeys[v]
              && ZoneTowerLadder(z,v) >= 0){
        out[n++] = 2 * z.standing[v];
    }
    return n;
}

int WalkJoin(const ChasmWorld& w, const ZoneState& z, int a, int la, int b, int lb){
    if (!w.terrain->SameGround(a,b)){
        return -1;
    }
    //Under a block or a stair there is no ground to step onto.
    if ((la == WALK_GROUND && ZoneCastleShuts(z,a)) || (lb == WALK_GROUND && ZoneCastleShuts(z,b))){
        return -1;
    }
    int meet = WalkLayersMeet(la,lb);
    if (meet < 0 || (WalkLayerStair(la) && !StairTakes(w,z,a,la,b,lb,meet))
        || (WalkLayerStair(lb) && !StairTakes(w,z,b,lb,a,la,meet))){
        return -1;
    }
    return meet;
}

int WalkJoinAny(const ChasmWorld& w, const ZoneState& z, int a, int la, int b){
    int ls[WALK_MOST_DECKS];
    for (int j = WalkDeckLayers(z,b,ls) - 1; j >= 0; j--){
        int meet = WalkJoin(w,z,a,la,b,ls[j]);
        if (meet >= 0){
            return meet;
        }
    }
    return WalkJoin(w,z,a,la,b,WALK_GROUND);
}

void Walkers::Reset(std::shared_ptr<const ChasmWorld> w){
    world = w;
    graph.reset();
    links.clear();
    links_version = 0xFFFFFFFFu;
    decks.clear();
    joins.clear();
    decks_version = 0xFFFFFFFFu;
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

/*
    THE DECKS (LAYERS, Walkers.h), in plot order, and their joins. A guard tower that stands whole is its
    platform, anyone's, reached by its ladder - a tower with no side for one (loaded with none open) keeps
    no deck and is walked into as a house is, as it always was. A piece of the debug castle (Zones.h) is
    its top - a stair's middle - and an arch over it a second deck; the posted's only.
*/
void Walkers::EnsureDecks(const ZoneState& z){
    if (decks_version == z.version || !world || z.world != world){
        return;
    }
    decks_version = z.version;
    decks.clear();
    joins.clear();
    int n = (int)world->grid->fine.pos.size();
    for (int v = 0; v < n && v < (int)z.storeys.size(); v++){
        int ls[WALK_MOST_DECKS];
        int c = WalkDeckLayers(z,v,ls);
        for (int i = 0; i < c; i++){
            WalkDeck d;
            d.plot = v;
            d.layer = (uint8_t)ls[i];
            d.f_posted = ZoneCastleShape(z,v) != ZONE_CASTLE_NONE;
            decks.push_back(d);
        }
    }
    for (int i = 0; i < (int)decks.size(); i++){
        const WalkDeck& d = decks[i];
        int node = n + i;
        //Up its ladder from the plot it stands toward, at the cost that edge always had.
        int ladder = ZoneTowerLadder(z,d.plot);
        if (ladder >= 0){
            float len = EdgeLength(ladder,d.plot);
            float speed = EdgeSpeed(z,ladder,d.plot);
            joins.push_back(Join{ladder,node,len,speed});
            joins.push_back(Join{node,ladder,len,speed});
        }
        //To each of a neighbour's decks the two join at (WalkJoin), and down a first stair to the ground.
        int nb[8];
        int c = ZonePlotNeighbours(*world,d.plot,nb,8);
        for (int j = 0; j < c; j++){
            int u = nb[j];
            float len = EdgeLength(d.plot,u);
            int ls[WALK_MOST_DECKS];
            int lc = WalkDeckLayers(z,u,ls);
            for (int k = 0; k < lc; k++){
                int e = DeckNode(u,ls[k]);
                if (e >= 0 && WalkJoin(*world,z,d.plot,d.layer,u,ls[k]) >= 0){
                    bool f_stair = WalkLayerStair(d.layer) || WalkLayerStair(ls[k]);
                    joins.push_back(Join{node,e,len,f_stair ? WALKER_SPEED_STAIR : WALKER_SPEED_DECK});
                }
            }
            if (WalkJoin(*world,z,d.plot,d.layer,u,WALK_GROUND) >= 0){
                joins.push_back(Join{node,u,len,WALKER_SPEED_STAIR});
                joins.push_back(Join{u,node,len,WALKER_SPEED_STAIR});
            }
        }
    }
    //By the node left, then the one reached: a fixed order, so A*'s ties fall the same way every run.
    std::sort(joins.begin(),joins.end(),[](const Join& a, const Join& b){
        return (a.from != b.from) ? a.from < b.from : a.to < b.to;
    });
}

const std::vector<WalkDeck>& Walkers::Decks(const ZoneState& z){
    EnsureGraph();
    EnsureDecks(z);
    return decks;
}

//The index of the first deck on `plot` - or of the first past it, when it has none.
int Walkers::FirstDeck(int plot) const{
    auto it = std::lower_bound(decks.begin(),decks.end(),plot,[](const WalkDeck& d, int p){ return d.plot < p; });
    return (int)(it - decks.begin());
}

//The node of the deck on `plot` at `layer`, or -1.
int Walkers::DeckNode(int plot, int layer) const{
    int n = (int)world->grid->fine.pos.size();
    for (int i = FirstDeck(plot); i < (int)decks.size() && decks[i].plot == plot; i++){
        if (decks[i].layer == layer){
            return n + i;
        }
    }
    return -1;
}

/*
    Whether a walk by `who` may take a join: onto a deck he may walk, or down onto ground he may enter. Off a
    deck is always open - he got onto it. `from` and `to` are the walk's ends as plots of the ground, -1 for a deck.
*/
bool Walkers::JoinOpen(const ZoneState& z, const Join& j, int who, int from, int to) const{
    int n = (int)world->grid->fine.pos.size();
    if (j.to >= n){
        return who != WALK_WHO_ZOMBIE && (!decks[j.to - n].f_posted || who == WALK_WHO_POSTED);
    }
    int deck_plot = (j.from >= n) ? decks[j.from - n].plot : j.from;
    if (ZoneBoundaryBetween(z,deck_plot,j.to) != ZONE_BOUNDARY_NONE && !ZoneGateBetween(*world,z,deck_plot,j.to)){
        return false;   //a garden's wall or a lot's fence, as on the ground
    }
    return GroundEnterable(z,j.to,from,to,who);
}

//Whether the ground of plot v may be stepped onto off a deck: what EdgeOpen asks of an edge's far end.
bool Walkers::GroundEnterable(const ZoneState& z, int v, int from, int to, int who) const{
    const Terrain& t = *world->terrain;
    if ((t.wet[v] && !ZoneBridgeWalkable(z,v) && !IceOpen(who,v)) || t.Mountain(v) || ZoneShutToWalkers(z,v)){
        return false;
    }
    int kind = z.KindOf(v);
    bool f_passage = kind == ZONE_KIND_WINCH || kind == ZONE_KIND_CAMP || ZoneBridgeWalkable(z,v);
    if (z.storeys[v] > 0 && v != to && v != from && !f_passage){
        return false;
    }
    return ZoneTowerLadder(z,v) < 0;    //a tower's ground only from its ladder, along the grid's edge
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
bool Walkers::EdgeOpen(const ZoneState& z, int a, int b, int from, int to, int who, const WalkBreaks* breaks,
                       float* extra) const{
    const Terrain& t = *world->terrain;
    if (extra){
        *extra = 0.0f;
    }
    if (!t.SameGround(a,b)){
        //A cliff - unless a winch rides it, with its landing clear (or the walk ending on it).
        if (!IsLink(a,b)){
            return false;
        }
        int landing = (t.steps[a] > t.steps[b]) ? a : b;
        return z.storeys[landing] == 0 || landing == to || landing == from;
    }
    //A river and its wet bank are crossed only on a bridge that stands (bridge_plan.md). A water
    //collector stands on the dry edge of the bank, so its carrier needs no way onto the wet. In winter the
    //posted cross the ice too (ICE) - and anyone stood on the wet when it thawed may step off it.
    auto water = [&](int v){
        return t.wet[v] && !ZoneBridgeWalkable(z,v) && !IceOpen(who,v) && v != from;
    };
    if (water(a) || water(b)){
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
    //and a bridge that stands. A bridge's SITE too, under the snow, by those who walk the ice (ICE): its piles
    //stand in the frozen river and its banks are only staked - and it lies where the crossing is, so a
    //woodcutter after its far bank's trees went the long way round the river without it (2026-10-16).
    auto passage = [&](int v){
        int kind = z.KindOf(v);
        return kind == ZONE_KIND_WINCH || kind == ZONE_KIND_CAMP || ZoneBridgeWalkable(z,v)
            || (kind == ZONE_KIND_BRIDGE && (who == WALK_WHO_POSTED || who == WALK_WHO_ICE) && Frozen(v));
    };
    //A ZOMBIE'S WAY (`breaks`): a building with a door is a door to breach, not a wall - entered at the cost of
    //breaching it, nothing once it is, and walked through after. A tower stays shut to it: it has a ladder, not a door.
    auto house = [&](int v){
        return breaks && ZoneKindHasDoor(z.KindOf(v)) && z.standing[v] > 0;
    };
    if (z.storeys[b] > 0 && b != to && !passage(b) && !house(b)){
        return false;
    }
    if (z.storeys[a] > 0 && a != from && !passage(a) && !house(a)){
        return false;
    }
    float cost = 0.0f;
    if (house(b) && z.building[b] != z.building[a]){
        cost += breaks->DoorLeft(z.building[b]);
    }
    //A guard tower is got onto and off only by its LADDER, its door (defence_plan.md): from the plot the ladder
    //stands toward, and back to it - its site's builders too. One left with none (loaded with no side open) is
    //entered as a house is.
    int ladder_b = ZoneTowerLadder(z,b);
    int ladder_a = ZoneTowerLadder(z,a);
    if ((ladder_b >= 0 && a != ladder_b) || (ladder_a >= 0 && b != ladder_a)){
        return false;
    }
    //A standing palisade is never walked onto, nor through; off one only by someone the builders raised
    //it round, where his walk starts (Zones.h, WALLS). A gate is road, and open - unless its door is shut
    //(defence_plan.md), when it is the palisade's: off it only for someone it was shut on.
    //A zombie's way through either is breaking it; a castle's stone it never breaks.
    bool f_shut_b = ZoneShutToWalkers(z,b);
    if (f_shut_b || (ZoneShutToWalkers(z,a) && a != from)){
        if (!breaks || ZoneCastleShuts(z,a) || ZoneCastleShuts(z,b)){
            return false;
        }
        if (f_shut_b){
            cost += ZoneWallStands(z,b) ? breaks->WallLeft(b) : breaks->GateLeft(b);
        }
    }
    auto open = [&](float seconds){
        if (extra){
            *extra = seconds;
        }
        return true;
    };
    //A garden's wall or a lot's fence is crossed at its gate and nowhere else - but climbed by a zombie, slowly.
    if (ZoneBoundaryBetween(z,a,b) == ZONE_BOUNDARY_NONE || ZoneGateBetween(*world,z,a,b)){
        return open(cost);
    }
    if (breaks){
        return open(cost + breaks->ClimbSeconds());
    }
    /*
        SHUT IN A YARD: out over it, never in, for someone whose walk starts inside one. Nobody is meant to be
        there (a woodpile is reached from its hut), but a site is open ground: a fence went up round a man
        crossing it, and with no gate (no road ends at it) and the hut no way through, he stood in the lot
        until he died of thirst (the user's recording, 2026-10-09). A road run on past a gate shuts one the same.
        Entering is still closed, so he can only have walked to this edge from inside the yard he started in.
    */
    return from >= 0 && ZoneGroundStands(z,from) && z.storeys[from] == 0 && ZoneGroundStands(z,a);
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
    A* over the nodes - the plots' ground, then the decks (LAYERS, Walkers.h). The estimate is the straight
    line at road speed, the fastest anything is walked, so it never overestimates and the path found is the
    cheapest. The open set is ordered by estimate and then by node index, and neighbours come in the graph's
    fixed order, so the same question always gets the same path.
*/
std::vector<int> Walkers::FindPath(const ZoneState& z, int from, int to, int who, const WalkBreaks* breaks) const{
    std::vector<int> path;
    if (!graph || from < 0 || to < 0){
        return path;
    }
    if (from == to){
        path.push_back(from);
        return path;
    }
    const GridLevel& f = world->grid->fine;
    int n = (int)f.pos.size();
    size_t count = (size_t)n + decks.size();
    std::vector<float> cost(count,INFINITY);
    std::vector<int> came(count,-1);
    std::vector<uint8_t> closed(count,0);
    typedef std::pair<float,int> Entry;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    auto plot_of = [&](int v){
        return (v < n) ? v : decks[v - n].plot;
    };
    const vec2 target = f.pos[plot_of(to)];
    auto estimate = [&](int v){
        return (f.pos[plot_of(v)] - target).length() / WALKER_SPEED_ROAD;
    };
    //The ground's rules know a walk's ends as plots: an end on a deck is none of the ground's.
    int from_ground = (from < n) ? from : -1;
    int to_ground = (to < n) ? to : -1;
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
        if (v < n){
            for (int i = graph->start[v]; i < graph->start[v + 1]; i++){
                int u = graph->next[i];
                float extra = 0.0f;
                if (closed[u] || !EdgeOpen(z,v,u,from_ground,to_ground,who,breaks,&extra)){
                    continue;
                }
                float c = cost[v] + graph->length[i] / EdgeSpeed(z,v,u) + extra;     //a zombie's breaking too
                if (c < cost[u]){
                    cost[u] = c;
                    came[u] = v;
                    open.push(Entry(c + estimate(u),u));
                }
            }
            //And down or up a winch, in the links' fixed order.
            for (const auto& l : links){
                int u = (l.first == v) ? l.second : ((l.second == v) ? l.first : -1);
                if (u < 0 || closed[u] || !EdgeOpen(z,v,u,from_ground,to_ground,who)){
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
        //Off the grid's edges: up onto a deck, along the decks, down off them.
        auto first = std::lower_bound(joins.begin(),joins.end(),v,[](const Join& j, int node){ return j.from < node; });
        for (auto it = first; it != joins.end() && it->from == v; ++it){
            int u = it->to;
            if (closed[u] || !JoinOpen(z,*it,who,from_ground,to_ground)){
                continue;
            }
            float c = cost[v] + it->length / it->speed;
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

WalkPlace Walkers::GoalPlace(const ZoneState& z, int to, int who, int to_layer){
    WalkPlace goal;
    goal.plot = to;
    if (!world || world != z.world){
        return goal;
    }
    EnsureGraph();
    EnsureDecks(z);
    //The ground asked for, where there is ground to stand on: not under a block or a stair. A zombie's is always the
    //ground, whatever stands on it: it walks no deck.
    if ((to_layer == WALK_GROUND && !ZoneCastleShuts(z,to)) || who == WALK_WHO_ZOMBIE){
        return goal;
    }
    bool f_asked = false;
    for (int i = FirstDeck(to); i < (int)decks.size() && decks[i].plot == to; i++){
        if ((!decks[i].f_posted || who == WALK_WHO_POSTED) && !f_asked){
            goal.layer = decks[i].layer;    //in layer order: the last he may walk is the top
            f_asked = decks[i].layer == to_layer;
        }
    }
    return goal;
}

//The economy's people (Economy.h) walk these: the same graph, rules and A* the debug walker had.
bool Walkers::PlanRoute(const ZoneState& z, WalkPlace from, int to, int who, std::vector<WalkPlace>& places,
                        std::vector<float>& speeds, int to_layer, const WalkBreaks* breaks){
    places.clear();
    speeds.clear();
    if (!world || world != z.world){
        return false;
    }
    EnsureGraph();
    EnsureLinks(z);
    EnsureDecks(z);
    int n = (int)world->grid->fine.pos.size();
    if (from.plot < 0 || from.plot >= n || to < 0 || to >= n){
        return false;
    }
    //From the deck he is on; one pulled down from under him, from its plot's ground.
    int start = from.plot;
    int deck = (from.layer != WALK_GROUND) ? DeckNode(from.plot,from.layer) : -1;
    if (deck >= 0){
        start = deck;
        if (decks[deck - n].f_posted){
            who = WALK_WHO_POSTED;      //released on a wall: he still gets down off it
        }
    }
    WalkPlace goal = GoalPlace(z,to,who,to_layer);
    int end = (goal.layer != WALK_GROUND) ? DeckNode(goal.plot,goal.layer) : to;
    std::vector<int> path = FindPath(z,start,end,who,breaks);
    for (size_t i = 0; i < path.size(); i++){
        WalkPlace p;
        p.plot = (path[i] < n) ? path[i] : decks[path[i] - n].plot;
        p.layer = (path[i] < n) ? (uint8_t)WALK_GROUND : decks[path[i] - n].layer;
        places.push_back(p);
        if (i == 0){
            continue;
        }
        //A grid edge's speed as ever; a join's its own.
        int a = path[i - 1];
        int b = path[i];
        float speed = WALKER_SPEED_GROUND;
        if (a < n && b < n){
            speed = EdgeSpeed(z,a,b);
        }else{
            for (const Join& j : joins){
                if (j.from == a && j.to == b){
                    speed = j.speed;
                }
            }
        }
        speeds.push_back(speed);
    }
    return !places.empty();
}

/*
    No walk starts on the neighbour (`from` -1), so a built one is shut, as it is to anyone passing. Behind an
    open one, the open ground is walked out over - by the same edges, never onto a built plot - only until there
    is enough of it, so a door costs a few dozen plots looked at, not the map. Each pocket is walked once.
*/
bool Walkers::OpenFromOutside(const ZoneState& z, int v){
    if (!world || world != z.world){
        return false;
    }
    EnsureGraph();
    EnsureLinks(z);
    if (!graph || v < 0 || v + 1 >= (int)graph->start.size()){
        return false;
    }
    std::vector<int> seen;
    for (int i = graph->start[v]; i < graph->start[v + 1]; i++){
        int n = graph->next[i];
        if (!EdgeOpen(z,n,v,-1,v) || std::find(seen.begin(),seen.end(),n) != seen.end()){
            continue;
        }
        size_t first = seen.size();
        seen.push_back(n);
        for (size_t q = first; q < seen.size() && seen.size() - first < WALKER_OPEN_GROUND; q++){
            int a = seen[q];
            for (int j = graph->start[a]; j < graph->start[a + 1]; j++){
                int b = graph->next[j];
                if (b != v && std::find(seen.begin(),seen.end(),b) == seen.end() && EdgeOpen(z,a,b,-1,-1)){
                    seen.push_back(b);
                }
            }
        }
        if (seen.size() - first >= WALKER_OPEN_GROUND){
            return true;
        }
    }
    return false;
}

void Walkers::SetSeason(uint64_t tick){
    //UnderSnow's own figures (Economy.cpp), so the ice a walk takes is where a collector freezes and the view draws it.
    float cover = CalendarSnowCover(tick);
    f_ice = cover > 0.0f && world;
    ice_front = f_ice ? CalendarSnowFrontZ(cover,world->grid->bounds_min.y,world->grid->bounds_max.y) : 0.0f;
}

bool Walkers::Frozen(int v) const{
    return f_ice && world && v >= 0 && v < (int)world->grid->fine.pos.size() && CalendarSnowAt(world->grid->fine.pos[v],ice_front);
}

bool Walkers::FrozenInWinter(int v) const{
    if (!world || v < 0 || v >= (int)world->grid->fine.pos.size()){
        return false;
    }
    float front = CalendarSnowFrontZ(1.0f,world->grid->bounds_min.y,world->grid->bounds_max.y);
    return CalendarSnowAt(world->grid->fine.pos[v],front);
}

bool Walkers::IceOpen(int who, int v) const{
    return (who == WALK_WHO_POSTED || who == WALK_WHO_ICE) && Frozen(v);
}

void Walkers::MapFlood(const ZoneState& z, const std::vector<int>& from, float reach, std::vector<float>& dist, int ice){
    EnsureGraph();
    EnsureLinks(z);
    if (!graph || !world || z.world != world){
        dist.clear();
        return;
    }
    const Terrain& t = *world->terrain;
    int n = (int)world->grid->fine.pos.size();
    dist.assign(n,INFINITY);
    //Ground a walk goes on from: dry, or a bridge's deck - or frozen water, if asked; never the mountain.
    auto onward = [&](int v){
        bool f_ice = (ice == WALK_ICE_NOW) ? Frozen(v) : (ice == WALK_ICE_WINTER) ? FrozenInWinter(v) : false;
        return (!t.wet[v] || ZoneBridgeWalkable(z,v) || f_ice) && !t.Mountain(v);
    };
    typedef std::pair<float,int> Entry;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    for (int v : from){
        if (v >= 0 && v < n && dist[v] > 0.0f){
            dist[v] = 0.0f;
            open.push(Entry(0.0f,v));
        }
    }
    while (!open.empty()){
        Entry e = open.top();
        open.pop();
        int v = e.second;
        if (e.first > dist[v] || !onward(v)){
            continue;
        }
        for (int i = graph->start[v]; i < graph->start[v + 1]; i++){
            int u = graph->next[i];
            //A cliff only by a winch; and nothing too steep, as EdgeOpen - what stands on either plot aside.
            if (!t.SameGround(v,u) ? !IsLink(v,u) : std::fabs(t.ground[u] - t.ground[v]) > WALKER_STEEPEST * graph->length[i]){
                continue;
            }
            float d = dist[v] + graph->length[i];
            if (d <= reach && d < dist[u]){
                dist[u] = d;
                open.push(Entry(d,u));
            }
        }
    }
}

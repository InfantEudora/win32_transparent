#include "TitleTown.h"

#include <algorithm>
#include <cmath>

/*
    THE TOWN'S PLAN, round its middle (TitleTownCentre), world units: a main road east and west out through two gates
    in a palisade ring, a cross road inside it, houses of one to three storeys along both with gardens behind, a store
    by the crossroads and a guard tower inside the east gate; outside, a woodcutter's hut by the west gate, two fields
    in the band to the south and the range to the north. Every number is the look, not a rule - tune freely.
*/
#define TOWN_WALL           TITLE_TOWN_WALL     //the palisade's ring
#define TOWN_HOUSES         14.0f   //houses inside this
#define TOWN_HOUSE_CLEAR    2.5f    //...and outside this: the crossroads stays open
#define TOWN_GARDENS        15.5f   //gardens inside this
#define TOWN_ROAD_LONG      27.0f   //the main road, east and west, out through the gates
#define TOWN_ROAD_SHORT     12.0f   //the cross road, north and south, inside
#define TOWN_FIELDS_IN      20.0f   //the fields' band outside the wall
#define TOWN_FIELDS_OUT     30.0f
#define TOWN_FIELD_CELLS    8       //a field's cells at most (ZONE_FIELD_MAX_CELLS)
#define TOWN_RANGE_CELLS    3
#define TOWN_PAIRED         0.50f   //of the houses, the share that take a second plot beside the first
#define TOWN_TWO_STOREYS    0.45f   //...the share of two storeys or more
#define TOWN_THREE_STOREYS  0.85f   //...of three: what is past this - a village, not a city
#define TOWN_GARDEN_SHARE   0.55f   //of the free plots behind the houses, the share that are gardens

namespace {

//splitmix64, as the forest's: each choice from the plot and a salt, so none depends on the order plots are visited.
uint64_t Mix(uint64_t x){
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

float Roll(int plot, int salt){
    uint64_t h = Mix(((uint64_t)TITLE_TOWN_SEED << 40) ^ ((uint64_t)(uint32_t)plot << 8) ^ (uint64_t)(uint32_t)salt);
    return (float)(h >> 40) / (float)(1ull << 24);
}

//The plots along a line from a to b, in order, each once: sampled finer than a plot, so the chain is joined.
std::vector<int> PlotsAlong(const ChasmWorld& w, const vec2& a, const vec2& b){
    std::vector<int> out;
    float len = (b - a).length();
    int steps = std::max(1,(int)std::ceil(len / 0.4f));
    for (int i = 0; i <= steps; i++){
        GridPick p = w.picker->Pick(a + (b - a) * ((float)i / (float)steps));
        if (p.f_hit && p.plot >= 0 && std::find(out.begin(),out.end(),p.plot) == out.end()){
            out.push_back(p.plot);
        }
    }
    return out;
}

vec2 CoarseCentre(const Grid& g, int c){
    vec2 s(0.0f,0.0f);
    for (int k = 0; k < 4; k++){
        s = s + g.coarse.pos[g.coarse.quads[c].v[k]];
    }
    return s * 0.25f;
}

//Radians round the middle, from +x toward +z (south): the town's plan is laid out by these.
float AngleOf(const vec2& d){
    return std::atan2(d.y,d.x);
}

bool InSector(float a, float from, float to){
    return a >= from && a <= to;
}

}

vec2 TitleTownCentre(const ChasmWorld& w){
    static const std::vector<uint8_t> none;
    return TitleTownCentre(*w.grid,w.forest ? w.forest->core : none);
}

vec2 TitleTownCentre(const Grid& g, const std::vector<uint8_t>& core){
    vec2 mid = (g.bounds_min + g.bounds_max) * 0.5f;
    vec2 span = g.bounds_max - g.bounds_min;
    if (core.size() != g.fine.pos.size()){
        return mid;
    }
    //Over the map's middle third: each spot by the forest cores within the wall and a little past it, the nearer the
    //middle the better on a tie. Held to the middle third so the whole capture lies on the map.
    const float reach = TOWN_WALL + 6.0f;
    float best = 1e30f;
    vec2 at = mid;
    for (int j = -4; j <= 4; j++){
        for (int i = -6; i <= 6; i++){
            vec2 c = mid + vec2(span.x * (float)i / 36.0f,span.y * (float)j / 24.0f);
            int cores = 0;
            for (size_t v = 0; v < g.fine.pos.size(); v++){
                vec2 d = g.fine.pos[v] - c;
                if (core[v] && d.dot(d) < reach * reach){
                    cores++;
                }
            }
            float score = (float)cores + (c - mid).length() * 0.01f;
            if (score < best){
                best = score;
                at = c;
            }
        }
    }
    return at;
}

void Zones::PlaceTitleTown(){
    if (!state.world || !state.world->forest){
        return;
    }
    const ChasmWorld& w = *state.world;
    const Grid& g = *w.grid;
    const vec2 c = TitleTownCentre(w);
    auto dist = [&](int v){ return (g.fine.pos[v] - c).length(); };
    //Strokes no drag reaches, a building or a line each, put back after - as the start camp's.
    uint32_t stroke = 0x700000u;
    auto next = [&](){ return ++stroke; };

    //THE ROADS: the main one out through both gates, the cross road inside the wall.
    std::vector<int> road = PlotsAlong(w,c - vec2(TOWN_ROAD_LONG,0.0f),c + vec2(TOWN_ROAD_LONG,0.0f));
    std::vector<int> cross = PlotsAlong(w,c - vec2(0.0f,TOWN_ROAD_SHORT),c + vec2(0.0f,TOWN_ROAD_SHORT));
    uint32_t s = next();
    for (int v : road){
        Apply(ZONE_OP_GROUND_PAINT,(uint32_t)v,ZONE_GROUND_ROAD,s);
    }
    s = next();
    for (int v : cross){
        Apply(ZONE_OP_GROUND_PAINT,(uint32_t)v,ZONE_GROUND_ROAD,s);
    }
    road.insert(road.end(),cross.begin(),cross.end());

    //THE PALISADE: a ring round the town, broken where the main road goes out - its two gates.
    s = next();
    std::vector<int> ring;
    for (int a = 0; a < 720; a++){
        float r = (float)a * 3.1415927f / 360.0f;
        GridPick p = w.picker->Pick(c + vec2(std::cos(r),std::sin(r)) * TOWN_WALL);
        if (p.f_hit && p.plot >= 0 && std::find(ring.begin(),ring.end(),p.plot) == ring.end()){
            ring.push_back(p.plot);
        }
    }
    for (int v : ring){
        if (state.ground[v] != ZONE_GROUND_ROAD){
            Apply(ZONE_OP_GROUND_PAINT,(uint32_t)v,ZONE_GROUND_WALL,s);
        }
    }

    //THE LOTS: every plot beside a road inside the houses' ring, in order round the middle.
    std::vector<int> lots;
    for (int v : road){
        int nb[8];
        int n = ZonePlotNeighbours(w,v,nb,8);
        for (int j = 0; j < n; j++){
            int u = nb[j];
            if (state.ground[u] == ZONE_GROUND_NONE && !state.building[u] && dist(u) < TOWN_HOUSES && dist(u) > TOWN_HOUSE_CLEAR
                && std::find(lots.begin(),lots.end(),u) == lots.end()){
                lots.push_back(u);
            }
        }
    }
    std::sort(lots.begin(),lots.end(),[&](int a, int b){
        float aa = AngleOf(g.fine.pos[a] - c);
        float ab = AngleOf(g.fine.pos[b] - c);
        return (aa != ab) ? aa < ab : a < b;
    });
    //THE STORE: the lot nearest the crossroads to the north-east, and one beside it.
    int store = -1;
    for (int v : lots){
        float a = AngleOf(g.fine.pos[v] - c);
        if (InSector(a,-1.5708f,0.0f) && (store < 0 || dist(v) < dist(store))){
            store = v;
        }
    }
    if (store >= 0){
        s = next();
        Apply(ZONE_OP_BUILD_PAINT,(uint32_t)store,ZONE_KIND_STORE,s);
        int nb[8];
        int n = ZonePlotNeighbours(w,store,nb,8);
        for (int j = 0; j < n; j++){
            if (std::find(lots.begin(),lots.end(),nb[j]) != lots.end() && !state.building[nb[j]]
                && Apply(ZONE_OP_BUILD_PAINT,(uint32_t)nb[j],ZONE_KIND_STORE,s)){
                break;
            }
        }
    }
    //THE HOUSES: a house a lot, half of them taking the next lot too, one to three storeys.
    std::vector<int> houses;
    for (int v : lots){
        if (state.building[v]){
            continue;
        }
        s = next();
        if (!Apply(ZONE_OP_BUILD_PAINT,(uint32_t)v,ZONE_KIND_HOUSE,s)){
            continue;
        }
        std::vector<int> plots(1,v);
        if (Roll(v,1) < TOWN_PAIRED){
            int nb[8];
            int n = ZonePlotNeighbours(w,v,nb,8);
            for (int j = 0; j < n; j++){
                if (std::find(lots.begin(),lots.end(),nb[j]) != lots.end() && !state.building[nb[j]]
                    && Apply(ZONE_OP_BUILD_PAINT,(uint32_t)nb[j],ZONE_KIND_HOUSE,s)){
                    plots.push_back(nb[j]);
                    break;
                }
            }
        }
        float tall = Roll(v,2);
        int storeys = (tall >= TOWN_THREE_STOREYS) ? 3 : (tall >= 1.0f - TOWN_TWO_STOREYS) ? 2 : 1;
        for (int k = 1; k < storeys; k++){
            for (int p : plots){
                Apply(ZONE_OP_BUILD_ADD,(uint32_t)p,ZONE_KIND_HOUSE,s);
            }
        }
        houses.insert(houses.end(),plots.begin(),plots.end());
    }
    //GARDENS: some of the free plots behind the houses, each its house's (yards_plan.md).
    for (int h : houses){
        int nb[8];
        int n = ZonePlotNeighbours(w,h,nb,8);
        for (int j = 0; j < n; j++){
            int u = nb[j];
            if (state.ground[u] == ZONE_GROUND_NONE && !state.building[u] && dist(u) < TOWN_GARDENS
                && Roll(u,3) < TOWN_GARDEN_SHARE){
                Apply(ZONE_OP_GROUND_PAINT,(uint32_t)u,ZONE_GROUND_GARDEN,0,false,state.building[h]);
            }
        }
    }
    //A GUARD TOWER inside the east gate, beside the road; a WOODCUTTER'S HUT out of the west gate, by the trees.
    auto open_near = [&](const vec2& p){
        int best = -1;
        float best_d = 1e30f;
        for (int v = 0; v < (int)g.fine.pos.size(); v++){
            float d = (g.fine.pos[v] - p).length();
            if (d < best_d && state.ground[v] == ZONE_GROUND_NONE && !state.building[v]){
                best = v;
                best_d = d;
            }
        }
        return best;
    };
    int tower = open_near(c + vec2(TOWN_WALL - 2.5f,-2.5f));
    if (tower >= 0){
        Apply(ZONE_OP_BUILD_PAINT,(uint32_t)tower,ZONE_KIND_TOWER,next());
    }
    int hut = open_near(c + vec2(-(TOWN_WALL + 4.0f),3.5f));
    if (hut >= 0){
        Apply(ZONE_OP_BUILD_PAINT,(uint32_t)hut,ZONE_KIND_WOODCUTTER,next());
    }

    //THE FIELDS, south-east and south-west in the band outside the wall, and THE RANGE to the north: coarse cells,
    //nearest the wall first.
    auto cells_in = [&](float from, float to, float in, float out){
        std::vector<std::pair<float,int>> found;
        for (int q = 0; q < (int)g.coarse.quads.size(); q++){
            vec2 d = CoarseCentre(g,q) - c;
            float r = d.length();
            if (r >= in && r <= out && InSector(AngleOf(d),from,to)){
                found.emplace_back(r,q);
            }
        }
        std::sort(found.begin(),found.end());
        return found;
    };
    const int crops[2] = {ZONE_CROP_WHEAT,ZONE_CROP_BEANS};
    const float sectors[2][2] = {{0.35f,1.25f},{1.90f,2.80f}};
    for (int f = 0; f < 2; f++){
        s = next();
        int first = -1;
        int made = 0;
        for (const std::pair<float,int>& q : cells_in(sectors[f][0],sectors[f][1],TOWN_FIELDS_IN,TOWN_FIELDS_OUT)){
            if (made < TOWN_FIELD_CELLS && Apply(ZONE_OP_FIELD_PAINT,(uint32_t)q.second,0,s)){
                first = (first < 0) ? q.second : first;
                made++;
            }
        }
        if (first >= 0){
            Apply(ZONE_OP_FIELD_CROP,(uint32_t)first,crops[f]);
        }
    }
    s = next();
    int range = 0;
    for (const std::pair<float,int>& q : cells_in(-2.20f,-1.60f,TOWN_FIELDS_IN,TOWN_FIELDS_OUT)){
        if (range < TOWN_RANGE_CELLS && Apply(ZONE_OP_RANGE_PAINT,(uint32_t)q.second,0,s)){
            range++;
        }
    }
    state.stroke = 0;
    state.stroke_building = 0;
    last_refusal.clear();
}

std::vector<TitleTownPerson> TitleTownPeople(const ChasmWorld& w, const ZoneState& z){
    std::vector<TitleTownPerson> out;
    if (z.world.get() != &w){
        return out;
    }
    const Grid& g = *w.grid;
    const vec2 c = TitleTownCentre(w);
    auto ground = [&](const vec2& p){
        GridPick k = w.picker->Pick(p);
        float y = (k.f_hit && k.plot >= 0) ? w.terrain->GroundHeight(p,w.terrain->Height(k.plot)) : 0.0f;
        return vec3(p.x,y,p.y);
    };
    auto facing = [](const vec2& d){
        float l = d.length();
        return (l > 1e-4f) ? d * (1.0f / l) : vec2(0.0f,1.0f);
    };
    //The first plot of each kind of building, and of a garden.
    auto first_of = [&](int kind){
        for (int v = 0; v < (int)z.building.size(); v++){
            uint32_t id = z.building[v];
            if (id && id < z.buildings.size() && z.buildings[id].kind == kind){
                return v;
            }
        }
        return -1;
    };
    //Two walking in from the east gate, a man and a woman, a little apart, each on his side of the road.
    for (int i = 0; i < 2; i++){
        TitleTownPerson p;
        p.figure = i;
        p.clip = "Walking";
        p.speed = (i == 0) ? 1.05f : 0.9f;
        p.phase = 0.35f * (float)i;
        float side = (i == 0) ? -0.4f : 0.4f;
        for (float x = TOWN_WALL + 7.0f - 5.0f * (float)i; x >= 3.0f; x -= 0.75f){
            p.path.push_back(ground(c + vec2(x,side)));
        }
        p.at = p.path.front();
        p.forward = vec2(-1.0f,0.0f);
        out.push_back(p);
    }
    //At the store, a man with a sack, turned to the crossroads.
    int store = first_of(ZONE_KIND_STORE);
    if (store >= 0){
        vec2 at = g.fine.pos[store] + facing(c - g.fine.pos[store]) * 1.3f;
        TitleTownPerson p;
        p.figure = 0;
        p.load = 2;
        p.at = ground(at);
        p.forward = facing(c - at);
        p.phase = 0.2f;
        out.push_back(p);
    }
    //In a garden, a woman.
    for (int v = 0; v < (int)z.ground.size(); v++){
        if (z.ground[v] == ZONE_GROUND_GARDEN){
            TitleTownPerson p;
            p.figure = 1;
            p.at = ground(g.fine.pos[v]);
            p.forward = facing(c - g.fine.pos[v]);
            p.phase = 0.6f;
            out.push_back(p);
            break;
        }
    }
    //By the woodcutter's hut, a man with a log on his shoulder.
    int hut = first_of(ZONE_KIND_WOODCUTTER);
    if (hut >= 0){
        TitleTownPerson p;
        p.figure = 0;
        p.load = 1;
        vec2 at = g.fine.pos[hut] + facing(c - g.fine.pos[hut]) * 1.2f;
        p.at = ground(at);
        p.forward = facing(c - at);
        p.phase = 0.5f;
        out.push_back(p);
    }
    //At the range, an archer: on its cell nearest the town, shooting out along it.
    int near_cell = -1;
    int far_cell = -1;
    for (int q = 0; q < (int)z.field.size(); q++){
        uint32_t id = z.field[q];
        if (!id || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_RANGE){
            continue;
        }
        float d = (CoarseCentre(g,q) - c).length();
        if (near_cell < 0 || d < (CoarseCentre(g,near_cell) - c).length()){
            near_cell = q;
        }
        if (far_cell < 0 || d > (CoarseCentre(g,far_cell) - c).length()){
            far_cell = q;
        }
    }
    if (near_cell >= 0){
        vec2 from = CoarseCentre(g,near_cell);
        vec2 to = (far_cell != near_cell) ? CoarseCentre(g,far_cell) : from + facing(from - c) * 4.0f;
        TitleTownPerson p;
        p.figure = 0;
        p.clip = "Shoot";
        p.f_archer = true;
        p.at = ground(from);
        p.forward = facing(to - from);
        out.push_back(p);
    }
    return out;
}

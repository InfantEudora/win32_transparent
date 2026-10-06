#include "Zones.h"
#include "ChasmWorld.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

const char* ZoneOpName(int op){
    switch (op){
        case ZONE_OP_BUILD_PAINT:   return "build_paint";
        case ZONE_OP_BUILD_ADD:     return "build_add";
        case ZONE_OP_BUILD_REMOVE:  return "build_remove";
        case ZONE_OP_BUILD_ERASE:   return "build_erase";
        case ZONE_OP_FIELD_PAINT:   return "field_paint";
        case ZONE_OP_FIELD_ERASE:   return "field_erase";
        case ZONE_OP_GROUND_PAINT:  return "ground_paint";
        case ZONE_OP_GROUND_ERASE:  return "ground_erase";
        case ZONE_OP_FIELD_CROP:    return "field_crop";
        case ZONE_OP_STORE_ALLOW:   return "store_allow";
        case ZONE_OP_BUILD_RAISE:   return "build_raise";
        case ZONE_OP_BRIDGE:        return "bridge";
        case ZONE_OP_GROUND_RAISE:  return "ground_raise";
        default:                   return "none";
    }
}

int ZoneOpByName(const std::string& name){
    for (int i = 1; i < ZONE_OP_COUNT; i++){
        if (name == ZoneOpName(i)){
            return i;
        }
    }
    //The names the ops had while every building was a house - scripts and tools still use them.
    if (name == "house_paint") return ZONE_OP_BUILD_PAINT;
    if (name == "house_add") return ZONE_OP_BUILD_ADD;
    if (name == "house_remove") return ZONE_OP_BUILD_REMOVE;
    if (name == "house_erase") return ZONE_OP_BUILD_ERASE;
    return -1;
}

const char* ZoneKindName(int kind){
    switch (kind){
        case ZONE_KIND_HOUSE:       return "house";
        case ZONE_KIND_STORE:       return "store";
        case ZONE_KIND_WOODCUTTER:  return "woodcutter";
        case ZONE_KIND_WATER:       return "water";
        case ZONE_KIND_FIELD:       return "field";
        case ZONE_KIND_WINCH:       return "winch";
        case ZONE_KIND_CAMP:        return "camp";
        case ZONE_KIND_BRIDGE:      return "bridge";
        default:                    return "none";
    }
}

int ZoneKindByName(const std::string& name){
    for (int k = 1; k < ZONE_KIND_COUNT; k++){
        if (name == ZoneKindName(k)){
            return k;
        }
    }
    return -1;
}

/*
    How high each kind may go. A house to the most there is; a store a little less - a granary, not a
    tower; a workshop two; a water collector is a shed by the water.
*/
int ZoneKindMaxStoreys(int kind){
    switch (kind){
        case ZONE_KIND_HOUSE:       return 4;
        case ZONE_KIND_STORE:       return 3;
        case ZONE_KIND_WOODCUTTER:  return 2;
        case ZONE_KIND_WATER:       return 1;
        case ZONE_KIND_WINCH:       return 1;
        case ZONE_KIND_CAMP:        return 1;
        case ZONE_KIND_BRIDGE:      return 1;
        default:                    return 0;
    }
}

const char* ZoneCropName(int crop){
    switch (crop){
        case ZONE_CROP_WHEAT:       return "wheat";
        case ZONE_CROP_GREENS:      return "greens";
        case ZONE_CROP_BEANS:       return "beans";
        default:                    return "none";
    }
}

int ZoneCropByName(const std::string& name){
    for (int c = 0; c < ZONE_CROP_COUNT; c++){
        if (name == ZoneCropName(c)){
            return c;
        }
    }
    return -1;
}

const char* ZoneGroundName(int ground){
    switch (ground){
        case ZONE_GROUND_GARDEN:    return "garden";
        case ZONE_GROUND_LOT:       return "lot";
        case ZONE_GROUND_ROAD:      return "road";
        default:                    return "none";
    }
}

int ZoneBoundaryBetween(const ZoneState& z, int a, int b){
    bool in_a = z.storeys[a] > 0 || ZoneGroundStands(z,a);
    bool in_b = z.storeys[b] > 0 || ZoneGroundStands(z,b);
    if (in_a == in_b){
        return ZONE_BOUNDARY_NONE;
    }
    int inside = in_a ? a : b;
    if (z.storeys[inside] > 0){
        return ZONE_BOUNDARY_NONE;     //a building's own wall bounds it
    }
    return (z.ground[inside] == ZONE_GROUND_GARDEN) ? ZONE_BOUNDARY_GARDEN_WALL : ZONE_BOUNDARY_PALISADE;
}

int ZonePlotNeighbours(const ChasmWorld& w, int v, int* out, int max){
    const GridPicker& p = *w.picker;
    int n = 0;
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        int qc = p.PlotQuadCorner(v,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        int both[2] = {q.v[(k + 1) % 4],q.v[(k + 3) % 4]};
        for (int u : both){
            bool f_seen = false;
            for (int j = 0; j < n; j++){
                f_seen = f_seen || out[j] == u;
            }
            if (!f_seen && n < max){
                out[n++] = u;
            }
        }
    }
    return n;
}

namespace {

#define ZONE_GATE_AHEAD     0.34f   //cos of the most a gate may be off the road's line: about 70 degrees

}

int ZoneGateOf(const ChasmWorld& w, const ZoneState& z, int road){
    if (z.ground[road] != ZONE_GROUND_ROAD){
        return -1;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,road,nb,8);
    int from = -1;
    for (int i = 0; i < n; i++){
        if (z.ground[nb[i]] == ZONE_GROUND_ROAD){
            if (from >= 0){
                return -1;      //two road neighbours: the road goes on, it does not end here
            }
            from = nb[i];
        }
    }
    if (from < 0){
        return -1;              //a lone road plot has no direction to open a gate in
    }
    const std::vector<vec2>& pos = w.grid->fine.pos;
    vec2 in = pos[road] - pos[from];
    in = in / std::max(1e-6f,in.length());
    int best = -1;
    float best_dot = ZONE_GATE_AHEAD;
    for (int i = 0; i < n; i++){
        int g = nb[i];
        if (ZoneBoundaryBetween(z,road,g) == ZONE_BOUNDARY_NONE || z.storeys[g] > 0){
            continue;
        }
        vec2 d = pos[g] - pos[road];
        float dot = in.dot(d / std::max(1e-6f,d.length()));
        if (dot > best_dot){
            best_dot = dot;
            best = g;
        }
    }
    return best;
}

bool ZoneGateBetween(const ChasmWorld& w, const ZoneState& z, int a, int b){
    return ZoneGateOf(w,z,a) == b || ZoneGateOf(w,z,b) == a;
}

namespace {

//A road plot with two road neighbours and every other neighbour a building of two storeys or more: what
//could be bridged. The storeys it would be bridged to, or 0. Its road neighbours in `roads_out`.
int ArchCandidate(const ChasmWorld& w, const ZoneState& z, int plot, int* roads_out){
    if (z.ground[plot] != ZONE_GROUND_ROAD){
        return 0;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    int roads = 0;
    int top = ZONE_MAX_STOREYS;
    for (int i = 0; i < n; i++){
        int u = nb[i];
        if (z.ground[u] == ZONE_GROUND_ROAD){
            if (roads < 2){
                roads_out[roads] = u;
            }
            roads++;
        }else if (z.standing[u] >= 2){
            //What stands: a site's storeys bridge nothing yet.
            top = std::min(top,(int)z.standing[u]);
        }else{
            return 0;           //an open side: nothing to bridge from
        }
    }
    //Two road neighbours: the road goes through, straight or round a bend - Townscaper's passages bend
    //too. A dead end under a house would be a tunnel to nowhere, a junction a hall.
    return (roads == 2 && n >= 4) ? top : 0;
}

//Plots: through a row of houses, not down a street. Three, not two: a road crossing a row steps along
//the grid's edges, so even a row two houses deep takes it three plots to cross.
#define ZONE_ARCH_MAX_RUN   3

}

int ZoneArchStoreys(const ChasmWorld& w, const ZoneState& z, int plot){
    int roads[2];
    int top = ArchCandidate(w,z,plot,roads);
    if (top == 0){
        return 0;
    }
    /*
        Only a SHORT run is bridged. A street lined both sides with tall houses makes every plot down it
        a candidate, and covering it would make a tunnel; a hole through a row of houses is a run of one
        or two. So walk the run both ways from here and give up past the most it may be.
    */
    int run = 1;
    for (int side = 0; side < 2 && run <= ZONE_ARCH_MAX_RUN; side++){
        int prev = plot;
        int at = roads[side];
        while (run <= ZONE_ARCH_MAX_RUN){
            int next[2];
            if (ArchCandidate(w,z,at,next) == 0){
                break;
            }
            run++;
            int step = (next[0] == prev) ? next[1] : next[0];
            prev = at;
            at = step;
        }
    }
    return (run <= ZONE_ARCH_MAX_RUN) ? top : 0;
}

/*
    SLOPE (biomes_plan.md step 2): the most the ground may rise across what is built on it, world units -
    over a plot for a building or ground (every corner of every cell round it), over its nine vertices
    for a field. A road takes more: it climbs; so does a water collector, which stands on a bank. A
    building on a slope stands on a footing (BuildingMesh).
*/
#define ZONE_RISE_HOUSE     0.7f
#define ZONE_RISE_ROAD      1.4f
#define ZONE_RISE_FIELD     1.3f
/*
    What a building and ground both need of their plot: flat all round it - every corner of every
    fine cell round the plot on the plot's level, so nothing stands half over a cliff - dry, and no
    field touching it. `f_waterside` turns the water rule round, for a water collector: some corner
    must be wet, and the plot's own vertex dry - the last dry plot before the bank's stones, which its
    pipe reaches over (the user, 2026-10-06), and which its carrier can walk to.
*/
static bool PlotIsBuildable(const ChasmWorld& w, const ZoneState& z, int plot, const char** why,
                            float max_rise = ZONE_RISE_HOUSE, bool f_waterside = false, bool f_path = false){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    const Grid& g = *w.grid;
    if (plot < 0 || plot >= (int)g.fine.pos.size()){
        return refuse("no such plot");
    }
    if (g.fine.f_boundary[plot]){
        return refuse("on the map's edge");
    }
    const Terrain& t = *w.terrain;
    int level = t.level[plot];
    if (level == TERRAIN_FLOOR){
        return refuse("on the chasm floor - nothing is built under the mist");
    }
    if (level == TERRAIN_BARE){
        return refuse("on bare rock - a ledge too small to build on");
    }
    const GridPicker& p = *w.picker;
    float lowest = t.ground[plot];
    float highest = lowest;
    bool f_water = false;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        int q = p.PlotQuadCorner(plot,i) / 4;
        const GridQuad& quad = g.fine.quads[q];
        for (int k = 0; k < 4; k++){
            int v = quad.v[k];
            if (!t.SameGround(v,plot)){
                return refuse("too close to a cliff");
            }
            lowest = std::min(lowest,t.ground[v]);
            highest = std::max(highest,t.ground[v]);
            //A road is only a path: it may run beside the wet - along a bank, up to a bridge's end
            //(bridge_plan.md) - so long as its own plot is dry. Buildings and enclosed ground keep off.
            if (t.wet[v] && !f_waterside && !(f_path && v != plot)){
                return refuse("too close to water");
            }
            f_water = f_water || t.wet[v];
            if (t.Mountain(v)){
                return refuse("on the mountain");
            }
        }
        if (!z.field.empty() && z.field[quad.parent]){
            return refuse("a field is there");
        }
    }
    if (f_waterside){
        if (t.wet[plot]){
            return refuse("on the wet bank - a water collector stands at its edge, on dry ground");
        }
        if (!f_water){
            return refuse("not at the water - a water collector stands at the edge of a bank");
        }
    }
    if (highest - lowest > max_rise){
        return refuse("too steep");
    }
    return true;
}

int ZoneWinchLanding(const ChasmWorld& w, int plot){
    const Grid& g = *w.grid;
    const GridPicker& p = *w.picker;
    if (plot < 0 || plot >= (int)g.fine.pos.size()){
        return -1;
    }
    int best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& q = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = q.v[k];
            //A balcony BELOW the winch: from the rim, or from a balcony down to the next.
            if (w.terrain->level[v] != TERRAIN_BALCONY || w.terrain->steps[v] <= w.terrain->steps[plot]){
                continue;
            }
            float d = (g.fine.pos[v] - g.fine.pos[plot]).length();
            if (d < best_d || (d == best_d && v < best)){
                best_d = d;
                best = v;
            }
        }
    }
    return best;
}

void ZoneWinchLinks(const ChasmWorld& w, const ZoneState& z, std::vector<std::pair<int,int>>& out){
    out.clear();
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] && z.buildings[z.building[v]].kind == ZONE_KIND_WINCH){
            int landing = ZoneWinchLanding(w,(int)v);
            if (landing >= 0){
                out.push_back(std::make_pair((int)v,landing));
            }
        }
    }
}

//Whether `plot` is where a standing winch lands - which keeps it clear for whoever rides down.
static bool PlotIsWinchLanding(const ChasmWorld& w, const ZoneState& z, int plot){
    if (w.terrain->level[plot] != TERRAIN_BALCONY){
        return false;
    }
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& q = w.grid->fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = q.v[k];
            if (z.KindOf(v) == ZONE_KIND_WINCH && ZoneWinchLanding(w,v) == plot){
                return true;
            }
        }
    }
    return false;
}

/*
    The winch's own ground rule (Zones.h): the plot on the plateau or on a balcony - one may winch
    down twice (user, 2026-10-06); every corner of the cells round it on its own ground or a balcony
    below it, at least one below - the rim right above a ledge, not above the open chasm; the corners
    on its ground dry, off the mountain, no field, gentle; its landing clear.
*/
static bool WinchIsBuildable(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    const Grid& g = *w.grid;
    if (plot < 0 || plot >= (int)g.fine.pos.size()){
        return refuse("no such plot");
    }
    if (g.fine.f_boundary[plot]){
        return refuse("on the map's edge");
    }
    const Terrain& t = *w.terrain;
    if (t.level[plot] != TERRAIN_PLATEAU && t.level[plot] != TERRAIN_BALCONY){
        return refuse("a winch stands on the rim, above a balcony");
    }
    const GridPicker& p = *w.picker;
    bool f_balcony = false;
    float lowest = t.ground[plot];
    float highest = lowest;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& quad = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = quad.v[k];
            if (t.level[v] == TERRAIN_BALCONY && t.steps[v] > t.steps[plot]){
                f_balcony = true;
                continue;
            }
            if (!t.SameGround(v,plot)){
                return refuse("over the open chasm - a winch lowers to a balcony");
            }
            if (t.wet[v]){
                return refuse("too close to water");
            }
            if (t.Mountain(v)){
                return refuse("on the mountain");
            }
            lowest = std::min(lowest,t.ground[v]);
            highest = std::max(highest,t.ground[v]);
        }
        if (!z.field.empty() && z.field[quad.parent]){
            return refuse("a field is there");
        }
    }
    if (!f_balcony){
        return refuse("no balcony below - a winch stands on the rim above one");
    }
    if (highest - lowest > ZONE_RISE_HOUSE){
        return refuse("too steep");
    }
    int landing = ZoneWinchLanding(w,plot);
    if (landing < 0 || (!z.storeys.empty() && z.storeys[landing] > 0)){
        return refuse("its landing on the balcony is built on");
    }
    return true;
}

/*
    One plot of a bridge, wet or dry: on the map, on buildable ground, not the mountain, nothing on it.
    What a whole bridge needs - dry ends, a wet span, its length - is ZoneBridgeChain's, asked when it is
    placed; this is what each plot of one must still be (a load, the checks).
*/
static bool BridgePlotOk(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    if (plot < 0 || plot >= (int)g.fine.pos.size()){
        return refuse("no such plot");
    }
    if (g.fine.f_boundary[plot]){
        return refuse("on the map's edge");
    }
    if (!t.Buildable(plot) || t.Mountain(plot)){
        return refuse("not on ground a bridge can stand on");
    }
    if (!z.building.empty() && z.building[plot]){
        return refuse("a building is there");
    }
    if (!z.ground.empty() && z.ground[plot] != ZONE_GROUND_NONE){
        return refuse("a road or garden is there");
    }
    return true;
}

#define BRIDGE_MOST_PLOTS   40      //a chain longer than this is no bridge
#define BRIDGE_FALL_CLEAR   9.0f    //no bridge this near a fall's lip (Terrain.cpp's HEIGHT_FALL_CLEAR)

bool ZoneBridgeChain(const ChasmWorld& w, const ZoneState& z, int from, int to, std::vector<int>& plots, const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    plots.clear();
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    int n = (int)g.fine.pos.size();
    if (from < 0 || to < 0 || from >= n || to >= n || from == to){
        return refuse("a bridge needs two ends");
    }
    //The chain: each step to the neighbour nearest the far end, staying near the line - the lower index
    //on a tie - and always closer than the last, so it cannot wander.
    vec2 a = g.fine.pos[from];
    vec2 b = g.fine.pos[to];
    vec2 dir = b - a;
    float len = dir.length();
    dir = dir * (1.0f / std::max(1e-4f,len));
    plots.push_back(from);
    int at = from;
    while (at != to && (int)plots.size() < BRIDGE_MOST_PLOTS){
        int nb[8];
        int k = ZonePlotNeighbours(w,at,nb,8);
        int best = -1;
        float best_score = 1e30f;
        float here = (g.fine.pos[at] - b).length();
        for (int i = 0; i < k; i++){
            vec2 q = g.fine.pos[nb[i]];
            float left = (q - b).length();
            if (left >= here){
                continue;
            }
            vec2 off = (q - a) - dir * (q - a).dot(dir);
            float score = left + 2.0f * off.length();
            if (score < best_score || (score == best_score && nb[i] < best)){
                best_score = score;
                best = nb[i];
            }
        }
        if (best < 0){
            break;
        }
        plots.push_back(best);
        at = best;
    }
    if (at != to){
        return refuse("too far to bridge");
    }
    if (plots.size() < 3){
        return refuse("nothing to bridge - a bridge crosses wet ground");
    }
    if (t.wet[from] || t.wet[to]){
        return refuse("a bridge starts and ends on dry ground");
    }
    for (size_t i = 0; i < plots.size(); i++){
        int v = plots[i];
        if (!BridgePlotOk(w,z,v,why)){
            return false;
        }
        if (!t.SameGround(v,from)){
            return refuse("a bridge stays on one level");
        }
        if (i > 0 && i + 1 < plots.size() && !t.wet[v]){
            return refuse("a bridge crosses one stretch of water - dry ground between");
        }
        for (const TerrainFall& f : t.falls){
            if ((g.fine.pos[v] - f.lip).length() < BRIDGE_FALL_CLEAR){
                return refuse("too near a fall");
            }
        }
    }
    //The water it crosses, against the river's usual width: the span, less the two banks' wet margin.
    float banks = 2.0f * (TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN);
    float water = std::max(0.0f,len - banks);
    float usual = TerrainRiver().width;
    int river = -1;
    float along = 0.0f, across = 0.0f;
    if (t.RiverCoords((a + b) * 0.5f,len + 10.0f,river,along,across) && river >= 0 && river < (int)t.rivers.size()){
        usual = t.rivers[river].width;
    }
    if (water > ZONE_BRIDGE_SPAN_RIVERS * usual){
        return refuse("too wide - a bridge spans at most twice the river's usual width");
    }
    return true;
}

bool ZoneCanBuild(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t joining, const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (kind <= ZONE_KIND_NONE || kind >= ZONE_KIND_COUNT || kind == ZONE_KIND_FIELD){
        return refuse("no such building");
    }
    if (!z.ground.empty() && plot >= 0 && plot < (int)z.ground.size() && z.ground[plot] == ZONE_GROUND_ROAD){
        return refuse("a road is there");
    }
    if (kind == ZONE_KIND_WINCH){
        if (joining){
            return refuse("a winch is one plot");
        }
        return WinchIsBuildable(w,z,plot,why);
    }
    if (kind == ZONE_KIND_BRIDGE){
        return BridgePlotOk(w,z,plot,why);
    }
    if (plot >= 0 && plot < (int)w.grid->fine.pos.size() && PlotIsWinchLanding(w,z,plot)){
        return refuse("a winch lands there");
    }
    bool f_water = (kind == ZONE_KIND_WATER);
    if (!PlotIsBuildable(w,z,plot,why,f_water ? ZONE_RISE_ROAD : ZONE_RISE_HOUSE,f_water)){
        return false;
    }
    if (!z.ground.empty() && z.ground[plot] == ZONE_GROUND_ROAD){
        return refuse("a road is there");
    }
    if (joining && kind == ZONE_KIND_HOUSE && joining < z.buildings.size() &&
        z.buildings[joining].size >= ZONE_HOUSE_MAX_PLOTS){
        return refuse("a house is at most four plots - one family");
    }
    /*
        A new woodcutter's hut goes up beside a lot, where his woodpile will be (Economy). Only a NEW
        one: a plot joining a woodcutter already has the lot its first plot was put beside. A lot still
        a site is enough - it will stand. Erasing the lot later is allowed; he then has nowhere to pile
        and stops felling, which his card says.
    */
    if (kind == ZONE_KIND_WOODCUTTER && !joining && ZoneLotBeside(w,z,plot,false) < 0){
        return refuse("a woodcutter needs a lot beside it, for his woodpile");
    }
    return true;
}

int ZoneLotBeside(const ChasmWorld& w, const ZoneState& z, int v, bool f_built){
    if (v < 0 || v >= (int)z.ground.size()){
        return -1;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    int best = -1;
    for (int i = 0; i < n; i++){
        int o = nb[i];
        if (z.ground[o] != ZONE_GROUND_LOT || z.storeys[o] > 0 || (f_built && !z.ground_built[o])){
            continue;
        }
        if (best < 0 || o < best){
            best = o;
        }
    }
    return best;
}

bool ZoneCanGround(const ChasmWorld& w, const ZoneState& z, int plot, const char** why, int kind){
    if (!PlotIsBuildable(w,z,plot,why,(kind == ZONE_GROUND_ROAD) ? ZONE_RISE_ROAD : ZONE_RISE_HOUSE,false,
                         kind == ZONE_GROUND_ROAD)){
        return false;
    }
    if (kind == ZONE_GROUND_ROAD && !z.storeys.empty() && z.storeys[plot] > 0){
        if (why){
            *why = "a building is there";
        }
        return false;
    }
    /*
        A road stops at a garden's or a lot's edge rather than eating into it - so a road dragged into
        a walled garden ends against the wall, which is exactly where a gate opens (Zones.h). Erase the
        ground first to run a road through.
    */
    if (kind == ZONE_GROUND_ROAD && !z.ground.empty() && ZoneEnclosesGround(z.ground[plot])){
        if (why){
            *why = "a garden or lot is there";
        }
        return false;
    }
    return true;
}

bool ZoneCanField(const ChasmWorld& w, const ZoneState& z, int coarse, const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    const Grid& g = *w.grid;
    if (coarse < 0 || coarse >= (int)g.coarse.quads.size()){
        return refuse("no such cell");
    }
    //Its nine fine vertices are the corners of its four children, fine quads 4c..4c+3.
    int first = -1;
    float lowest = 1e30f;
    float highest = -1e30f;
    for (int q = coarse * 4; q < coarse * 4 + 4; q++){
        for (int k = 0; k < 4; k++){
            int v = g.fine.quads[q].v[k];
            if (first < 0){
                first = v;
            }else if (!w.terrain->SameGround(v,first)){
                return refuse("not flat - it crosses a cliff");
            }
            int level = w.terrain->level[v];
            if (level == TERRAIN_FLOOR){
                return refuse("on the chasm floor - nothing is built under the mist");
            }
            if (level == TERRAIN_BARE){
                return refuse("on bare rock - a ledge too small to farm");
            }
            //Anything may be built on a balcony but food: whoever lives down there is fed from the
            //rim, by winch (gameplay_plan.md, "Terraces").
            if (level == TERRAIN_BALCONY){
                return refuse("no fields on a balcony - its food comes down from the rim");
            }
            if (w.terrain->wet[v]){
                return refuse("too close to water");
            }
            if (w.terrain->Mountain(v)){
                return refuse("on the mountain");
            }
            if (!z.storeys.empty() && z.storeys[v] > 0){
                return refuse("a building is there");
            }
            if (!z.ground.empty() && z.ground[v] != ZONE_GROUND_NONE){
                return refuse("a garden or lot is there");
            }
            lowest = std::min(lowest,w.terrain->ground[v]);
            highest = std::max(highest,w.terrain->ground[v]);
        }
    }
    if (highest - lowest > ZONE_RISE_FIELD){
        return refuse("too steep");
    }
    return true;
}

bool ZoneCampFire(int plot){
    uint32_t h = (uint32_t)plot * 2654435761u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h % ZONE_CAMP_FIRE_EVERY) == 0;
}

float ZoneCampYaw(int plot){
    uint32_t h = (uint32_t)plot * 0x9E3779B1u + 0xCA4Bu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return (float)(h % 628) * 0.01f;
}

#define ZONE_CAMP_DOOR_OUT  1.05f   //from the plot's vertex: past the tent's front end (half its 1.25), and a step

vec2 ZoneCampDoor(const ChasmWorld& w, int plot){
    float yaw = ZoneCampYaw(plot);
    return w.grid->fine.pos[plot] + vec2(std::cos(yaw),std::sin(yaw)) * ZONE_CAMP_DOOR_OUT;
}

int ZoneCampTents(const ZoneState& z, uint32_t id){
    if (id == 0 || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_CAMP){
        return 0;
    }
    int n = 0;
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id && !ZoneCampFire((int)v)){
            n++;
        }
    }
    return n;
}

#define ZONE_CAMP_INLAND    30.0f   //from the middle of the home balcony's stretch of rim

int ZoneStartCampPlot(const ChasmWorld& w){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& p = *w.picker;
    //The home side's balcony - every map has one (gameplay_plan.md) - or, failing that, the east middle.
    vec2 target((g.bounds_min.x + g.bounds_max.x) * 0.5f + (g.bounds_max.x - g.bounds_min.x) * 0.25f,
                (g.bounds_min.y + g.bounds_max.y) * 0.5f);
    for (const GridFeature& f : g.layout.features){
        if (f.kind == GRID_FEATURE_BALCONY && f.side > 0 && f.line.points.size() >= 3){
            vec2 chord = (f.line.points.front() + f.line.points.back()) * 0.5f;
            vec2 out = chord - f.line.points[f.line.points.size() / 2];
            float l = out.length();
            if (l > 1e-3f){
                target = chord + out * (ZONE_CAMP_INLAND / l);
            }
            break;
        }
    }
    int best_v = -1;
    float best = 1e30f;
    for (size_t v = 0; v < g.fine.pos.size(); v++){
        if (t.level[v] != TERRAIN_PLATEAU || t.wet[v] || t.Mountain((int)v) || g.fine.f_boundary[v]){
            continue;
        }
        float d = (g.fine.pos[v] - target).length();
        if (d >= best){
            continue;
        }
        bool f_flat = true;
        for (int i = 0; i < p.PlotQuadCount((int)v) && f_flat; i++){
            const GridQuad& q = g.fine.quads[p.PlotQuadCorner((int)v,i) / 4];
            for (int c = 0; c < 4; c++){
                f_flat = f_flat && t.level[q.v[c]] == TERRAIN_PLATEAU && !t.wet[q.v[c]];
            }
        }
        if (f_flat){
            best = d;
            best_v = (int)v;
        }
    }
    return best_v;
}

void Zones::PlaceStartCamp(int tents){
    if (!state.world){
        return;
    }
    const ChasmWorld& w = *state.world;
    int start = ZoneStartCampPlot(w);
    if (start < 0){
        return;
    }
    //A stroke number no drag will reach, put back after: the camp is one building, made as a drag is.
    const uint32_t CAMP_STROKE = 0x7FFFFFu;
    std::vector<int> queue(1,start);
    std::vector<uint8_t> seen(state.building.size(),0);
    seen[start] = 1;
    int have = 0;
    for (size_t at = 0; at < queue.size() && have < tents; at++){
        int v = queue[at];
        uint32_t joining = state.stroke_building;
        if (ZoneCanBuild(w,state,v,ZONE_KIND_CAMP,joining,NULL) && Apply(ZONE_OP_BUILD_PAINT,(uint32_t)v,ZONE_KIND_CAMP,CAMP_STROKE)){
            have += ZoneCampFire(v) ? 0 : 1;
        }else if (at > 0){
            continue;   //not a camp plot: the flood goes on only through the camp
        }
        int nb[8];
        int n = ZonePlotNeighbours(w,v,nb,8);
        for (int j = 0; j < n; j++){
            if (!seen[nb[j]]){
                seen[nb[j]] = 1;
                queue.push_back(nb[j]);
            }
        }
    }
    state.stroke = 0;
    state.stroke_building = 0;
    last_refusal.clear();
}

void Zones::Reset(std::shared_ptr<const ChasmWorld> world){
    state = ZoneState();
    state.world = world;
    coarse_across.clear();
    if (!world){
        return;
    }
    const Grid& g = *world->grid;
    state.buildings.assign(1,ZoneBuilding());   //id 0: none
    state.building.assign(g.fine.pos.size(),0);
    state.storeys.assign(g.fine.pos.size(),0);
    state.standing.assign(g.fine.pos.size(),0);
    state.ground.assign(g.fine.pos.size(),ZONE_GROUND_NONE);
    state.ground_built.assign(g.fine.pos.size(),0);
    state.field.assign(g.coarse.quads.size(),0);
    state.chunk_version.assign(world->mesh->chunks.size(),0);
    last_refusal.clear();
    //The coarse quad across each coarse edge, for keeping a field in one piece: two quads share an
    //edge when they share both its corners.
    coarse_across.assign(g.coarse.quads.size() * 4,-1);
    std::vector<std::pair<uint64_t,int>> edges;
    edges.reserve(g.coarse.quads.size() * 4);
    for (int c = 0; c < (int)g.coarse.quads.size(); c++){
        for (int k = 0; k < 4; k++){
            uint32_t a = (uint32_t)g.coarse.quads[c].v[k];
            uint32_t b = (uint32_t)g.coarse.quads[c].v[(k + 1) % 4];
            if (a > b){
                std::swap(a,b);
            }
            edges.push_back(std::make_pair(((uint64_t)a << 32) | b,c * 4 + k));
        }
    }
    std::sort(edges.begin(),edges.end());
    for (size_t i = 0; i + 1 < edges.size(); i++){
        if (edges[i].first == edges[i + 1].first){
            coarse_across[edges[i].second] = edges[i + 1].second / 4;
            coarse_across[edges[i + 1].second] = edges[i].second / 4;
        }
    }
}

//A fine quad's look changed: its chunk has to be rebuilt.
void Zones::Touch(int fine_quad){
    int c = TerrainChunkOfQuad(*state.world->grid,*state.world->mesh,fine_quad);
    state.chunk_version[c]++;
}

uint32_t Zones::NewBuilding(int kind){
    ZoneBuilding b;
    b.kind = (uint8_t)kind;
    state.buildings.push_back(b);
    return (uint32_t)state.buildings.size() - 1;
}

/*
    After plot (or cell) `removed` left building `id`: if what is left of it is no longer one piece,
    the largest piece keeps the id and each other becomes a building of its own, of the same kind -
    ties going to the piece with the lowest index in it, and the new ids handed out in the order of
    each piece's lowest index, so the same erase always splits the same way. Only the removed one's
    old neighbours can start a piece, so only they are flooded from.
*/
void Zones::SplitAfterRemoval(uint32_t id, int removed, bool f_field){
    const ChasmWorld& w = *state.world;
    std::vector<uint32_t>& owner = f_field ? state.field : state.building;
    auto neighbours = [&](int at, int* out) -> int {
        if (f_field){
            int n = 0;
            for (int k = 0; k < 4; k++){
                int c = coarse_across[at * 4 + k];
                if (c >= 0){
                    out[n++] = c;
                }
            }
            return n;
        }
        return ZonePlotNeighbours(w,at,out,8);
    };
    int nb[8];
    int n = neighbours(removed,nb);
    std::vector<int> starts;
    for (int i = 0; i < n; i++){
        if (owner[nb[i]] == id){
            starts.push_back(nb[i]);
        }
    }
    if (starts.size() < 2){
        return;     //one neighbour or none: whatever is left is still in one piece
    }
    std::vector<std::vector<int>> pieces;
    std::vector<uint8_t> seen(owner.size(),0);
    for (int s : starts){
        if (seen[s]){
            continue;
        }
        std::vector<int> piece;
        std::vector<int> stack(1,s);
        seen[s] = 1;
        while (!stack.empty()){
            int at = stack.back();
            stack.pop_back();
            piece.push_back(at);
            int around[8];
            int m = neighbours(at,around);
            for (int i = 0; i < m; i++){
                int u = around[i];
                if (!seen[u] && owner[u] == id){
                    seen[u] = 1;
                    stack.push_back(u);
                }
            }
        }
        std::sort(piece.begin(),piece.end());
        pieces.push_back(piece);
    }
    if (pieces.size() < 2){
        return;
    }
    //By size, largest first, the lowest index breaking ties; the first keeps the id.
    std::sort(pieces.begin(),pieces.end(),[](const std::vector<int>& a, const std::vector<int>& b){
        return (a.size() != b.size()) ? a.size() > b.size() : a.front() < b.front();
    });
    std::sort(pieces.begin() + 1,pieces.end(),[](const std::vector<int>& a, const std::vector<int>& b){
        return a.front() < b.front();
    });
    const GridPicker& p = *w.picker;
    for (size_t k = 1; k < pieces.size(); k++){
        uint32_t fresh = NewBuilding(state.buildings[id].kind);
        state.buildings[fresh].crop = state.buildings[id].crop;
        state.buildings[fresh].allow = state.buildings[id].allow;
        state.buildings[fresh].split_from = id;
        for (int at : pieces[k]){
            owner[at] = fresh;
            //Its own look now - a roof colour of its own - so all of it is drawn again.
            if (f_field){
                for (int q = at * 4; q < at * 4 + 4; q++){
                    Touch(q);
                }
            }else{
                for (int i = 0; i < p.PlotQuadCount(at); i++){
                    Touch(p.PlotQuadCorner(at,i) / 4);
                }
            }
        }
        state.buildings[fresh].size = (int)pieces[k].size();
        state.buildings[id].size -= (int)pieces[k].size();
    }
}

bool Zones::Apply(int op, uint32_t index, int kind, uint32_t stroke, bool f_play){
    last_refusal.clear();
    if (!state.world){
        last_refusal = "no world";
        return false;
    }
    const ChasmWorld& w = *state.world;
    const GridPicker& p = *w.picker;
    const char* why = NULL;
    //The quads whose look a plot's change touches: every one around it.
    auto touch_plot = [&](int plot){
        for (int i = 0; i < p.PlotQuadCount(plot); i++){
            Touch(p.PlotQuadCorner(plot,i) / 4);
        }
    };
    //A field's fence runs along its outline, drawn from either side's chunk, so a field changing
    //touches every quad round its nine vertices - its neighbours' included.
    auto touch_field = [&](int c){
        for (int q = c * 4; q < c * 4 + 4; q++){
            for (int k = 0; k < 4; k++){
                touch_plot(w.grid->fine.quads[q].v[k]);
            }
        }
    };
    /*
        A road plot changing redraws its neighbours too - a road vertex is drawn by how many road edges
        meet there (RoadMesh.h), a gate by where the road ends, an arch by the buildings either side -
        and moves the forest's clearance a cell out: every quad round every plot that shares a cell
        with it. Any ground or building changing does the same: a garden's wall beside a road's end
        makes a gate, a building beside a road makes or unmakes an arch over it.
    */
    auto touch_ring = [&](int v){
        for (int i = 0; i < p.PlotQuadCount(v); i++){
            const GridQuad& q = w.grid->fine.quads[p.PlotQuadCorner(v,i) / 4];
            for (int k = 0; k < 4; k++){
                touch_plot(q.v[k]);
            }
        }
    };
    //One ring further again: whether a road plot is an arch depends on the run it is in (Zones.h), so
    //a building going up can unmake the arch two plots along.
    auto touch_road = [&](int v){
        for (int i = 0; i < p.PlotQuadCount(v); i++){
            const GridQuad& q = w.grid->fine.quads[p.PlotQuadCorner(v,i) / 4];
            for (int k = 0; k < 4; k++){
                touch_ring(q.v[k]);
            }
        }
    };
    //The building the stroke in progress is making, if this command continues it with a `kind`.
    auto stroke_target = [&](int want_kind) -> uint32_t {
        if (stroke == 0 || stroke != state.stroke || state.stroke_building == 0){
            return 0;
        }
        const ZoneBuilding& b = state.buildings[state.stroke_building];
        return (b.kind == want_kind && b.size > 0) ? state.stroke_building : 0;
    };
    auto plot_touches = [&](int plot, uint32_t id){
        int nb[8];
        int n = ZonePlotNeighbours(w,plot,nb,8);
        for (int i = 0; i < n; i++){
            if (state.building[nb[i]] == id){
                return true;
            }
        }
        return false;
    };
    auto cell_touches = [&](int c, uint32_t id){
        for (int k = 0; k < 4; k++){
            int o = coarse_across[c * 4 + k];
            if (o >= 0 && state.field[o] == id){
                return true;
            }
        }
        return false;
    };
    int plot = (int)index;
    switch (op){
        case ZONE_OP_BUILD_PAINT:
        case ZONE_OP_BUILD_ADD:{
            if (kind == ZONE_KIND_NONE){
                kind = ZONE_KIND_HOUSE;     //a recording from when every building was a house
            }
            if (kind < 0 || kind >= ZONE_KIND_COUNT || kind == ZONE_KIND_FIELD){
                last_refusal = "no such building";
                return false;
            }
            if (kind == ZONE_KIND_BRIDGE){
                last_refusal = "a bridge is drawn from bank to bank (ZONE_OP_BRIDGE)";
                return false;
            }
            if (plot < 0 || plot >= (int)state.storeys.size()){
                last_refusal = "no such plot";
                return false;
            }
            uint32_t on = state.building[plot];
            if (on){
                //A stroke starting on a building of its kind extends that building.
                if (stroke != state.stroke){
                    state.stroke = stroke;
                    state.stroke_building = (state.buildings[on].kind == kind) ? on : 0;
                }
                if (op == ZONE_OP_BUILD_PAINT){
                    return false;   //a drag does not stack - not a refusal, just nothing to do
                }
                int top = ZoneKindMaxStoreys(state.buildings[on].kind);
                uint8_t& s = state.storeys[plot];
                if (s >= top){
                    static char buf[96];
                    snprintf(buf,sizeof(buf),"at the most storeys a %s may have",ZoneKindName(state.buildings[on].kind));
                    last_refusal = buf;
                    return false;
                }
                s++;
                if (!f_play){
                    state.standing[plot] = s;   //a debug click builds it, and any site under it, at once
                }
                touch_road(plot);     //an arch over a road beside it may come or go
                break;
            }
            //An empty plot: on with the stroke's building where it is joined to it and has room, or a
            //new one - a stroke that jumps a gap, or has filled a house, goes on with the next.
            //A winch is a plot of its own: a stroke of them makes one each.
            uint32_t joining = (kind == ZONE_KIND_WINCH) ? 0 : stroke_target(kind);
            /*
                A drag across this grid steps diagonally as often as not - from a plot to the one across
                a cell, which shares no edge with it. Then the plot at the cell's corner between them
                joins first, so the building stays one piece: the first of the two that can be built.
            */
            int bridge = -1;
            if (joining && !plot_touches(plot,joining)){
                for (int i = 0; i < p.PlotQuadCount(plot) && bridge < 0; i++){
                    int qc = p.PlotQuadCorner(plot,i);
                    const GridQuad& q = w.grid->fine.quads[qc / 4];
                    int k = qc % 4;
                    if (state.building[q.v[(k + 2) % 4]] != joining){
                        continue;
                    }
                    int a = q.v[(k + 1) % 4];
                    int b = q.v[(k + 3) % 4];
                    int order[2] = {std::min(a,b),std::max(a,b)};
                    for (int c : order){
                        if (bridge < 0 && state.building[c] == 0 && ZoneCanBuild(w,state,c,kind,joining,NULL)){
                            bridge = c;
                        }
                    }
                }
            }
            int room = (bridge >= 0) ? 2 : 1;
            if (joining && ((!plot_touches(plot,joining) && bridge < 0) ||
                            (kind == ZONE_KIND_HOUSE && state.buildings[joining].size + room > ZONE_HOUSE_MAX_PLOTS))){
                joining = 0;
                bridge = -1;
            }
            if (bridge >= 0 && ZoneCanBuild(w,state,plot,kind,joining,NULL)){
                state.building[bridge] = joining;
                state.storeys[bridge] = 1;
                state.standing[bridge] = f_play ? 0 : 1;
                state.buildings[joining].size++;
                state.built_plots++;
                touch_road(bridge);
            }
            if (!ZoneCanBuild(w,state,plot,kind,joining,&why)){
                last_refusal = why;
                return false;
            }
            if (!joining){
                joining = NewBuilding(kind);
            }
            state.stroke = stroke;
            state.stroke_building = joining;
            state.building[plot] = joining;
            state.storeys[plot] = 1;
            state.standing[plot] = f_play ? 0 : 1;      //a player's: a site, until the builders raise it
            state.buildings[joining].size++;
            state.built_plots++;
            touch_road(plot);
            break;
        }
        case ZONE_OP_BUILD_REMOVE:
        case ZONE_OP_BUILD_ERASE:{
            if (plot < 0 || plot >= (int)state.storeys.size() || state.building[plot] == 0){
                last_refusal = "nothing built there";
                return false;
            }
            uint32_t id = state.building[plot];
            uint8_t& s = state.storeys[plot];
            if (op == ZONE_OP_BUILD_REMOVE && s > 1){
                s--;
                state.standing[plot] = std::min(state.standing[plot],s);    //the top comes off, built or not
                touch_road(plot);
                break;
            }
            s = 0;
            state.standing[plot] = 0;
            state.building[plot] = 0;
            state.buildings[id].size--;
            state.built_plots--;
            if (state.buildings[id].size == 0){
                state.buildings[id].kind = ZONE_KIND_NONE;    //gone; its id is never handed out again
            }else{
                SplitAfterRemoval(id,plot,false);
            }
            touch_road(plot);     //an arch over a road beside it may come or go
            break;
        }
        case ZONE_OP_FIELD_PAINT:{
            int c = (int)index;
            if (c >= 0 && c < (int)state.field.size() && state.field[c]){
                if (stroke != state.stroke){
                    state.stroke = stroke;
                    state.stroke_building = state.field[c];
                }
                return false;
            }
            uint32_t joining = stroke_target(ZONE_KIND_FIELD);
            //A diagonal step, as for buildings: through a cell beside both, the first that can be a field.
            if (joining && !cell_touches(c,joining)){
                int bridge = -1;
                for (int k = 0; k < 4 && bridge < 0; k++){
                    int n = coarse_across[c * 4 + k];
                    if (n >= 0 && !state.field[n] && cell_touches(n,joining) && ZoneCanField(w,state,n,NULL)){
                        bridge = n;
                    }
                }
                if (bridge >= 0 && ZoneCanField(w,state,c,NULL)){
                    state.field[bridge] = joining;
                    state.buildings[joining].size++;
                    state.field_cells++;
                    touch_field(bridge);
                }else{
                    joining = 0;
                }
            }
            if (!ZoneCanField(w,state,c,&why)){
                last_refusal = why;
                return false;
            }
            if (!joining){
                joining = NewBuilding(ZONE_KIND_FIELD);
            }
            state.stroke = stroke;
            state.stroke_building = joining;
            state.field[c] = joining;
            state.buildings[joining].size++;
            state.field_cells++;
            touch_field(c);
            break;
        }
        case ZONE_OP_FIELD_ERASE:{
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c]){
                last_refusal = "no field there";
                return false;
            }
            uint32_t id = state.field[c];
            state.field[c] = 0;
            state.buildings[id].size--;
            state.field_cells--;
            if (state.buildings[id].size == 0){
                state.buildings[id].kind = ZONE_KIND_NONE;
            }else{
                SplitAfterRemoval(id,c,true);
            }
            touch_field(c);
            break;
        }
        case ZONE_OP_FIELD_CROP:{
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c]){
                last_refusal = "no field there";
                return false;
            }
            if (kind < 0 || kind >= ZONE_CROP_COUNT){
                last_refusal = "no such crop";
                return false;
            }
            uint32_t id = state.field[c];
            if (state.buildings[id].crop == kind){
                return false;
            }
            state.buildings[id].crop = (uint8_t)kind;
            for (int o = 0; o < (int)state.field.size(); o++){
                if (state.field[o] == id){
                    for (int q = o * 4; q < o * 4 + 4; q++){
                        Touch(q);
                    }
                }
            }
            break;
        }
        case ZONE_OP_BRIDGE:{
            //Placed whole: the chain from this plot to the plot in `kind`, if it may stand there.
            std::vector<int> plots;
            if (!ZoneBridgeChain(w,state,plot,kind,plots,&why)){
                last_refusal = why;
                return false;
            }
            uint32_t id = NewBuilding(ZONE_KIND_BRIDGE);
            for (int v : plots){
                state.building[v] = id;
                state.storeys[v] = 1;
                state.standing[v] = f_play ? 0 : 1;     //a player's: built by carriers, from its banks
                state.buildings[id].size++;
                state.built_plots++;
                touch_road(v);
            }
            break;
        }
        case ZONE_OP_BUILD_RAISE:{
            //The builders' work: the next storey of a site stands. Only what was planned.
            if (plot < 0 || plot >= (int)state.storeys.size() || state.standing[plot] >= state.storeys[plot]){
                last_refusal = "nothing to build there";
                return false;
            }
            state.standing[plot]++;
            touch_road(plot);
            break;
        }
        case ZONE_OP_STORE_ALLOW:{
            //Changes nothing that is drawn, only what the store takes - so nothing is touched.
            int plot = (int)index;
            if (plot < 0 || plot >= (int)state.building.size() || state.KindOf(plot) != ZONE_KIND_STORE){
                last_refusal = "no store there";
                return false;
            }
            uint8_t mask = (uint8_t)(kind & GOODS_ALL);
            uint32_t id = state.building[plot];
            if (state.buildings[id].allow == mask){
                return false;
            }
            state.buildings[id].allow = mask;
            break;
        }
        case ZONE_OP_GROUND_PAINT:{
            if (kind <= ZONE_GROUND_NONE || kind >= ZONE_GROUND_COUNT){
                last_refusal = "no such ground";
                return false;
            }
            if (plot < 0 || plot >= (int)state.ground.size()){
                last_refusal = "no such plot";
                return false;
            }
            if (state.ground[plot] == kind){
                return false;   //already that - nothing to do
            }
            if (!ZoneCanGround(w,state,plot,&why,kind)){
                last_refusal = why;
                return false;
            }
            if (state.ground[plot] == ZONE_GROUND_NONE){
                state.grounds++;
            }
            state.ground[plot] = (uint8_t)kind;
            //A player's garden or lot is a site until its wood is brought; a road, or a debug paint, is there.
            state.ground_built[plot] = (f_play && ZoneEnclosesGround(kind)) ? 0 : 1;
            touch_road(plot);       //a road's ends, gates and the forest's clearance - see touch_road
            break;
        }
        case ZONE_OP_GROUND_ERASE:{
            if (plot < 0 || plot >= (int)state.ground.size() || state.ground[plot] == ZONE_GROUND_NONE){
                last_refusal = "no garden, lot or road there";
                return false;
            }
            state.ground[plot] = ZONE_GROUND_NONE;
            state.ground_built[plot] = 0;
            state.grounds--;
            touch_road(plot);
            break;
        }
        case ZONE_OP_GROUND_RAISE:{
            //The builders' work on a garden or lot: its wall goes up. Only one planned.
            if (plot < 0 || plot >= (int)state.ground.size() || !ZoneEnclosesGround(state.ground[plot])
                || state.ground_built[plot]){
                last_refusal = "nothing to build there";
                return false;
            }
            state.ground_built[plot] = 1;
            touch_road(plot);       //its wall, and a gate where a road meets it
            break;
        }
        default:
            last_refusal = "unknown zone op";
            return false;
    }
    state.version++;
    return true;
}

int Zones::Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<ZoneSavedBuilding>& buildings,
                   uint32_t next_id, const std::vector<std::pair<int,int>>& grounds,
                   uint32_t stroke, uint32_t stroke_building){
    Reset(world);
    if (!world){
        return 0;
    }
    const ChasmWorld& w = *world;
    int refused = 0;
    //The table first, every id where it was - gaps where buildings were erased stay gaps.
    uint32_t top = std::max<uint32_t>(next_id,1);
    for (const ZoneSavedBuilding& b : buildings){
        top = std::max(top,b.id + 1);
    }
    state.buildings.assign(top,ZoneBuilding());
    for (const ZoneSavedBuilding& b : buildings){
        if (b.id == 0 || b.kind <= ZONE_KIND_NONE || b.kind >= ZONE_KIND_COUNT){
            refused += (int)(b.plots.size() + b.cells.size());
            continue;
        }
        state.buildings[b.id].kind = (uint8_t)b.kind;
        state.buildings[b.id].crop = (uint8_t)std::max(0,std::min(ZONE_CROP_COUNT - 1,b.crop));
        state.buildings[b.id].allow = (uint8_t)(b.allow & GOODS_ALL);
    }
    //Then what stands on the ground, in the order the rules want: fields, ground, buildings - each
    //through the same questions a command asks.
    for (const ZoneSavedBuilding& b : buildings){
        if (b.kind != ZONE_KIND_FIELD || b.id == 0){
            continue;
        }
        for (int c : b.cells){
            if (c < 0 || c >= (int)state.field.size() || state.field[c] || !ZoneCanField(w,state,c,NULL)){
                refused++;
                continue;
            }
            state.field[c] = b.id;
            state.buildings[b.id].size++;
            state.field_cells++;
        }
    }
    for (const auto& gr : grounds){
        //A site goes back as a play paint would leave it.
        bool f_site = (gr.second & ZONE_GROUND_SAVED_SITE) != 0;
        if (!Apply(ZONE_OP_GROUND_PAINT,(uint32_t)gr.first,gr.second & ~ZONE_GROUND_SAVED_SITE,0,f_site)){
            refused++;
        }
    }
    for (const ZoneSavedBuilding& b : buildings){
        if (b.kind == ZONE_KIND_FIELD || b.id == 0 || state.buildings[b.id].kind == ZONE_KIND_NONE){
            continue;
        }
        for (size_t i = 0; i < b.plots.size(); i++){
            int v = b.plots[i].first;
            //A woodcutter is put back as a building already there, which the lot rule does not ask of:
            //it is a rule for placing one, and its lot may since have been erased.
            uint32_t join = (b.kind == ZONE_KIND_WOODCUTTER) ? b.id : 0;
            if (v < 0 || v >= (int)state.building.size() || state.building[v] ||
                !ZoneCanBuild(w,state,v,b.kind,join,NULL)){
                refused++;
                continue;
            }
            state.building[v] = b.id;
            state.storeys[v] = (uint8_t)std::max(1,std::min(ZoneKindMaxStoreys(b.kind),b.plots[i].second));
            //A save from before construction has no `standing`: everything in it stood.
            int st = (i < b.standing.size()) ? b.standing[i] : state.storeys[v];
            state.standing[v] = (uint8_t)std::max(0,std::min((int)state.storeys[v],st));
            state.buildings[b.id].size++;
            state.built_plots++;
        }
    }
    for (ZoneBuilding& b : state.buildings){
        if (b.size == 0){
            b.kind = ZONE_KIND_NONE;
        }
    }
    state.stroke = stroke;
    state.stroke_building = (stroke_building < state.buildings.size() && state.buildings[stroke_building].size > 0)
                            ? stroke_building : 0;
    //Everything is new: every chunk is drawn again.
    for (uint32_t& v : state.chunk_version){
        v++;
    }
    state.version++;
    last_refusal.clear();
    return refused;
}

std::vector<ZoneSavedBuilding> ZoneSaveBuildings(const ZoneState& state){
    std::vector<ZoneSavedBuilding> out;
    std::vector<int> slot(state.buildings.size(),-1);
    for (uint32_t id = 1; id < state.buildings.size(); id++){
        if (state.buildings[id].size > 0){
            slot[id] = (int)out.size();
            ZoneSavedBuilding b;
            b.id = id;
            b.kind = state.buildings[id].kind;
            b.crop = state.buildings[id].crop;
            b.allow = state.buildings[id].allow;
            out.push_back(b);
        }
    }
    for (size_t v = 0; v < state.building.size(); v++){
        uint32_t id = state.building[v];
        if (id && slot[id] >= 0){
            out[slot[id]].plots.push_back(std::make_pair((int)v,(int)state.storeys[v]));
            out[slot[id]].standing.push_back((int)state.standing[v]);
        }
    }
    for (size_t c = 0; c < state.field.size(); c++){
        uint32_t id = state.field[c];
        if (id && slot[id] >= 0){
            out[slot[id]].cells.push_back((int)c);
        }
    }
    return out;
}

ZoneStats ComputeZoneStats(const ChasmWorld& w, const ZoneState& z){
    ZoneStats s;
    for (size_t id = 1; id < z.buildings.size(); id++){
        if (z.buildings[id].size > 0){
            s.buildings[z.buildings[id].kind]++;
        }
    }
    for (size_t v = 0; v < z.storeys.size(); v++){
        if (z.storeys[v]){
            s.built_plots++;
            s.storeys += z.standing[v];
            s.floor_area[z.KindOf((int)v)] += w.picker->PlotArea((int)v) * z.standing[v];
        }
    }
    for (size_t c = 0; c < z.field.size(); c++){
        if (z.field[c]){
            s.field_cells++;
            s.floor_area[ZONE_KIND_FIELD] += w.picker->CoarseArea((int)c);
        }
    }
    for (size_t v = 0; v < z.ground.size(); v++){
        if (z.ground[v] == ZONE_GROUND_GARDEN){
            s.gardens++;
        }else if (z.ground[v] == ZONE_GROUND_LOT){
            s.lots++;
        }else if (z.ground[v] == ZONE_GROUND_ROAD){
            s.roads++;
        }
        if (ZoneEnclosesGround(z.ground[v])){
            s.ground_area += w.picker->PlotArea((int)v);
        }
    }
    return s;
}

ZoneBuildingInfo ZoneBuildingFigures(const ChasmWorld& w, const ZoneState& z, uint32_t id){
    ZoneBuildingInfo info;
    if (id == 0 || id >= z.buildings.size() || z.buildings[id].size == 0){
        return info;
    }
    info.id = id;
    info.kind = z.buildings[id].kind;
    info.crop = z.buildings[id].crop;
    info.allow = z.buildings[id].allow;
    info.size = z.buildings[id].size;
    if (info.kind == ZONE_KIND_FIELD){
        for (size_t c = 0; c < z.field.size(); c++){
            if (z.field[c] == id){
                info.floor_area += w.picker->CoarseArea((int)c);
            }
        }
        return info;
    }
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id){
            int site = z.storeys[v] - z.standing[v];
            info.storeys += z.standing[v];
            info.floor_area += w.picker->PlotArea((int)v) * z.standing[v];
            info.site_storeys += site;
            info.site_area += w.picker->PlotArea((int)v) * site;
        }
    }
    return info;
}

#ifdef DEBUG
void RunZoneChecks(const ChasmWorld& w, const ZoneState& z, GridCheckReport& report){
    char buf[256];
    GridCheckResult r;
    r.name = "zones";
    if (z.world.get() != &w){
        r.f_skipped = true;
        r.detail = "nothing painted on this map yet";
        report.results.push_back(r);
        return;
    }
    auto issue = [&](const char* kind, int index, vec2 where){
        if (report.issues.size() < 200){
            GridIssue i;
            i.kind = kind;
            i.index = index;
            i.where = where;
            report.issues.push_back(i);
        }
    };
    //Every building and field must still be one its command would have allowed. Each is asked with
    //itself taken out, since a building never forbids itself.
    int bad = 0;
    ZoneState probe = z;
    for (size_t v = 0; v < z.storeys.size(); v++){
        bool f_built = z.building[v] != 0;
        if (f_built != (z.storeys[v] > 0) || z.standing[v] > z.storeys[v]){
            bad++;
            issue("zone building",(int)v,w.grid->fine.pos[v]);
            continue;
        }
        if (!f_built){
            continue;
        }
        int kind = z.KindOf((int)v);
        probe.storeys[v] = 0;
        probe.building[v] = 0;
        const char* why = NULL;
        //A woodcutter as one already standing: the lot rule is for placing one (Zones.h).
        uint32_t join = (kind == ZONE_KIND_WOODCUTTER) ? z.building[v] : 0;
        if (z.storeys[v] > ZoneKindMaxStoreys(kind) || !ZoneCanBuild(w,probe,(int)v,kind,join,&why)){
            bad++;
            issue("zone building",(int)v,w.grid->fine.pos[v]);
        }
        probe.storeys[v] = z.storeys[v];
        probe.building[v] = z.building[v];
    }
    for (size_t v = 0; v < z.ground.size(); v++){
        if (!z.ground[v]){
            continue;
        }
        probe.ground[v] = ZONE_GROUND_NONE;
        if (z.ground[v] >= ZONE_GROUND_COUNT || !ZoneCanGround(w,probe,(int)v,NULL,z.ground[v])){
            bad++;
            issue("zone ground",(int)v,w.grid->fine.pos[v]);
        }
        probe.ground[v] = z.ground[v];
    }
    for (size_t c = 0; c < z.field.size(); c++){
        if (!z.field[c]){
            continue;
        }
        probe.field[c] = 0;
        if (!ZoneCanField(w,probe,(int)c,NULL)){
            bad++;
            issue("zone field",(int)c,w.grid->FineQuadCentre((int)c * 4 + 2));
        }
        probe.field[c] = z.field[c];
    }

    /*
        The buildings themselves: each id's count matches what names it, everything naming it is of
        one kind on one side - plots for buildings, cells for fields - every building is ONE piece,
        and a house is within its plots.
    */
    std::vector<int> counted(z.buildings.size(),0);
    int broken = 0;
    for (size_t v = 0; v < z.building.size(); v++){
        uint32_t id = z.building[v];
        if (id >= z.buildings.size() || (id && z.buildings[id].kind == ZONE_KIND_FIELD)){
            broken++;
            issue("zone building",(int)v,w.grid->fine.pos[v]);
        }else if (id){
            counted[id]++;
        }
    }
    for (size_t c = 0; c < z.field.size(); c++){
        uint32_t id = z.field[c];
        if (id >= z.buildings.size() || (id && z.buildings[id].kind != ZONE_KIND_FIELD)){
            broken++;
            issue("zone field",(int)c,w.grid->FineQuadCentre((int)c * 4 + 2));
        }else if (id){
            counted[id]++;
        }
    }
    int live = 0;
    int in_pieces = 0;
    int big_houses = 0;
    std::vector<uint8_t> seen;
    for (uint32_t id = 1; id < z.buildings.size(); id++){
        const ZoneBuilding& b = z.buildings[id];
        if (counted[id] != b.size || (b.size == 0) != (b.kind == ZONE_KIND_NONE)){
            broken++;
            continue;
        }
        if (b.size == 0){
            continue;
        }
        live++;
        if (b.kind == ZONE_KIND_HOUSE && b.size > ZONE_HOUSE_MAX_PLOTS){
            big_houses++;
        }
        //One piece: a flood from its first element reaches all of it.
        bool f_field = (b.kind == ZONE_KIND_FIELD);
        const std::vector<uint32_t>& owner = f_field ? z.field : z.building;
        int first = -1;
        for (size_t i = 0; i < owner.size() && first < 0; i++){
            if (owner[i] == id){
                first = (int)i;
            }
        }
        seen.assign(owner.size(),0);
        std::vector<int> stack(1,first);
        seen[first] = 1;
        int reached = 0;
        while (!stack.empty()){
            int at = stack.back();
            stack.pop_back();
            reached++;
            int nb[8];
            int n = 0;
            if (f_field){
                const GridQuad& q = w.grid->coarse.quads[at];
                //Coarse neighbours: the quads sharing an edge, found through the corners.
                for (int k = 0; k < 4 && n < 8; k++){
                    int a = q.v[k];
                    int b2 = q.v[(k + 1) % 4];
                    for (size_t o = 0; o < owner.size() && n < 8; o++){
                        if (owner[o] != id || (int)o == at){
                            continue;
                        }
                        const GridQuad& oq = w.grid->coarse.quads[o];
                        bool fa = false, fb = false;
                        for (int j = 0; j < 4; j++){
                            fa = fa || oq.v[j] == a;
                            fb = fb || oq.v[j] == b2;
                        }
                        if (fa && fb){
                            nb[n++] = (int)o;
                        }
                    }
                }
            }else{
                n = ZonePlotNeighbours(w,at,nb,8);
            }
            for (int i = 0; i < n; i++){
                if (!seen[nb[i]] && owner[nb[i]] == id){
                    seen[nb[i]] = 1;
                    stack.push_back(nb[i]);
                }
            }
        }
        if (reached != b.size){
            in_pieces++;
            issue(f_field ? "zone field" : "zone building",first,
                  f_field ? w.grid->FineQuadCentre(first * 4 + 2) : w.grid->fine.pos[first]);
        }
    }
    snprintf(buf,sizeof(buf),"%i buildings and fields on %i plots and %i cells, %i ground plots; %i break a rule, "
             "%i miscounted, %i in pieces, %i houses too big",live,z.built_plots,z.field_cells,z.grounds,bad,broken,
             in_pieces,big_houses);
    r.f_pass = (bad == 0 && broken == 0 && in_pieces == 0 && big_houses == 0);
    r.detail = buf;
    report.results.push_back(r);
    if (!r.f_pass){
        report.f_pass = false;
    }
}
#endif

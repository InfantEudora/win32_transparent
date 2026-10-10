#include "Zones.h"
#include "ChasmWorld.h"
#include "Walkers.h"

#include <algorithm>
#include <climits>
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
        case ZONE_OP_SITE_PRIORITY: return "site_priority";
        case ZONE_OP_FIELD_NEXT:    return "field_next";
        case ZONE_OP_FIELD_RAISE:   return "field_raise";
        case ZONE_OP_FIELD_LAND:    return "field_land";
        case ZONE_OP_FIELD_PRIORITY: return "field_priority";
        case ZONE_OP_GATE_SHUT:     return "gate_shut";
        case ZONE_OP_RANGE_PAINT:   return "range_paint";
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
        case ZONE_KIND_BUILDERS:    return "builders";
        case ZONE_KIND_RANGE:       return "range";
        case ZONE_KIND_TOWER:       return "tower";
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
        case ZONE_KIND_BUILDERS:    return 2;       //a workshop, like the woodcutter's
        case ZONE_KIND_TOWER:       return 2;       //and never less: painted whole (ZoneKindPaintStoreys)
        default:                    return 0;
    }
}

const char* ZoneCropName(int crop){
    switch (crop){
        case ZONE_CROP_WHEAT:       return "wheat";
        case ZONE_CROP_GREENS:      return "greens";
        case ZONE_CROP_BEANS:       return "beans";
        case ZONE_CROP_AUTO:        return "auto";
        default:                    return "none";
    }
}

int ZoneCropByName(const std::string& name){
    for (int c = 0; c < ZONE_CROP_COUNT; c++){
        if (name == ZoneCropName(c)){
            return c;
        }
    }
    if (name == "auto"){
        return ZONE_CROP_AUTO;
    }
    return (name == "none") ? ZONE_CROP_NONE : -1;
}

const char* ZoneGroundName(int ground){
    switch (ground){
        case ZONE_GROUND_GARDEN:    return "garden";
        case ZONE_GROUND_LOT:       return "lot";
        case ZONE_GROUND_ROAD:      return "road";
        case ZONE_GROUND_WALL:      return "wall";
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
    //A planned road opens nothing yet, nor counts as the road going on (line_works_plan.md).
    if (!ZoneRoadLaid(z,road)){
        return -1;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,road,nb,8);
    int from = -1;
    for (int i = 0; i < n; i++){
        if (ZoneRoadLaid(z,nb[i])){
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
    if (!ZoneRoadLaid(z,plot)){
        return 0;       //a planned road is bridged once it is laid
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    int roads = 0;
    int top = ZONE_MAX_STOREYS;
    for (int i = 0; i < n; i++){
        int u = nb[i];
        if (ZoneRoadLaid(z,u)){
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

ZoneRangeCell ZoneRangeCellAt(const ChasmWorld& w, int cell){
    ZoneRangeCell r;
    const Grid& g = *w.grid;
    if (cell < 0 || cell * 4 + 3 >= (int)g.fine.quads.size()){
        return r;
    }
    //The middle is the one vertex all four of its children share - found, as Economy's CoarseCentre does,
    //rather than trusted to a corner's place in the quad.
    r.middle = g.fine.quads[cell * 4].v[2];
    for (int k = 0; k < 4; k++){
        int v = g.fine.quads[cell * 4].v[k];
        bool f_all = true;
        for (int q = cell * 4 + 1; q < cell * 4 + 4 && f_all; q++){
            const GridQuad& fq = g.fine.quads[q];
            f_all = (fq.v[0] == v || fq.v[1] == v || fq.v[2] == v || fq.v[3] == v);
        }
        if (f_all){
            r.middle = v;
            break;
        }
    }
    r.centre = g.fine.pos[r.middle];
    r.dir = vec2(0.0f,-1.0f);
    r.size = std::sqrt(std::max(0.0f,w.picker->CoarseArea(cell)));
    return r;
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
    if (kind <= ZONE_KIND_NONE || kind >= ZONE_KIND_COUNT || ZoneKindOnCells(kind)){
        return refuse("no such building");
    }
    //A guard tower may take a wall's plot (defence_plan.md): it stands in the wall's line.
    bool f_line_ok = kind == ZONE_KIND_TOWER && !z.ground.empty() && plot >= 0 && plot < (int)z.ground.size()
                     && z.ground[plot] == ZONE_GROUND_WALL;
    if (kind == ZONE_KIND_TOWER && joining){
        return refuse("a tower is one plot");
    }
    if (!f_line_ok && !z.ground.empty() && plot >= 0 && plot < (int)z.ground.size() && ZoneLineGround(z.ground[plot])){
        return refuse((z.ground[plot] == ZONE_GROUND_WALL) ? "a wall is there" : "a road is there");
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
    if (!f_line_ok && !z.ground.empty() && ZoneLineGround(z.ground[plot])){
        return refuse((z.ground[plot] == ZONE_GROUND_WALL) ? "a wall is there" : "a road is there");
    }
    if (joining && kind == ZONE_KIND_HOUSE && joining < z.buildings.size() &&
        z.buildings[joining].size >= ZONE_HOUSE_MAX_PLOTS){
        return refuse("a house is at most four plots - one family");
    }
    //A woodcutter needs no lot to be built (yards_plan.md, the user's 2026-10-09): without one of his own he
    //fells nothing, and his card and NEEDS ATTENTION say so - as a house shut in on every side does.
    return true;
}

int ZoneJoinCorner(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t joining){
    //A drag across this grid steps diagonally as often as not - from a plot to the one across a cell, which
    //shares no edge with it. Of the cell's two other corners, the first that can be built.
    const GridPicker& p = *w.picker;
    int bridge = -1;
    for (int i = 0; i < p.PlotQuadCount(plot) && bridge < 0; i++){
        int qc = p.PlotQuadCorner(plot,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        if (z.building[q.v[(k + 2) % 4]] != joining){
            continue;
        }
        int a = q.v[(k + 1) % 4];
        int b = q.v[(k + 3) % 4];
        int order[2] = {std::min(a,b),std::max(a,b)};
        for (int c : order){
            if (bridge < 0 && z.building[c] == 0 && ZoneCanBuild(w,z,c,kind,joining,NULL)){
                bridge = c;
            }
        }
    }
    return bridge;
}

bool ZoneCanExtend(const ChasmWorld& w, const ZoneState& z, int plot, int kind, uint32_t owner, const char** why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (plot < 0 || plot >= (int)z.building.size()){
        return refuse("no such plot");
    }
    if (!owner || z.building[plot]){
        return true;
    }
    if (owner >= z.buildings.size() || z.buildings[owner].size <= 0){
        return refuse("that building is gone");
    }
    if (z.buildings[owner].kind != kind){
        return refuse("that building is of another kind");
    }
    //Full is a refusal here, where a stroke of its own would go on with a new house - and the reason that
    //matters wherever the plot is.
    int most = (kind == ZONE_KIND_HOUSE) ? ZONE_HOUSE_MAX_PLOTS : INT_MAX;
    if (z.buildings[owner].size >= most){
        return refuse("a house is at most four plots - one family");
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    bool f_beside = false;
    for (int i = 0; i < n; i++){
        f_beside = f_beside || z.building[nb[i]] == owner;
    }
    if (!f_beside){
        if (ZoneJoinCorner(w,z,plot,kind,owner) < 0){
            return refuse("an extension goes beside its building");
        }
        //The corner plot between comes with it: two more.
        if (z.buildings[owner].size + 2 > most){
            return refuse("a house is at most four plots - one family");
        }
    }
    return true;
}

bool ZoneOwnsGround(int kind, int ground){
    return (kind == ZONE_KIND_HOUSE && ZoneEnclosesGround(ground)) || (kind == ZONE_KIND_WOODCUTTER && ground == ZONE_GROUND_LOT);
}

uint32_t ZoneGroundOwner(const ZoneState& z, int v){
    if (v < 0 || v >= (int)z.ground_owner.size()){
        return 0;
    }
    uint32_t id = z.ground_owner[v];
    bool f_alive = id && id < z.buildings.size() && z.buildings[id].size > 0;
    return (f_alive && ZoneOwnsGround(z.buildings[id].kind,z.ground[v])) ? id : 0;
}

namespace {

//Plot `plot` shares an edge with building `owner` or with ground of its yard.
bool TouchesYard(const ChasmWorld& w, const ZoneState& z, int plot, uint32_t owner){
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    for (int i = 0; i < n; i++){
        if (z.building[nb[i]] == owner || ZoneGroundOwner(z,nb[i]) == owner){
            return true;
        }
    }
    return false;
}

}

bool ZoneCanYard(const ChasmWorld& w, const ZoneState& z, int plot, int ground, uint32_t owner, const char** why){
    auto refuse = [&](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (!owner || !ZoneEnclosesGround(ground)){
        return true;
    }
    if (owner >= z.buildings.size() || z.buildings[owner].size <= 0){
        return refuse("that building is gone");
    }
    int kind = z.buildings[owner].kind;
    if (!ZoneOwnsGround(kind,ground)){
        return refuse((kind == ZONE_KIND_WOODCUTTER) ? "a woodcutter's hut has no garden"
                                                     : "only a house or a woodcutter's hut has a yard");
    }
    //Another building's yard stays its own: erase it first to give it away.
    uint32_t held = ZoneEnclosesGround(z.ground[plot]) ? ZoneGroundOwner(z,plot) : 0;
    if (held && held != owner){
        return refuse("that is another building's yard");
    }
    if (!TouchesYard(w,z,plot,owner)){
        return refuse((ground == ZONE_GROUND_LOT) ? "a lot goes beside its building, or its yard"
                                                  : "a garden goes beside its house, or its yard");
    }
    return true;
}

int ZoneYardPlots(const ZoneState& z, uint32_t id, int ground){
    int n = 0;
    for (size_t v = 0; v < z.ground_owner.size(); v++){
        n += (z.ground_owner[v] == id && z.ground[v] == ground && ZoneGroundOwner(z,(int)v) == id) ? 1 : 0;
    }
    return n;
}

uint32_t ZoneAutoOwner(const ChasmWorld& w, const ZoneState& z, int plot, int ground){
    if (!ZoneEnclosesGround(ground)){
        return 0;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    //A building it touches first - so a lot beside a hut is the hut's even where a house's garden adjoins it -
    //then a yard it grows from.
    for (int pass = 0; pass < 2; pass++){
        uint32_t best = 0;
        for (int i = 0; i < n; i++){
            uint32_t b = pass ? ZoneGroundOwner(z,nb[i]) : z.building[nb[i]];
            if (b && ZoneOwnsGround(z.buildings[b].kind,ground) && (!best || b < best)){
                best = b;
            }
        }
        if (best){
            return best;
        }
    }
    return 0;
}

bool ZoneCanGround(const ChasmWorld& w, const ZoneState& z, int plot, const char** why, int kind){
    //A wall goes where a road may (the user, 2026-10-08): a line, on a road's slope, beside the wet.
    bool f_line = ZoneLineGround(kind);
    if (!PlotIsBuildable(w,z,plot,why,f_line ? ZONE_RISE_ROAD : ZONE_RISE_HOUSE,false,f_line)){
        return false;
    }
    if (f_line && !z.storeys.empty() && z.storeys[plot] > 0){
        if (why){
            *why = "a building is there";
        }
        return false;
    }
    /*
        A road stops at a garden's or a lot's edge rather than eating into it - so a road dragged into
        a walled garden ends against the wall, which is exactly where a gate opens (Zones.h). Erase the
        ground first to run a road through. A wall stops there too.
    */
    if (f_line && !z.ground.empty() && ZoneEnclosesGround(z.ground[plot])){
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

#define ZONE_CAMP_RIM       27.0f   //no nearer the rim than this: about 30 inland (the user's)
#define ZONE_CAMP_STEP      16.0f   //along a river's course, between the spots looked at
#define ZONE_CAMP_BACK      8.0f    //back from the water's edge, on either bank: the first row of spots...
#define ZONE_CAMP_ROW       8.0f    //...and every this much farther back, out to ZONE_CAMP_SURVEY
#define ZONE_CAMP_REACH     64.0f   //...looked for no farther than this from the centre line
#define ZONE_CAMP_NUDGE     8.0f    //how far a refused spot may move to a plot that passes
#define ZONE_CAMP_TREES     0.5f    //the best share of a survey's plots with a tree on (the user's)
#define ZONE_CAMP_W_FLAT    1.0f
#define ZONE_CAMP_W_TREES   1.0f
#define ZONE_CAMP_W_MIDDLE  0.25f

/*
    WHERE A NEW COLONY CAMPS (the user, 2026-10-08): by a river, on the best ground along it. Every map has a
    river at home (rivers_plan.md), so the spots looked at are along the rivers - every ZONE_CAMP_STEP of each
    one's course, branches and streams too, on both banks, in rows from ZONE_CAMP_BACK back from the water: not
    only the first, as the relief climbs back from a river's flat over RELIEF_RIVER_FADE, steepest just there
    (seed 21 had no room for a camp along any of its rivers in that row) - and each is scored on its SURVEY,
    the ground a walk from it reaches within ZONE_CAMP_SURVEY:
      - FLAT: the share of the survey a house may stand on, and the share a field may lie on - out of what a
        whole disc that size holds, so a river, a cliff or the map's edge cutting into it counts against it.
      - TREES: best with ZONE_CAMP_TREES of the survey's plots wooded, falling to nothing at none and at all of
        them: a forest's edge, with wood to fell and open ground to build on.
      - MIDDLE, a little: level with the map's middle rather than up by the mountain or down by the swamp.
    A spot is refused where the camp would not fit (PlaceStartCamp's flood, by a house's rules - a hill
    camped nothing, 2026-10-08), with no water collector's spot in its survey, nearer the rim than
    ZONE_CAMP_RIM, off the plateau or in the swamp; a refused spot gives way to the nearest plot within
    ZONE_CAMP_NUDGE that passes. The best score is the camp - the balcony plays no part, a winch being
    something for a couple of years in. With no spot by a river passing, the camp goes to the plot nearest the
    home side's middle that does, water or not. The weights are tuning, not rules.
*/
void ZoneCampSpots(const ChasmWorld& w, int tents, std::vector<ZoneCampSpot>& out){
    out.clear();
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& p = *w.picker;
    const int n = (int)g.fine.pos.size();
    const ZoneState none;           //a new map's: nothing built yet
    std::vector<uint8_t> wooded(n,0);
    if (w.forest){
        for (const PropInstance& pr : w.forest->props){
            if (pr.kind <= PROP_SNOW_PINE && pr.plot >= 0 && pr.plot < n){
                wooded[pr.plot] = 1;
            }
        }
    }
    auto house = [&](int v){
        return t.level[v] == TERRAIN_PLATEAU && PlotIsBuildable(w,none,v,NULL);
    };
    //A step a walker could take (Walkers::EdgeOpen, on a new map): no cliff, no water, no mountain, not too steep.
    auto walk = [&](int a, int b){
        float run = (g.fine.pos[b] - g.fine.pos[a]).length();
        return t.SameGround(a,b) && !t.wet[b] && !t.Mountain(b) && std::fabs(t.ground[b] - t.ground[a]) <= WALKER_STEEPEST * run;
    };
    //Flood marks, a new number per flood, so nothing is cleared between them.
    std::vector<int> camp_mark(n,0), mark(n,0), cell_mark(g.coarse.quads.size(),0);
    int marks = 0;
    std::vector<int> camp_queue, queue, cells;
    //The camp fits from v: as PlaceStartCamp lays it, a flood through the plots a tent or fire may take, to `tents` tents.
    auto fits = [&](int v){
        if (!house(v)){
            return false;
        }
        int m = ++marks;
        camp_queue.assign(1,v);
        camp_mark[v] = m;
        int have = 0;
        for (size_t at = 0; at < camp_queue.size() && have < tents; at++){
            have += ZoneCampFire(camp_queue[at]) ? 0 : 1;
            int nb[8];
            int k = ZonePlotNeighbours(w,camp_queue[at],nb,8);
            for (int j = 0; j < k; j++){
                if (camp_mark[nb[j]] != m && house(nb[j])){
                    camp_mark[nb[j]] = m;
                    camp_queue.push_back(nb[j]);
                }
            }
        }
        return have >= tents;
    };
    auto refusal = [&](int v) -> const char*{
        if (t.level[v] != TERRAIN_PLATEAU || t.Mountain(v)){
            return "not on the plateau";
        }
        if (t.biome[v] == TERRAIN_BIOME_SWAMP){
            return "in the swamp";
        }
        if (ChasmRiftDistance(g.layout,g.fine.pos[v]) < ZONE_CAMP_RIM){
            return "too near the rim";
        }
        if (!fits(v)){
            return "no room for the camp";
        }
        return NULL;
    };
    //The nearest plot to `from` within ZONE_CAMP_NUDGE that passes, the lower index on a tie; -1 if none.
    std::vector<std::pair<float,int>> near;
    auto nudge = [&](int from){
        int m = ++marks;
        const vec2 at = g.fine.pos[from];
        queue.assign(1,from);
        mark[from] = m;
        near.clear();
        for (size_t i = 0; i < queue.size(); i++){
            near.push_back({(g.fine.pos[queue[i]] - at).length(),queue[i]});
            int nb[8];
            int k = ZonePlotNeighbours(w,queue[i],nb,8);
            for (int j = 0; j < k; j++){
                if (mark[nb[j]] != m && (g.fine.pos[nb[j]] - at).length() <= ZONE_CAMP_NUDGE){
                    mark[nb[j]] = m;
                    queue.push_back(nb[j]);
                }
            }
        }
        std::sort(near.begin(),near.end());
        for (const auto& c : near){
            if (!refusal(c.second)){
                return c.second;
            }
        }
        return -1;
    };
    const float area = (g.bounds_max.x - g.bounds_min.x) * (g.bounds_max.y - g.bounds_min.y);
    const float disc = 3.14159265f * ZONE_CAMP_SURVEY * ZONE_CAMP_SURVEY;
    const float disc_plots = disc * (float)n / area;
    const float disc_cells = disc * (float)g.coarse.quads.size() / area;
    const float middle = (g.bounds_min.y + g.bounds_max.y) * 0.5f;
    const float half_depth = (g.bounds_max.y - g.bounds_min.y) * 0.5f;
    auto survey = [&](ZoneCampSpot& s){
        int m = ++marks;
        const vec2 at = g.fine.pos[s.plot];
        queue.assign(1,s.plot);
        mark[s.plot] = m;
        cells.clear();
        int houses = 0, trees = 0, fields = 0;
        bool f_well = false;
        for (size_t i = 0; i < queue.size(); i++){
            int v = queue[i];
            houses += house(v) ? 1 : 0;
            trees += wooded[v];
            f_well = f_well || PlotIsBuildable(w,none,v,NULL,ZONE_RISE_ROAD,true);
            for (int k = 0; k < p.PlotQuadCount(v); k++){
                int c = g.fine.quads[p.PlotQuadCorner(v,k) / 4].parent;
                if (c >= 0 && cell_mark[c] != m){
                    cell_mark[c] = m;
                    cells.push_back(c);
                }
            }
            int nb[8];
            int k = ZonePlotNeighbours(w,v,nb,8);
            for (int j = 0; j < k; j++){
                if (mark[nb[j]] != m && (g.fine.pos[nb[j]] - at).length() <= ZONE_CAMP_SURVEY && walk(v,nb[j])){
                    mark[nb[j]] = m;
                    queue.push_back(nb[j]);
                }
            }
        }
        //A field's cell counts when all nine of its vertices are in the survey.
        for (int c : cells){
            bool f_in = true;
            for (int q = c * 4; q < c * 4 + 4 && f_in; q++){
                for (int k = 0; k < 4; k++){
                    f_in = f_in && mark[g.fine.quads[q].v[k]] == m;
                }
            }
            fields += (f_in && ZoneCanField(w,none,c,NULL)) ? 1 : 0;
        }
        s.house_share = std::min(1.0f,(float)houses / disc_plots);
        s.field_share = std::min(1.0f,(float)fields / disc_cells);
        s.tree_share = (float)trees / (float)queue.size();
        s.flat = (s.house_share + s.field_share) * 0.5f;
        s.trees = (s.tree_share < ZONE_CAMP_TREES) ? s.tree_share / ZONE_CAMP_TREES
                                                    : (1.0f - s.tree_share) / (1.0f - ZONE_CAMP_TREES);
        s.middle = std::max(0.0f,1.0f - std::fabs(at.y - middle) / half_depth);
        s.score = ZONE_CAMP_W_FLAT * s.flat + ZONE_CAMP_W_TREES * s.trees + ZONE_CAMP_W_MIDDLE * s.middle;
        if (!f_well){
            s.refused = "no water collector's spot near";
        }
    };
    std::vector<uint8_t> f_looked(n,0), f_taken(n,0);
    for (const TerrainRiver& r : t.rivers){
        float along = 0.0f;
        float next = ZONE_CAMP_STEP * 0.5f;
        for (size_t i = 0; i + 1 < r.points.size(); i++){
            vec2 d = r.points[i + 1] - r.points[i];
            float len = d.length();
            if (len < 1e-4f){
                continue;
            }
            vec2 dir = d / len;
            vec2 across(-dir.y,dir.x);
            for (; next <= along + len; next += ZONE_CAMP_STEP){
                vec2 c = r.points[i] + dir * (next - along);
                for (int side = -1; side <= 1; side += 2){
                    //Out from the centre line, a spot each row past ANY river's water, so one by a confluence is back from both.
                    float o = 0.0f;
                    for (float back = ZONE_CAMP_BACK; back <= ZONE_CAMP_SURVEY; back += ZONE_CAMP_ROW){
                        while (o < ZONE_CAMP_REACH && t.WaterEdgeDistance(c + across * (o * side)) < back){
                            o += 1.0f;
                        }
                        GridPick at = p.Pick(c + across * (o * side));
                        if (o >= ZONE_CAMP_REACH || !at.f_hit || f_looked[at.plot]){
                            continue;
                        }
                        f_looked[at.plot] = 1;
                        ZoneCampSpot s;
                        s.plot = at.plot;
                        s.refused = refusal(at.plot);
                        int v = s.refused ? nudge(at.plot) : at.plot;
                        if (v >= 0){
                            if (f_taken[v]){
                                continue;
                            }
                            f_taken[v] = 1;
                            s.plot = v;
                            s.refused = NULL;
                            survey(s);
                        }
                        out.push_back(s);
                    }
                }
            }
            along += len;
        }
    }
    for (const ZoneCampSpot& s : out){
        if (!s.refused){
            return;
        }
    }
    //None by a river: the plot nearest the home side's middle that passes, its water a walk away.
    vec2 target((g.bounds_min.x + g.bounds_max.x) * 0.5f + (g.bounds_max.x - g.bounds_min.x) * 0.25f,middle);
    near.clear();
    for (int v = 0; v < n; v++){
        near.push_back({(g.fine.pos[v] - target).length(),v});
    }
    std::sort(near.begin(),near.end());
    for (const auto& c : near){
        if (!refusal(c.second)){
            ZoneCampSpot s;
            s.plot = c.second;
            s.f_fallback = true;
            survey(s);
            s.refused = NULL;
            out.push_back(s);
            return;
        }
    }
}

int ZoneStartCampPlot(const ChasmWorld& w, int tents){
    std::vector<ZoneCampSpot> spots;
    ZoneCampSpots(w,tents,spots);
    int best = -1;
    for (int i = 0; i < (int)spots.size(); i++){
        if (!spots[i].refused && (best < 0 || spots[i].score > spots[best].score)){
            best = i;
        }
    }
    return (best >= 0) ? spots[best].plot : -1;
}

void Zones::PlaceStartCamp(int tents){
    if (!state.world){
        return;
    }
    const ChasmWorld& w = *state.world;
    int start = ZoneStartCampPlot(w,tents);
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
    state.line.assign(g.fine.pos.size(),0);
    state.gate.assign(g.fine.pos.size(),0);
    state.ground_owner.assign(g.fine.pos.size(),0);
    state.field.assign(g.coarse.quads.size(),0);
    state.field_built.assign(g.coarse.quads.size(),0);
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
        state.buildings[fresh].f_priority = state.buildings[id].f_priority;
        state.buildings[fresh].next_crop = state.buildings[id].next_crop;
        state.buildings[fresh].land_of = state.buildings[id].land_of;      //a split field's pieces stay that house's
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

bool Zones::Apply(int op, uint32_t index, int kind, uint32_t stroke, bool f_play, uint32_t owner){
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
    /*
        A building plot just put down takes the ground nobody owns beside it that its kind may own, and all of
        that ground joined to it (yards_plan.md) - so a lot painted first and a hut put beside it after is the
        hut's, every plot of it.
    */
    auto claim_yard = [&](int v, uint32_t id){
        if (!ZoneKindHasYard(state.buildings[id].kind)){
            return;
        }
        std::vector<int> open(1,v);
        while (!open.empty()){
            int at = open.back();
            open.pop_back();
            int nb[8];
            int n = ZonePlotNeighbours(w,at,nb,8);
            for (int i = 0; i < n; i++){
                int o = nb[i];
                if (state.storeys[o] == 0 && ZoneOwnsGround(state.buildings[id].kind,state.ground[o])
                    && !ZoneGroundOwner(state,o)){
                    state.ground_owner[o] = id;
                    touch_plot(o);
                    open.push_back(o);
                }
            }
        }
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
            if (kind < 0 || kind >= ZONE_KIND_COUNT || ZoneKindOnCells(kind)){
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
            //A building named in the command is extended or nothing: a plot that cannot join it is refused.
            if (owner && !ZoneCanExtend(w,state,plot,kind,owner,&why)){
                last_refusal = why;
                return false;
            }
            uint32_t joining = owner ? owner : (kind == ZONE_KIND_WINCH || kind == ZONE_KIND_TOWER) ? 0 : stroke_target(kind);
            //A diagonal step brings the plot at the cell's corner between first, so the building stays one piece.
            int bridge = (joining && !plot_touches(plot,joining)) ? ZoneJoinCorner(w,state,plot,kind,joining) : -1;
            int room = (bridge >= 0) ? 2 : 1;
            if (!owner && joining && ((!plot_touches(plot,joining) && bridge < 0) ||
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
                claim_yard(bridge,joining);
            }
            if (!ZoneCanBuild(w,state,plot,kind,joining,&why)){
                last_refusal = why;
                return false;
            }
            if (!joining){
                joining = NewBuilding(kind);
            }
            //A guard tower in a wall's line takes the wall's plot (defence_plan.md): the palisade ends against it.
            if (kind == ZONE_KIND_TOWER && state.ground[plot] == ZONE_GROUND_WALL){
                state.ground[plot] = ZONE_GROUND_NONE;
                state.ground_built[plot] = 0;
                state.line[plot] = 0;
                state.grounds--;
            }
            state.stroke = stroke;
            state.stroke_building = joining;
            state.building[plot] = joining;
            state.storeys[plot] = (uint8_t)ZoneKindPaintStoreys(kind);
            state.standing[plot] = f_play ? 0 : state.storeys[plot];   //a player's: a site, until the builders raise it
            state.buildings[joining].size++;
            state.built_plots++;
            touch_road(plot);
            claim_yard(plot,joining);
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
            //A guard tower is only ever whole (ZoneKindPaintStoreys): a storey less takes it all.
            if (op == ZONE_OP_BUILD_REMOVE && s > 1 && state.buildings[id].kind != ZONE_KIND_TOWER){
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
                //...and its yard is nobody's, for the next building beside it to take (yards_plan.md).
                for (size_t v = 0; v < state.ground_owner.size(); v++){
                    if (state.ground_owner[v] == id){
                        state.ground_owner[v] = 0;
                        touch_plot((int)v);
                    }
                }
            }else{
                SplitAfterRemoval(id,plot,false);
            }
            touch_road(plot);     //an arch over a road beside it may come or go
            break;
        }
        case ZONE_OP_FIELD_PAINT:
        case ZONE_OP_RANGE_PAINT:{
            //A range is painted as a field is, on the same cells and by the same rules (range_plan.md).
            int cell_kind = (op == ZONE_OP_RANGE_PAINT) ? ZONE_KIND_RANGE : ZONE_KIND_FIELD;
            int c = (int)index;
            if (c >= 0 && c < (int)state.field.size() && state.field[c]){
                if (stroke != state.stroke){
                    state.stroke = stroke;
                    state.stroke_building = state.field[c];
                }
                return false;
            }
            uint32_t joining = stroke_target(cell_kind);
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
                    state.field_built[bridge] = f_play ? 0 : 1;
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
                //An EMPTY field (fields_plan.md): nothing grows on it until its starting crop is chosen.
                joining = NewBuilding(cell_kind);
                if (cell_kind == ZONE_KIND_FIELD){
                    state.buildings[joining].crop = ZONE_CROP_NONE;
                    state.buildings[joining].next_crop = ZONE_CROP_AUTO;
                }
            }
            state.stroke = stroke;
            state.stroke_building = joining;
            state.field[c] = joining;
            //Painted in play, a site to be built with wood; by debug, there at once.
            state.field_built[c] = f_play ? 0 : 1;
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
            state.field_built[c] = 0;
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
            if (c < 0 || c >= (int)state.field.size() || !state.field[c] || state.buildings[state.field[c]].kind != ZONE_KIND_FIELD){
                last_refusal = "no field there";
                return false;
            }
            //A crop, or AUTO: the player's, or the economy's once a field has waited too long (issues_plan.md).
            if ((kind < 0 || kind >= ZONE_CROP_COUNT) && kind != ZONE_CROP_AUTO){
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
        case ZONE_OP_FIELD_NEXT:{
            //The player's pick for every sowing after the first: a crop, or the field's own (fields_plan.md).
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c] || state.buildings[state.field[c]].kind != ZONE_KIND_FIELD){
                last_refusal = "no field there";
                return false;
            }
            if ((kind < 0 || kind >= ZONE_CROP_COUNT) && kind != ZONE_CROP_AUTO){
                last_refusal = "no such crop";
                return false;
            }
            uint32_t id = state.field[c];
            if (state.buildings[id].next_crop == kind){
                return false;
            }
            state.buildings[id].next_crop = (uint8_t)kind;
            break;
        }
        case ZONE_OP_FIELD_RAISE:{
            //The builders' work on a field site: this cell of it is built (the economy raises them all at once).
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c] || state.field_built[c]){
                last_refusal = "nothing to build there";
                return false;
            }
            state.field_built[c] = 1;
            touch_field(c);
            break;
        }
        case ZONE_OP_FIELD_LAND:{
            //The player's say over whose land it is (land_plan.md): a house, or 0 to leave it to the economy.
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c] || state.buildings[state.field[c]].kind != ZONE_KIND_FIELD){
                last_refusal = "no field there";
                return false;
            }
            bool f_house = kind > 0 && kind < (int)state.buildings.size() && state.buildings[kind].kind == ZONE_KIND_HOUSE
                           && state.buildings[kind].size > 0;
            if (kind != 0 && !f_house){
                last_refusal = "not a house";
                return false;
            }
            uint32_t id = state.field[c];
            if (state.buildings[id].land_of == (uint32_t)kind){
                return false;
            }
            state.buildings[id].land_of = (uint32_t)kind;
            break;
        }
        case ZONE_OP_FIELD_PRIORITY:{
            //A field site's "build first" (construction_plan.md), as a building's by a plot of it - by a cell.
            //Only a field still staked out can be put first; any can be put back.
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c]){
                last_refusal = "no field there";
                return false;
            }
            uint32_t id = state.field[c];
            uint8_t want = kind ? 1 : 0;
            if (state.buildings[id].f_priority == want){
                return false;
            }
            bool f_site = false;
            for (size_t o = 0; o < state.field.size() && !f_site; o++){
                f_site = state.field[o] == id && !state.field_built[o];
            }
            if (want && !f_site){
                last_refusal = "nothing to build there";
                return false;
            }
            state.buildings[id].f_priority = want;
            //Its first stake flies the flag while it is first (CropMesh), so its cells are drawn again.
            for (size_t o = 0; o < state.field.size(); o++){
                if (state.field[o] == id){
                    touch_field((int)o);
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
            //A site put first: its flag moves on to the next plot of it still to build (BuildingMesh).
            if (state.buildings[state.building[plot]].f_priority && state.standing[plot] == state.storeys[plot]){
                for (size_t v = 0; v < state.building.size(); v++){
                    if (state.building[v] == state.building[plot]){
                        touch_plot((int)v);
                    }
                }
            }
            break;
        }
        case ZONE_OP_STORE_ALLOW:{
            //Changes nothing that is drawn, only what the store takes - so nothing is touched.
            int plot = (int)index;
            if (plot < 0 || plot >= (int)state.building.size() || state.KindOf(plot) != ZONE_KIND_STORE){
                last_refusal = "no store there";
                return false;
            }
            GoodsMask mask = (GoodsMask)(kind & GOODS_ALL);
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
            //Where a wall and a road cross the plot stays road, the wall's gate (Zones.h, WALLS): a wall
            //drawn over a road leaves it, and gates it.
            if (kind == ZONE_GROUND_WALL && state.ground[plot] == ZONE_GROUND_ROAD){
                if (state.gate[plot]){
                    return false;
                }
                state.gate[plot] = 1;
                touch_road(plot);
                break;
            }
            if (state.ground[plot] == kind){
                //Already that, painted for a building whose yard it is not: it takes it, if nobody owns it and
                //it touches the building or its yard (yards_plan.md).
                if (owner && ZoneEnclosesGround(kind) && ZoneGroundOwner(state,plot) != owner){
                    if (!ZoneCanYard(w,state,plot,kind,owner,&why)){
                        last_refusal = why;
                        return false;
                    }
                    state.ground_owner[plot] = owner;
                    touch_plot(plot);
                    break;
                }
                //A debug paint over a site builds it, as a debug click finishes a building's.
                if (f_play || state.ground_built[plot]){
                    return false;
                }
                state.ground_built[plot] = 1;
                touch_road(plot);
                break;
            }
            if (!ZoneCanGround(w,state,plot,&why,kind) || !ZoneCanYard(w,state,plot,kind,owner,&why)){
                last_refusal = why;
                return false;
            }
            //Whose yard it joins (yards_plan.md): the building it was painted for, else what it touches. A road
            //or wall is nobody's - and one drawn over a garden or lot takes the plot out of its yard.
            state.ground_owner[plot] = ZoneEnclosesGround(kind) ? (owner ? owner : ZoneAutoOwner(w,state,plot,kind)) : 0;
            if (state.ground[plot] == ZONE_GROUND_NONE){
                state.grounds++;
            }
            //...and a road drawn over a wall takes the plot, and is its gate.
            state.gate[plot] = (kind == ZONE_GROUND_ROAD && state.ground[plot] == ZONE_GROUND_WALL) ? 1 : 0;
            state.ground[plot] = (uint8_t)kind;
            //A player's garden or lot is a site until its wood is brought, a road until it is cleared and
            //laid, a wall until both (line_works_plan.md); a debug paint is there.
            state.ground_built[plot] = (f_play && (ZoneEnclosesGround(kind) || ZoneLineGround(kind))) ? 0 : 1;
            //A player's line takes its place in the queue: one id a line, from the buildings' counter.
            if (f_play && ZoneLineGround(kind) && stroke){
                if (stroke != state.line_stroke || !state.line_id){
                    state.line_stroke = stroke;
                    state.line_id = NewBuilding(ZONE_KIND_NONE);
                }
                state.line[plot] = state.line_id;
            }else{
                state.line[plot] = 0;
            }
            touch_road(plot);       //a road's ends, gates and the forest's clearance - see touch_road
            break;
        }
        case ZONE_OP_GROUND_ERASE:{
            if (plot < 0 || plot >= (int)state.ground.size() || state.ground[plot] == ZONE_GROUND_NONE){
                last_refusal = "no garden, lot, road or wall there";
                return false;
            }
            //Taking a wall off a road it gates leaves the road, ungated; a wall erase takes up walls only.
            if (kind == ZONE_GROUND_WALL && state.ground[plot] != ZONE_GROUND_WALL){
                if (state.ground[plot] != ZONE_GROUND_ROAD || !state.gate[plot]){
                    last_refusal = "no wall there";
                    return false;
                }
                state.gate[plot] = 0;
                touch_road(plot);
                break;
            }
            state.ground[plot] = ZONE_GROUND_NONE;
            state.ground_built[plot] = 0;
            state.line[plot] = 0;
            state.gate[plot] = 0;       //a gate's road taken up: the wall has a gap there
            state.ground_owner[plot] = 0;
            state.grounds--;
            touch_road(plot);
            break;
        }
        case ZONE_OP_GROUND_RAISE:{
            //The builders' work on a garden or lot: its wall goes up. A road's: it is laid. A wall's: it
            //stands, and shuts the walkers out. Only one planned.
            if (plot < 0 || plot >= (int)state.ground.size() || state.ground[plot] == ZONE_GROUND_NONE
                || state.ground_built[plot]){
                last_refusal = "nothing to build there";
                return false;
            }
            state.ground_built[plot] = 1;
            touch_road(plot);       //its wall, a gate where a road meets it, the road's curve and verge
            break;
        }
        case ZONE_OP_SITE_PRIORITY:{
            //The player's "build this first" (construction_plan.md). Only a site can be put first; any
            //building can be put back, so a flag never sticks where the card no longer offers it.
            if (plot < 0 || plot >= (int)state.building.size() || state.building[plot] == 0){
                last_refusal = "nothing built there";
                return false;
            }
            uint32_t id = state.building[plot];
            uint8_t want = kind ? 1 : 0;
            if (state.buildings[id].f_priority == want){
                return false;
            }
            bool f_site = false;
            for (size_t v = 0; v < state.building.size() && !f_site; v++){
                f_site = state.building[v] == id && state.standing[v] < state.storeys[v];
            }
            if (want && !f_site){
                last_refusal = "nothing to build there";
                return false;
            }
            state.buildings[id].f_priority = want;
            //Its scaffolds fly a flag while it is first (BuildingMesh), so every plot of it is drawn again.
            for (size_t v = 0; v < state.building.size(); v++){
                if (state.building[v] == id){
                    touch_plot((int)v);
                }
            }
            break;
        }
        case ZONE_OP_GATE_SHUT:{
            /*
                A gate's DOOR shut, or opened (defence_plan.md, step 3) - the player's, from the gate's card. A
                road crossing a wall on the slant gates two plots side by side, one opening: every gate plot
                joined to this one along fine edges goes with it, so a gate is never half shut.
            */
            if (plot < 0 || plot >= (int)state.gate.size() || !state.gate[plot]){
                last_refusal = "no gate there";
                return false;
            }
            uint8_t want = kind ? ZONE_GATE_SHUT : ZONE_GATE_OPEN;
            if (state.gate[plot] == want){
                return false;
            }
            std::vector<int> todo(1,plot);
            while (!todo.empty()){
                int v = todo.back();
                todo.pop_back();
                if (state.gate[v] == want){
                    continue;
                }
                state.gate[v] = want;
                touch_road(v);      //its door drawn again
                int nb[8];
                int n = ZonePlotNeighbours(w,v,nb,8);
                for (int i = 0; i < n; i++){
                    if (state.gate[nb[i]] && state.gate[nb[i]] != want){
                        todo.push_back(nb[i]);
                    }
                }
            }
            break;
        }
        default:
            last_refusal = "unknown zone op";
            return false;
    }
    if (op == ZONE_OP_BUILD_ADD || op == ZONE_OP_BUILD_REMOVE || op == ZONE_OP_BUILD_ERASE || op == ZONE_OP_BUILD_RAISE
        || op == ZONE_OP_FIELD_ERASE || op == ZONE_OP_FIELD_RAISE){
        SettlePriorities();
    }
    state.version++;
    return true;
}

/*
    A site put first stops being first once nothing of it is left to build - raised by its builders,
    finished by a debug click, or its last planned storey taken off; a field once its cells are built or
    the last one staked out is erased - so a storey added later is queued like any other. One pass over
    the plots and the cells, and only while anything is first at all.
*/
void Zones::SettlePriorities(){
    bool f_any = false;
    for (const ZoneBuilding& b : state.buildings){
        f_any = f_any || b.f_priority;
    }
    if (!f_any){
        return;
    }
    std::vector<uint8_t> open(state.buildings.size(),0);
    for (size_t v = 0; v < state.building.size(); v++){
        if (state.standing[v] < state.storeys[v]){
            open[state.building[v]] = 1;
        }
    }
    for (size_t c = 0; c < state.field.size(); c++){
        if (state.field[c] && !state.field_built[c] && state.field[c] < open.size()){
            open[state.field[c]] = 1;
        }
    }
    for (size_t id = 1; id < state.buildings.size(); id++){
        if (!open[id]){
            state.buildings[id].f_priority = 0;
        }
    }
}

int Zones::Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<ZoneSavedBuilding>& buildings,
                   uint32_t next_id, const std::vector<ZoneSavedGround>& grounds,
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
        bool f_crop = (b.crop >= 0 && b.crop < ZONE_CROP_COUNT) || b.crop == ZONE_CROP_AUTO;
        state.buildings[b.id].crop = f_crop ? (uint8_t)b.crop : ZONE_CROP_NONE;
        bool f_next = (b.next_crop >= 0 && b.next_crop < ZONE_CROP_COUNT) || b.next_crop == ZONE_CROP_AUTO;
        state.buildings[b.id].next_crop = f_next ? (uint8_t)b.next_crop : ZONE_CROP_AUTO;
        state.buildings[b.id].allow = (GoodsMask)(b.allow & GOODS_ALL);
        state.buildings[b.id].f_priority = b.f_priority ? 1 : 0;
        //A house the field was given to - ids never change, and one since gone the economy passes over.
        state.buildings[b.id].land_of = (b.kind == ZONE_KIND_FIELD && b.land_of < top) ? b.land_of : 0;
    }
    //Then what stands on the ground, in the order the rules want: fields, ground, buildings - each
    //through the same questions a command asks.
    for (const ZoneSavedBuilding& b : buildings){
        if (!ZoneKindOnCells(b.kind) || b.id == 0){
            continue;
        }
        for (size_t i = 0; i < b.cells.size(); i++){
            int c = b.cells[i];
            if (c < 0 || c >= (int)state.field.size() || state.field[c] || !ZoneCanField(w,state,c,NULL)){
                refused++;
                continue;
            }
            state.field[c] = b.id;
            //A save from before fields were built (fields_plan.md) has no `cells_built`: they all stood.
            state.field_built[c] = (i < b.cells_built.size()) ? (b.cells_built[i] ? 1 : 0) : 1;
            state.buildings[b.id].size++;
            state.field_cells++;
        }
    }
    for (const ZoneSavedGround& gr : grounds){
        //A site goes back as a play paint would leave it; its line and gate as they were.
        bool f_site = (gr.kind & ZONE_GROUND_SAVED_SITE) != 0;
        int kind = gr.kind & ~(ZONE_GROUND_SAVED_SITE | ZONE_GROUND_SAVED_GATE | ZONE_GROUND_SAVED_SHUT);
        if (!Apply(ZONE_OP_GROUND_PAINT,(uint32_t)gr.plot,kind,0,f_site)){
            refused++;
            continue;
        }
        state.line[gr.plot] = (ZoneLineGround(kind) && gr.line < state.buildings.size()) ? gr.line : 0;
        state.gate[gr.plot] = (kind != ZONE_GROUND_ROAD || !(gr.kind & ZONE_GROUND_SAVED_GATE)) ? ZONE_GATE_NONE
                            : (gr.kind & ZONE_GROUND_SAVED_SHUT) ? ZONE_GATE_SHUT : ZONE_GATE_OPEN;
    }
    for (const ZoneSavedBuilding& b : buildings){
        if (ZoneKindOnCells(b.kind) || b.id == 0 || state.buildings[b.id].kind == ZONE_KIND_NONE){
            continue;
        }
        for (size_t i = 0; i < b.plots.size(); i++){
            int v = b.plots[i].first;
            if (v < 0 || v >= (int)state.building.size() || state.building[v] ||
                !ZoneCanBuild(w,state,v,b.kind,0,NULL)){
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
    //Every yard to its owner as saved (yards_plan.md) - not as the rules would give it now: the ground went back
    //before the buildings, with nobody beside it. A save from before yards has none: every yard nobody's.
    std::fill(state.ground_owner.begin(),state.ground_owner.end(),0u);
    for (const ZoneSavedGround& gr : grounds){
        if (gr.plot >= 0 && gr.plot < (int)state.ground_owner.size() && gr.owner && gr.owner < state.buildings.size()
            && state.buildings[gr.owner].size > 0 && ZoneOwnsGround(state.buildings[gr.owner].kind,state.ground[gr.plot])){
            state.ground_owner[gr.plot] = gr.owner;
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
            b.f_priority = state.buildings[id].f_priority != 0;
            b.next_crop = state.buildings[id].next_crop;
            b.land_of = state.buildings[id].land_of;
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
            out[slot[id]].cells_built.push_back((int)state.field_built[c]);
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
            s.floor_area[z.buildings[z.field[c]].kind] += w.picker->CoarseArea((int)c);      //a field's or a range's
        }
    }
    for (size_t v = 0; v < z.ground.size(); v++){
        if (z.ground[v] == ZONE_GROUND_GARDEN){
            s.gardens++;
        }else if (z.ground[v] == ZONE_GROUND_LOT){
            s.lots++;
        }else if (z.ground[v] == ZONE_GROUND_ROAD){
            s.roads++;
        }else if (z.ground[v] == ZONE_GROUND_WALL){
            s.walls++;
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
    info.f_priority = z.buildings[id].f_priority != 0;
    info.size = z.buildings[id].size;
    info.next_crop = z.buildings[id].next_crop;
    if (ZoneKindOnCells(info.kind)){
        //What it grows on is what is built of it; its cells still a site are its construction (fields_plan.md).
        //A range's the same: its area is what is built of it (range_plan.md).
        for (size_t c = 0; c < z.field.size(); c++){
            if (z.field[c] == id){
                if (z.field_built[c]){
                    info.floor_area += w.picker->CoarseArea((int)c);
                }else{
                    info.site_cells++;
                    info.site_area += w.picker->CoarseArea((int)c);
                }
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
    //A gate is a road's, a line a road's or a wall's (Zones.h, WALLS).
    for (size_t v = 0; v < z.ground.size(); v++){
        if ((z.gate[v] && z.ground[v] != ZONE_GROUND_ROAD) || (z.line[v] && !ZoneLineGround(z.ground[v]))){
            bad++;
            issue("zone line",(int)v,w.grid->fine.pos[v]);
        }
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
        if (id >= z.buildings.size() || (id && ZoneKindOnCells(z.buildings[id].kind))){
            broken++;
            issue("zone building",(int)v,w.grid->fine.pos[v]);
        }else if (id){
            counted[id]++;
        }
    }
    for (size_t c = 0; c < z.field.size(); c++){
        uint32_t id = z.field[c];
        if (id >= z.buildings.size() || (id && !ZoneKindOnCells(z.buildings[id].kind))){
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
        bool f_field = ZoneKindOnCells(b.kind);
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

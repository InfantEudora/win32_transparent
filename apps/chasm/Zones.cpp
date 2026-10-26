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
        case ZONE_OP_TOWER_LADDER:  return "tower_ladder";
        case ZONE_OP_CASTLE:        return "castle";
        case ZONE_OP_WORK_STOP:     return "work_stop";
        case ZONE_OP_RANGE_STOP:    return "range_stop";
        case ZONE_OP_SITE_CANCEL:   return "site_cancel";
        case ZONE_OP_FIELD_CANCEL:  return "field_cancel";
        case ZONE_OP_GROUND_BREAK:  return "ground_break";
        case ZONE_OP_TERRACE:       return "terrace";
        case ZONE_OP_TERRACE_ERASE: return "terrace_erase";
        case ZONE_OP_TERRACE_RAISE: return "terrace_raise";
        case ZONE_OP_DOCK_PAINT:    return "dock_paint";
        case ZONE_OP_TOWER_STONE:   return "tower_stone";
        case ZONE_OP_PORTCULLIS:    return "portcullis";
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
        case ZONE_KIND_ROPEWALK:    return "ropewalk";
        case ZONE_KIND_STONECUTTER: return "stonecutter";
        case ZONE_KIND_MINE:        return "mine";
        case ZONE_KIND_WELL:        return "well";
        case ZONE_KIND_STEAM:       return "steam";
        case ZONE_KIND_CONDENSER:   return "condenser";
        case ZONE_KIND_SAIL_LOFT:   return "sail_loft";
        case ZONE_KIND_DOCK:        return "dock";
        case ZONE_KIND_MAST:        return "mast";
        case ZONE_KIND_HUT:         return "hut";
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
        case ZONE_KIND_ROPEWALK:    return 1;       //a long LOW shed (goods_plan.md): length is what it grows by
        case ZONE_KIND_STONECUTTER: return 1;       //a yard's shed (stone_plan.md): its stone is a storey's cost
        case ZONE_KIND_MINE:        return 1;       //a portal into the rock (stone_plan.md step 5)
        case ZONE_KIND_WELL:        return 1;       //a ring of stone and its roof (water_tools_plan.md)
        case ZONE_KIND_STEAM:       return 1;       //a shed with its hood out over the drop (steam_plan.md)
        case ZONE_KIND_CONDENSER:   return 1;       //a shed with its brass tanks through the roof (steam_plan.md)
        case ZONE_KIND_SAIL_LOFT:   return 1;       //one long floor to lay a canvas out on (steam_plan.md step 4)
        case ZONE_KIND_MAST:        return 1;       //the dock's lattice mast, on its own plot (flight_plan.md step 3)
        case ZONE_KIND_HUT:         return 1;       //one room on stilts (swamp_huts_plan.md)
        default:                    return 0;
    }
}

const char* ZoneCropName(int crop){
    switch (crop){
        case ZONE_CROP_WHEAT:       return "wheat";
        case ZONE_CROP_GREENS:      return "greens";
        case ZONE_CROP_BEANS:       return "beans";
        case ZONE_CROP_HEMP:        return "hemp";
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

//The building an arch is part of, among its plot's neighbours `nb` (ZoneArchOwner): the one with the most plots beside
//it, the older (lower id) on a tie - one on both sides wins with all of them.
uint32_t ArchOwnerAmong(const ZoneState& z, const int* nb, int n){
    uint32_t best = 0;
    int best_count = 0;
    for (int i = 0; i < n; i++){
        uint32_t id = z.building[nb[i]];
        if (!id){
            continue;
        }
        int count = 0;
        for (int j = 0; j < n; j++){
            count += (z.building[nb[j]] == id) ? 1 : 0;
        }
        if (count > best_count || (count == best_count && id < best)){
            best = id;
            best_count = count;
        }
    }
    return best;
}

#define ZONE_ARCH_PASSAGE_MOST  3   //half storeys: the most a passage stands - a storey and a half

/*
    A road plot with two road neighbours and every other neighbour a building with a house body, all of one kind,
    standing high enough to carry a storey of floor over a passage: what could be bridged. The storeys it would be
    bridged to - the passage and the floors over it - or 0; its floor over the passage, in half storeys
    (BuildingFootHalves' count), in `floor_out`. Its road neighbours in `roads_out`.
*/
int ArchCandidate(const ChasmWorld& w, const ZoneState& z, int plot, int* roads_out, int* floor_out = NULL){
    if (!ZoneRoadLaid(z,plot)){
        return 0;       //a planned road is bridged once it is laid
    }
    //Never over a gate (arches_plan.md): its posts and door stand taller than the passage. A gatehouse is its own.
    if (plot < (int)z.gate.size() && z.gate[plot] != ZONE_GATE_NONE){
        return 0;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    int roads = 0;
    int kind = ZONE_KIND_NONE;
    //What stands: a site's storeys bridge nothing yet. SAME KIND only (the user's): a house never bridges into a store,
    //and a guard tower - drawn apart, with no body to carry a floor - never at all.
    auto side = [&](int u){
        return z.standing[u] >= 1 && ZoneKindHasBody(z.KindOf(u)) && (kind == ZONE_KIND_NONE || z.KindOf(u) == kind);
    };
    for (int i = 0; i < n; i++){
        int u = nb[i];
        if (ZoneRoadLaid(z,u)){
            if (roads < 2){
                roads_out[roads] = u;
            }
            roads++;
        }else if (side(u)){
            kind = z.KindOf(u);
        }else{
            return 0;           //an open side: nothing to bridge from
        }
    }
    //Two road neighbours: the road goes through, straight or round a bend - Townscaper's passages bend
    //too. A dead end under a house would be a tunnel to nowhere, a junction a hall.
    if (roads != 2 || n < 4){
        return 0;
    }
    /*
        ROUND A BEND, where the two roads meet at a corner of one cell, that cell's fourth plot is the passage's inside:
        a building of the kind too, as where a road steps through a block - or the arch stands out over an open corner,
        its walls there missing (the user's, 2026-10-23: nothing is built there).
    */
    const GridPicker& picker = *w.picker;
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        int qc = picker.PlotQuadCorner(plot,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        int next = q.v[(k + 1) % 4];
        int prev = q.v[(k + 3) % 4];
        bool f_bend = (next == roads_out[0] && prev == roads_out[1]) || (next == roads_out[1] && prev == roads_out[0]);
        if (f_bend && !side(q.v[(k + 2) % 4])){
            return 0;
        }
    }
    /*
        ITS HEIGHTS, by what stands either side rather than by the road (terraces_plan.md, the user's 2026-10-23): its
        floor is its owner's storey that stands a storey over the road at the least, a storey and a half at the most, so
        the bridge meets its owner's floor - and a side on a terrace a storey up meets it with its ground floor. Its eaves
        are its lowest side's. Every side reaches down to its floor, and a storey of floor fits under the eaves.
    */
    uint32_t owner = ArchOwnerAmong(z,nb,n);
    int road = BuildingFootHalves(w,z,ZONE_KIND_NONE,plot);
    int floor = INT_MIN;
    for (int i = 0; i < n && floor == INT_MIN; i++){
        if (z.building[nb[i]] == owner){
            floor = BuildingFootHalves(w,z,kind,nb[i]);
        }
    }
    if (floor == INT_MIN){
        return 0;
    }
    while (floor < road + 2){
        floor += 2;
    }
    if (floor - road > ZONE_ARCH_PASSAGE_MOST){
        return 0;
    }
    int eave = INT_MAX;
    for (int i = 0; i < n; i++){
        int u = nb[i];
        if (ZoneRoadLaid(z,u)){
            continue;
        }
        int foot = BuildingFootHalves(w,z,kind,u);
        if (foot > floor){
            return 0;           //standing higher than the bridge: nothing to carry it on that side
        }
        eave = std::min(eave,foot + 2 * (int)z.standing[u]);
    }
    int floors = (eave - floor) / 2;
    if (floors < 1){
        return 0;
    }
    if (floor_out){
        *floor_out = floor;
    }
    return 1 + floors;
}

//Plots: through a row of houses, not down a street. Three, not two: a road crossing a row steps along
//the grid's edges, so even a row two houses deep takes it three plots to cross.
#define ZONE_ARCH_MAX_RUN   3

}

int ZoneArchStoreys(const ChasmWorld& w, const ZoneState& z, int plot, int* floor_halves){
    int roads[2];
    int top = ArchCandidate(w,z,plot,roads,floor_halves);
    if (top == 0){
        return 0;
    }
    /*
        Only a SHORT run is bridged. A street lined both sides with tall houses makes every plot down it
        a candidate, and covering it would make a tunnel; a hole through a row of houses is a run of one
        or two. So walk the run both ways from here and give up past the most it may be.
        ...for a NEW arch only (arches_plan.md step 4, the user's): one with a floor built stays while its own sides
        stand, so tall buildings going up beside the next road plot along never take down floors already paid
        for. The run still caps what is planned, so no passage grows past ZONE_ARCH_MAX_RUN.
    */
    if (z.ArchBuiltOf(plot) > 0){
        return top;
    }
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

uint32_t ZoneArchOwner(const ChasmWorld& w, const ZoneState& z, int plot){
    if (ZoneArchStoreys(w,z,plot) == 0){
        return 0;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    return ArchOwnerAmong(z,nb,n);
}

int Zones::RefreshArches(bool f_stand){
    size_t n = state.building.size();
    std::vector<uint8_t> was = state.arch;
    was.resize(n,0);
    state.arch.assign(n,0);
    state.arch_owner.assign(n,0);
    state.arch_floor.assign(n,0);
    state.arch_built.resize(n,0);
    if (!state.world || state.ground.size() < n){
        std::fill(state.arch_built.begin(),state.arch_built.end(),0);
        return 0;
    }
    const ChasmWorld& w = *state.world;
    int lost = 0;
    for (size_t v = 0; v < n; v++){
        int floor = 0;
        int s = (state.ground[v] == ZONE_GROUND_ROAD) ? ZoneArchStoreys(w,state,(int)v,&floor) : 0;
        if (s > 0){
            state.arch[v] = (uint8_t)s;
            state.arch_owner[v] = ZoneArchOwner(w,state,(int)v);
            state.arch_floor[v] = (int16_t)floor;
        }
        /*
            Its floors built: a debug placement's new ones stand at once, as the building does; and none past what
            it may rise to now - a side lowered, the road gone, a building of another kind beside it, and what no
            longer qualifies comes down, as a storey taken off does.
        */
        int floors = ZoneArchFloors(s);
        int built = state.arch_built[v];
        if (f_stand && s > was[v]){
            built += floors - ZoneArchFloors(was[v]);
        }
        built = std::max(0,std::min(built,floors));
        lost += std::max(0,(int)state.arch_built[v] - built);
        state.arch_built[v] = (uint8_t)built;
    }
    return lost;
}

/*
    SLOPE (biomes_plan.md step 2): the most the ground may rise across what is built on it, world units -
    over a plot for a building or ground (every corner of every cell round it), over its nine vertices
    for a field. A road takes more: it climbs; so does a water collector, which stands on a bank. A
    building on a slope stands on a footing (BuildingMesh) - and steeper, up to one storey, on a FOUNDATION
    (terraces_plan.md): a kind that takes one may, the stone for it on its ground storey (Economy).
*/
#define ZONE_RISE_HOUSE     0.7f
#define ZONE_RISE_ROAD      1.4f
#define ZONE_RISE_FIELD     1.3f
#define ZONE_RISE_FOUNDATION 1.3f       //one storey (ZONE_STOREY_HEIGHT): hills, not crags (the user's, 2026-10-21)
#define ZONE_TERRACE_MOST_FILL (2.0f * ZONE_STOREY_HEIGHT)     //a terrace's top over the lowest ground under it (terraces_plan.md)

float ZonePlotRise(const ChasmWorld& w, int plot){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& p = *w.picker;
    if (plot < 0 || plot >= (int)g.fine.pos.size()){
        return 0.0f;
    }
    float lowest = t.ground[plot];
    float highest = lowest;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& quad = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            lowest = std::min(lowest,t.ground[quad.v[k]]);
            highest = std::max(highest,t.ground[quad.v[k]]);
        }
    }
    return highest - lowest;
}

bool ZoneNeedsFoundation(const ChasmWorld& w, const ZoneState& z, int kind, int plot){
    return ZoneKindTakesFoundation(kind) && !ZoneTerraceStands(z,plot) && ZonePlotRise(w,plot) > ZONE_RISE_HOUSE;
}

float ZoneGroundAt(const ChasmWorld& w, const ZoneState& z, int plot){
    return ZoneTerraceStands(z,plot) ? ZoneTerraceTop(z,plot) : w.terrain->ground[plot];
}

float ZoneGroundHeight(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& p){
    return ZoneTerraceStands(z,plot) ? ZoneTerraceTop(z,plot) : w.terrain->GroundHeight(p,w.terrain->Height(plot));
}

#define WALL_STRAIGHT_FROM  5       //an edge with a point of this many cells or more at an end takes a straight wall
#define WALL_CROSSING_REACH 0.25f   //...crossing it no further than this of the edge from its midpoint, on a cell bent badly

vec2 BuildingWallCrossing(const ChasmWorld& w, int a, int b){
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    if (a > b){
        std::swap(a,b);
    }
    vec2 pa = g.fine.pos[a];
    vec2 pb = g.fine.pos[b];
    vec2 mid = (pa + pb) * 0.5f;
    int na = picker.PlotQuadCount(a);
    int nb = picker.PlotQuadCount(b);
    if ((na < WALL_STRAIGHT_FROM && nb < WALL_STRAIGHT_FROM) || na <= 3 || nb <= 3){
        return mid;
    }
    //The two cells either side of the edge, in index order; none across it on the map's edge.
    int cells[2];
    int found = 0;
    for (int i = 0; i < na && found < 2; i++){
        int q = picker.PlotQuadCorner(a,i) / 4;
        const GridQuad& gq = g.fine.quads[q];
        if (gq.v[0] == b || gq.v[1] == b || gq.v[2] == b || gq.v[3] == b){
            cells[found++] = q;
        }
    }
    if (found < 2){
        return mid;
    }
    if (cells[0] > cells[1]){
        std::swap(cells[0],cells[1]);
    }
    //pa + e t on the line from one centre to the other: t = (r x d) / (e x d).
    vec2 c0 = g.FineQuadCentre(cells[0]);
    vec2 d = g.FineQuadCentre(cells[1]) - c0;
    vec2 e = pb - pa;
    vec2 r = c0 - pa;
    float den = e.x * d.y - e.y * d.x;
    if (std::fabs(den) < 1e-9f){
        return mid;
    }
    float t = (r.x * d.y - r.y * d.x) / den;
    t = std::max(0.5f - WALL_CROSSING_REACH,std::min(0.5f + WALL_CROSSING_REACH,t));
    return pa + e * t;
}

vec2 BuildingWallOut(const ChasmWorld& w, int a, int b, bool* f_straight){
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    vec2 along = g.fine.pos[b] - g.fine.pos[a];
    //The cells either side of the edge, in a's order round it - the same whichever cell asks.
    vec2 centres[2];
    int found = 0;
    for (int i = 0; i < picker.PlotQuadCount(a) && found < 2; i++){
        int q = picker.PlotQuadCorner(a,i) / 4;
        const GridQuad& gq = g.fine.quads[q];
        if (gq.v[0] == b || gq.v[1] == b || gq.v[2] == b || gq.v[3] == b){
            centres[found++] = g.FineQuadCentre(q);
        }
    }
    vec2 out = along;
    if (f_straight){
        *f_straight = false;
    }
    if (found == 2){
        vec2 d = centres[1] - centres[0];
        out = vec2(-d.y,d.x);
        if (out.dot(along) < 0.0f){
            out = out * -1.0f;
        }
        if (f_straight){
            vec2 at = BuildingWallCrossing(w,a,b);
            vec2 in = at - centres[0];
            vec2 on = centres[1] - at;
            float lens = in.length() * on.length();
            *f_straight = lens > 1e-9f && in.dot(on) / lens > ZONE_WALL_STRAIGHT;
        }
    }
    float len = out.length();
    return (len > 1e-6f) ? out * (1.0f / len) : vec2(0.0f,0.0f);
}

int BuildingFootHalves(const ChasmWorld& w, const ZoneState& z, int kind, int plot){
    const float HALF = ZONE_STOREY_HEIGHT * 0.5f;
    const Terrain& t = *w.terrain;
    //On a terrace, its top: whole storeys, so exactly a number of halves (terraces_plan.md stage 2).
    if (ZoneTerraceStands(z,plot)){
        return (int)std::lround(ZoneTerraceTop(z,plot) / HALF);
    }
    if (!ZoneNeedsFoundation(w,z,kind,plot)){
        return (int)std::lround(t.ground[plot] / HALF);
    }
    //Its walls run round the quarters of the cells round it, by each edge's crossing and each cell's centre.
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    float level = t.Height(plot);
    float highest = t.ground[plot];
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        int qc = picker.PlotQuadCorner(plot,i);
        const GridQuad& q = g.fine.quads[qc / 4];
        int k = qc % 4;
        highest = std::max(highest,t.GroundHeight(g.FineQuadCentre(qc / 4),level));
        highest = std::max(highest,t.GroundHeight(BuildingWallCrossing(w,plot,q.v[(k + 1) % 4]),level));
        highest = std::max(highest,t.GroundHeight(BuildingWallCrossing(w,plot,q.v[(k + 3) % 4]),level));
    }
    return (int)std::ceil(highest / HALF);
}

void ZonePlotGroundRange(const ChasmWorld& w, int plot, float& lowest, float& highest){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& p = *w.picker;
    lowest = highest = t.ground[plot];
    float level = t.Height(plot);
    const vec2& at = g.fine.pos[plot];
    auto take = [&](const vec2& x){
        float y = t.GroundHeight(x,level);
        lowest = std::min(lowest,y);
        highest = std::max(highest,y);
    };
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        int qc = p.PlotQuadCorner(plot,i);
        const GridQuad& q = g.fine.quads[qc / 4];
        int k = qc % 4;
        take(g.FineQuadCentre(qc / 4));
        take((at + g.fine.pos[q.v[(k + 1) % 4]]) * 0.5f);
        take((at + g.fine.pos[q.v[(k + 3) % 4]]) * 0.5f);
    }
}
/*
    What a building and ground both need of their plot: flat all round it - every corner of every
    fine cell round the plot on the plot's level, so nothing stands half over a cliff - dry, and no
    field touching it. `f_waterside` turns the water rule round, for a water collector: some corner
    must be wet, and the plot's own vertex dry - the last dry plot before the bank's stones, which its
    pipe reaches over (the user, 2026-10-06), and which its carrier can walk to. `f_rim` lets a WALL stand at a
    cliff's edge (the user's, 2026-10-23): the corners down the cliff are passed over, and its slope is its own
    ground's - so a palisade runs right up to the chasm, with no last plot on the rim to walk round it by.
*/
static bool PlotIsBuildable(const ChasmWorld& w, const ZoneState& z, int plot, const char** why,
                            float max_rise = ZONE_RISE_HOUSE, bool f_waterside = false, bool f_path = false,
                            bool f_rim = false){
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
    //On a terrace (terraces_plan.md): only once it stands - and then flat, below.
    if (ZoneTerraced(z,plot) && !ZoneTerraceStands(z,plot)){
        return refuse("the terrace is still being built");
    }
    const GridPicker& p = *w.picker;
    bool f_water = false;
    bool f_at_cliff = false;
    float lowest = t.ground[plot];
    float highest = lowest;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        int q = p.PlotQuadCorner(plot,i) / 4;
        const GridQuad& quad = g.fine.quads[q];
        for (int k = 0; k < 4; k++){
            int v = quad.v[k];
            if (!t.SameGround(v,plot)){
                if (!f_rim){
                    return refuse("too close to a cliff");
                }
                f_at_cliff = true;
                continue;
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
    //Every corner on the plot's own ground, here: its rise is the one the slope grid and a foundation's stone go by - none
    //on a terrace that stands, whose top is flat. A wall at a cliff's edge: its own ground's corners only.
    float rise = f_at_cliff ? highest - lowest : ZonePlotRise(w,plot);
    if (!ZoneTerraceStands(z,plot) && rise > max_rise){
        return refuse((max_rise == ZONE_RISE_FOUNDATION) ? "too steep, even for a foundation" : "too steep");
    }
    return true;
}

//A step a walker could take from a to b over the ground alone (Walkers::EdgeOpen on a new map): no cliff, no water, no
//mountain, not too steep.
static bool GroundStep(const ChasmWorld& w, int a, int b){
    const Terrain& t = *w.terrain;
    float run = (w.grid->fine.pos[b] - w.grid->fine.pos[a]).length();
    return t.SameGround(a,b) && !t.wet[b] && !t.Mountain(b) && !w.grid->fine.f_boundary[b]
           && std::fabs(t.ground[b] - t.ground[a]) <= WALKER_STEEPEST * run;
}

int ZoneHutDoor(const ChasmWorld& w, int plot){
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size()){
        return -1;
    }
    int best = -1;
    int best_open = -1;
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    for (int i = 0; i < n; i++){
        if (!GroundStep(w,plot,nb[i])){
            continue;
        }
        //How open the ground before it is: the steps on from there. A hut's door looks out over its hummock, not at
        //the reeds.
        int on[8];
        int m = ZonePlotNeighbours(w,nb[i],on,8);
        int open = 0;
        for (int j = 0; j < m; j++){
            open += (on[j] != plot && GroundStep(w,nb[i],on[j])) ? 1 : 0;
        }
        if (open > best_open || (open == best_open && nb[i] < best)){
            best = nb[i];
            best_open = open;
        }
    }
    return best;
}

/*
    Where a SWAMP HUT may stand (swamp_huts_plan.md): in the swamp, its own point dry, and a dry plot before its door. It
    stands on stilts, so unlike a house it may have the wet at its corners - a hut by the pools is the point - and a
    foundation's slope. Never the player's: only the seed places one (Zones::PlaceSwampHuts), or a debug paint.
*/
static bool HutIsBuildable(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
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
    if (t.biome[plot] != TERRAIN_BIOME_SWAMP){
        return refuse("a swamp hut stands in the swamp");
    }
    if (t.level[plot] == TERRAIN_FLOOR || t.level[plot] == TERRAIN_BARE){
        return refuse("not on the plateau");
    }
    if (t.wet[plot]){
        return refuse("in the water - a hut's stilts stand at a pool's edge, not in it");
    }
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& quad = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            if (!t.SameGround(quad.v[k],plot)){
                return refuse("too close to a cliff");
            }
            if (t.Mountain(quad.v[k])){
                return refuse("on the mountain");
            }
        }
        if (!z.field.empty() && z.field[quad.parent]){
            return refuse("a field is there");
        }
    }
    if (ZonePlotRise(w,plot) > ZONE_RISE_FOUNDATION){
        return refuse("too steep, even on stilts");
    }
    if (ZoneHutDoor(w,plot) < 0){
        return refuse("no dry ground before its door");
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

int ZoneTowerGuards(const ChasmWorld& w, int plot){
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size()){
        return 0;
    }
    return std::max(1,std::min(4,w.picker->PlotQuadCount(plot) - 2));
}

void ZoneTowerCorners(const ChasmWorld& w, int plot, std::vector<vec2>& out){
    out.clear();
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size()){
        return;
    }
    vec2 p = w.grid->fine.pos[plot];
    std::vector<std::pair<float,vec2>> round;
    for (int i = 0; i < w.picker->PlotQuadCount(plot); i++){
        vec2 c = w.grid->FineQuadCentre(w.picker->PlotQuadCorner(plot,i) / 4);
        vec2 at = p + (c - p) * ZONE_TOWER_INSET;
        round.push_back(std::make_pair(std::atan2(at.y - p.y,at.x - p.x),at));
    }
    std::sort(round.begin(),round.end(),[](const std::pair<float,vec2>& a, const std::pair<float,vec2>& b){
        return a.first < b.first;
    });
    for (const auto& r : round){
        out.push_back(r.second);
    }
}

int ZoneTowerLevel(const ChasmWorld& w, const ZoneState& z, int plot){
    if (ZoneTerraceStands(z,plot)){
        return z.terrace[plot];
    }
    return ZoneCastleLevelFor(w,z,plot,0);
}

bool ZoneTowerCrenellated(const ChasmWorld& w, int plot){
    return plot >= 0 && plot < (int)w.grid->fine.pos.size() && w.picker->PlotQuadCount(plot) != 4;
}

int ZoneBuildingMaxStoreys(const ZoneState& z, int plot){
    return ZoneTowerStone(z,plot) ? ZONE_STONE_TOWER_MOST : ZoneKindMaxStoreys(z.KindOf(plot));
}

vec2 ZoneTowerPlace(const ChasmWorld& w, int plot, int rank){
    vec2 p = (plot >= 0 && plot < (int)w.grid->fine.pos.size()) ? w.grid->fine.pos[plot] : vec2();
    int guards = ZoneTowerGuards(w,plot);
    std::vector<vec2> corners;
    ZoneTowerCorners(w,plot,corners);
    if (guards <= 1 || corners.empty()){
        return p;
    }
    //Spread round it: of n corners, every n/guards-th - so on a square the two stand across it from each other.
    int n = (int)corners.size();
    int corner = ((rank % guards) * n) / guards;
    return p + (corners[corner] - p) * ZONE_TOWER_STAND;
}

bool ZoneTowerLadderOk(const ChasmWorld& w, const ZoneState& z, int tower, int to){
    const Terrain& t = *w.terrain;
    int n_plots = (int)w.grid->fine.pos.size();
    if (tower < 0 || to < 0 || tower >= n_plots || to >= n_plots || to >= (int)z.storeys.size()){
        return false;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,tower,nb,8);
    if (std::find(nb,nb + n,to) == nb + n){
        return false;
    }
    float run = std::max(1e-3f,(w.grid->fine.pos[to] - w.grid->fine.pos[tower]).length());
    if (!t.SameGround(tower,to) || t.wet[to] || t.Mountain(to) || w.grid->fine.f_boundary[to]
        || std::fabs(t.ground[to] - t.ground[tower]) > WALKER_STEEPEST * run){
        return false;
    }
    return z.storeys[to] == 0 && (z.ground.empty() || z.ground[to] != ZONE_GROUND_WALL) && (z.gate.empty() || !z.gate[to])
           && !ZoneCastleShuts(z,to);
}

int ZoneTowerDefaultLadder(const ChasmWorld& w, const ZoneState& z, int tower){
    //The camp's first plot: the colony's side of a wall, as near as the zones can say.
    int camp = -1;
    for (size_t v = 0; v < z.building.size() && camp < 0; v++){
        uint32_t b = z.building[v];
        if (b && b < z.buildings.size() && z.buildings[b].kind == ZONE_KIND_CAMP){
            camp = (int)v;
        }
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,tower,nb,8);
    int best = -1;
    float best_d = 0.0f;
    for (int i = 0; i < n; i++){
        if (!ZoneTowerLadderOk(w,z,tower,nb[i])){
            continue;
        }
        float d = (camp >= 0) ? (w.grid->fine.pos[nb[i]] - w.grid->fine.pos[camp]).length() : 0.0f;
        if (best < 0 || d < best_d || (d == best_d && nb[i] < best)){
            best = nb[i];
            best_d = d;
        }
    }
    return best;
}

bool ZoneIsLadderPlot(const ChasmWorld& w, const ZoneState& z, int plot){
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size() || z.building.empty()){
        return false;
    }
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    for (int i = 0; i < n; i++){
        if (ZoneTowerLadder(z,nb[i]) == plot){
            return true;
        }
    }
    return false;
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
    THE MINE's own ground rule (stone_plan.md step 5): one plot ON A DEPOSIT (Deposits.h) - a floatstone seam's foot,
    dug into its wall; the mountain's foot, dug into its slope (the user's, 2026-10-15: the plateau's gentle foot too,
    a portal into the slope); or an outcrop, a pit. With the winch, the only building that stands against a cliff:
    the rock it digs into is left out, and the rest of the corners round it are on its ground, dry, off the mountain,
    gentle and with no field. Never where a winch lands.
*/
static bool MineIsBuildable(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
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
    if (!t.Buildable(plot)){
        return refuse("not on ground a mine can stand on");
    }
    const GridPicker& p = *w.picker;
    int dep = w.deposits ? w.deposits->At(plot) : -1;
    if (dep < 0){
        //At the foot of a wall with nothing in it, the reason that says so.
        bool f_wall = false;
        for (int i = 0; i < p.PlotQuadCount(plot) && !f_wall; i++){
            const GridQuad& quad = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
            for (int k = 0; k < 4; k++){
                f_wall = f_wall || t.steps[quad.v[k]] < t.steps[plot];
            }
        }
        return refuse(f_wall ? "no seam in this wall" : "a mine is dug into a seam's wall, the mountain or an outcrop");
    }
    int kind = w.deposits->deposits[dep].kind;
    float lowest = t.ground[plot];
    float highest = lowest;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& quad = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = quad.v[k];
            if (kind == DEPOSIT_MOUNTAIN && t.Mountain(v)){
                continue;       //the slope it digs into
            }
            if (!t.SameGround(v,plot)){
                if (kind == DEPOSIT_SEAM){
                    continue;   //the wall it digs into, or the balcony's own lip behind it
                }
                return refuse("too close to a cliff");
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
    if (highest - lowest > ZONE_RISE_HOUSE){
        return refuse("too steep");
    }
    if (PlotIsWinchLanding(w,z,plot)){
        return refuse("a winch lands there");
    }
    return true;
}

/*
    THE STEAM COLLECTOR's own ground rule (steam_plan.md step 2): at the EDGE OF A DROP into the chasm, on the plateau's
    rim, a balcony or an island - some corner of the cells round it lower than its plot, so it leans out over the drop,
    and none higher, so it never stands against a wall. The drop is the floor under the mist, the rim's crumbled steps or
    a terrace below; a cliff painted by hand is no drop into the chasm. The corners on its own ground dry, off the
    mountain, with no field and gentle, as a house's. Never where a winch lands. A MOORING MAST's too (flight_plan.md
    step 3), which leans its cup out over the drop as the collector its hood: `kind` names which, in the words.
*/
static bool SteamIsBuildable(const ChasmWorld& w, const ZoneState& z, int plot, const char** why, int kind){
    bool f_mast = kind == ZONE_KIND_MAST;
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
    auto painted = [&t](int v){
        return v < (int)t.cliff.size() && t.cliff[v] > 0;
    };
    if (painted(plot)){
        return refuse(f_mast ? "not over the chasm - a mast stands where the ground drops into it"
                             : "not over the chasm - a steam collector stands where the ground drops into it");
    }
    const GridPicker& p = *w.picker;
    bool f_drop = false;
    float lowest = t.ground[plot];
    float highest = lowest;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& quad = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = quad.v[k];
            if (t.steps[v] > t.steps[plot]){
                if (painted(v)){
                    return refuse(f_mast ? "too close to a cliff - a mast leans over the chasm, not a painted cliff"
                                         : "too close to a cliff - a steam collector leans over the chasm, not a painted cliff");
                }
                f_drop = true;      //the drop it leans out over
                continue;
            }
            if (!t.SameGround(v,plot)){
                return refuse(f_mast ? "against a wall - a mast leans out over a drop"
                                     : "against a wall - a steam collector leans out over a drop");
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
    if (!f_drop){
        return refuse(f_mast ? "not at the edge - a mast stands where the ground drops into the chasm"
                             : "not at the edge - a steam collector stands where the ground drops into the chasm");
    }
    if (highest - lowest > ZONE_RISE_HOUSE){
        return refuse("too steep");
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

//Why a building of `kind` takes no more plots (ZoneKindMaxPlots) - in words, so a number changed there is changed here.
static const char* FullRefusal(int kind){
    static_assert(ZONE_BUILDING_MAX_PLOTS == 4 && ZONE_HOUSE_MAX_PLOTS == 4 && ZONE_ROPEWALK_MAX_PLOTS == 4
                  && ZONE_STORE_MAX_PLOTS == 6 && ZONE_FIELD_MAX_CELLS == 8 && ZONE_RANGE_MAX_CELLS == 4
                  && ZONE_DOCK_MAX_CELLS == 8,
                  "the sizes in words below");
    switch (kind){
        case ZONE_KIND_HOUSE:       return "a house is at most four plots - one family";
        case ZONE_KIND_ROPEWALK:    return "a ropewalk is at most four plots - longer makes no more rope";
        case ZONE_KIND_STORE:       return "a store is at most six plots";
        case ZONE_KIND_FIELD:       return "a field is at most eight cells";
        case ZONE_KIND_RANGE:       return "a range is at most four cells";
        case ZONE_KIND_DOCK:        return "a dock is at most eight cells";
        case ZONE_KIND_WINCH:       return "a winch is one plot";
        case ZONE_KIND_TOWER:       return "a tower is one plot";
        case ZONE_KIND_MINE:        return "a mine is one plot";
        case ZONE_KIND_WELL:        return "a well is one plot";
        case ZONE_KIND_MAST:        return "a mast is one plot";
        case ZONE_KIND_HUT:         return "a hut is one plot";
        default:                    return "a building is at most four plots";
    }
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
    //A tower's ladder is its door (defence_plan.md): nothing goes where it stands.
    if (ZoneIsLadderPlot(w,z,plot)){
        return refuse("a tower's ladder stands there");
    }
    if (ZoneCastleShape(z,plot) != ZONE_CASTLE_NONE){
        return refuse("a castle piece stands there");    //the debug castle's (ramparts_plan.md)
    }
    if (!f_line_ok && !z.ground.empty() && plot >= 0 && plot < (int)z.ground.size() && ZoneLineGround(z.ground[plot])){
        return refuse((z.ground[plot] == ZONE_GROUND_WALL) ? "a wall is there" : "a road is there");
    }
    //On a terrace (terraces_plan.md): what stands on its ground as a house does, and a guard tower - not the kinds whose
    //place is a drop, a seam, a bank, the water or a camp's.
    if (ZoneTerraced(z,plot) && !ZoneKindTakesFoundation(kind) && kind != ZONE_KIND_TOWER){
        return refuse("not on a terrace");
    }
    if (kind == ZONE_KIND_WINCH){
        if (joining){
            return refuse("a winch is one plot");
        }
        return WinchIsBuildable(w,z,plot,why);
    }
    if (kind == ZONE_KIND_MINE){
        if (joining){
            return refuse("a mine is one plot");
        }
        return MineIsBuildable(w,z,plot,why);
    }
    //A well (water_tools_plan.md) goes wherever a house could, on its own: it needs no water near.
    if (kind == ZONE_KIND_WELL && joining){
        return refuse("a well is one plot");
    }
    if (kind == ZONE_KIND_MAST && joining){
        return refuse("a mast is one plot");
    }
    if (kind == ZONE_KIND_HUT){
        if (joining){
            return refuse("a hut is one plot");
        }
        if (!z.ground.empty() && plot >= 0 && plot < (int)z.ground.size() && z.ground[plot] != ZONE_GROUND_NONE){
            return refuse("something is laid there");
        }
        return HutIsBuildable(w,z,plot,why);
    }
    if (kind == ZONE_KIND_BRIDGE){
        return BridgePlotOk(w,z,plot,why);
    }
    if (plot >= 0 && plot < (int)w.grid->fine.pos.size() && PlotIsWinchLanding(w,z,plot)){
        return refuse("a winch lands there");
    }
    bool f_water = (kind == ZONE_KIND_WATER);
    float rise = f_water ? ZONE_RISE_ROAD : ZoneKindTakesFoundation(kind) ? ZONE_RISE_FOUNDATION : ZONE_RISE_HOUSE;
    bool f_drop = kind == ZONE_KIND_STEAM || kind == ZONE_KIND_MAST;
    if (f_drop ? !SteamIsBuildable(w,z,plot,why,kind) : !PlotIsBuildable(w,z,plot,why,rise,f_water)){
        return false;
    }
    if (!f_line_ok && !z.ground.empty() && ZoneLineGround(z.ground[plot])){
        return refuse((z.ground[plot] == ZONE_GROUND_WALL) ? "a wall is there" : "a road is there");
    }
    if (joining && ZoneKindMaxPlots(kind) && joining < z.buildings.size() &&
        z.buildings[joining].size >= ZoneKindMaxPlots(kind)){
        return refuse(FullRefusal(kind));
    }
    //A guard tower needs a side for its ladder, its only way up.
    if (kind == ZONE_KIND_TOWER){
        int nb[8];
        int n = ZonePlotNeighbours(w,plot,nb,8);
        bool f_open = false;
        for (int i = 0; i < n && !f_open; i++){
            f_open = ZoneTowerLadderOk(w,z,plot,nb[i]);
        }
        if (!f_open){
            return refuse("no open side for its ladder");
        }
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
    int most = ZoneKindMaxPlots(kind) ? ZoneKindMaxPlots(kind) : INT_MAX;
    if (z.buildings[owner].size >= most){
        return refuse(FullRefusal(kind));
    }
    if (kind == ZONE_KIND_CAMP){
        return true;        //its tents need not touch (THE CAMP, Zones.h)
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
            return refuse(FullRefusal(kind));
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
    if (!PlotIsBuildable(w,z,plot,why,f_line ? ZONE_RISE_ROAD : ZONE_RISE_HOUSE,false,f_line,kind == ZONE_GROUND_WALL)){
        return false;
    }
    if (f_line && !z.storeys.empty() && z.storeys[plot] > 0){
        if (why){
            *why = "a building is there";
        }
        return false;
    }
    //Nor any ground under a swamp hut: it is the swamp's (ZoneKindWild), and a hut stands only on bare ground - a garden
    //under one would refuse it its place on the next load.
    const ZoneBuilding* on = z.BuildingOf(plot);
    if (on && ZoneKindWild(on->kind)){
        if (why){
            *why = "a swamp hut stands there";
        }
        return false;
    }
    if (kind == ZONE_GROUND_WALL && ZoneIsLadderPlot(w,z,plot)){
        if (why){
            *why = "a tower's ladder stands there";
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
    /*
        Nor a road beside another across a terrace's wall taller than a storey (terraces_plan.md): a road's steps climb a
        storey at most, so the two would be joined in the drawing and never walked - a road that read as broken.
    */
    if (kind == ZONE_GROUND_ROAD && !z.ground.empty() && !z.terrace.empty()){
        int nb[8];
        int n = ZonePlotNeighbours(w,plot,nb,8);
        for (int i = 0; i < n; i++){
            if (z.ground[nb[i]] == ZONE_GROUND_ROAD && ZoneTerraceTooTall(w,z,plot,nb[i])){
                if (why){
                    *why = "steps climb a storey at most - put a terrace a storey lower between";
                }
                return false;
            }
        }
    }
    //The debug castle (ramparts_plan.md): a road runs on under an arch, and through a gate - or through a wall, a block,
    //which it makes a gate, or leaves a gap in (castle_plan.md); nothing goes under anything else of it.
    int castle = ZoneCastleShape(z,plot);
    bool f_through = castle == ZONE_CASTLE_ARCH || castle == ZONE_CASTLE_GATE
                     || (castle == ZONE_CASTLE_BLOCK && ZoneCastleOverShape(z,plot) == ZONE_CASTLE_NONE);
    if (castle != ZONE_CASTLE_NONE && !(f_through && kind == ZONE_GROUND_ROAD)){
        if (why){
            *why = "a castle piece stands there";
        }
        return false;
    }
    return true;
}

int ZoneLandSays(const ChasmWorld& w, const ZoneState& z, int plot, int kind, bool f_road, const char** why, bool f_wall){
    //A terrace that stands is level ground made of the land - the one thing built that the land's say counts.
    if (ZoneTerraceStands(z,plot)){
        return ZONE_LAND_ALLOWED;
    }
    const ZoneState none;           //a new map's: nothing built yet
    float rise = f_road ? ZONE_RISE_ROAD : ZoneKindTakesFoundation(kind) ? ZONE_RISE_FOUNDATION : ZONE_RISE_HOUSE;
    if (!PlotIsBuildable(w,none,plot,why,rise,false,f_road,f_wall)){
        return ZONE_LAND_REFUSED;
    }
    return ZoneNeedsFoundation(w,none,kind,plot) ? ZONE_LAND_FOUNDATION : ZONE_LAND_ALLOWED;
}

#define ZONE_TERRACE_STEP   0.35f       //the most a terrace stands over the ground across and is still stepped onto

int ZoneTerraceStep(const ChasmWorld& w, const ZoneState& z, int a, int b){
    bool f_a = ZoneTerraceStands(z,a);
    bool f_b = ZoneTerraceStands(z,b);
    if (!f_a && !f_b){
        return ZONE_STEP_OPEN;
    }
    float ga = ZoneGroundAt(w,z,a);
    float gb = ZoneGroundAt(w,z,b);
    //Its wall is where it stands over what is across; where the hill rises past it, the slope's rules are the walkers'.
    if (!(f_a && ga - gb > ZONE_TERRACE_STEP) && !(f_b && gb - ga > ZONE_TERRACE_STEP)){
        return ZONE_STEP_OPEN;
    }
    /*
        A STAIR, built from below (the user's, 2026-10-26): it stands with the road at the wall's foot, the road above it
        only planned. Laid only once both roads stood, the one above could never be reached to be laid - its layer walks
        onto the plot he lays, and nothing walked onto a terrace without a stair.
    */
    int low = (ga < gb) ? a : b;
    int high = (low == a) ? b : a;
    if (ZoneRoadStands(z,low) && z.ground[high] == ZONE_GROUND_ROAD && std::fabs(ga - gb) <= ZONE_STOREY_HEIGHT + 1e-3f){
        return ZONE_STEP_STAIR;
    }
    if (ZoneUpperDoor(w,z,a,b) >= 0 || ZoneUpperDoor(w,z,b,a) >= 0){
        return ZONE_STEP_DOOR;
    }
    return ZONE_STEP_WALL;
}

bool ZoneTerraceTooTall(const ChasmWorld& w, const ZoneState& z, int a, int b){
    float ga = ZoneGroundAt(w,z,a);
    float gb = ZoneGroundAt(w,z,b);
    //A terrace's own wall: the higher of the two a terrace standing over the other - not the hill rising past one.
    int high = (ga > gb) ? a : b;
    return ZoneTerraceStands(z,high) && std::fabs(ga - gb) > ZONE_STOREY_HEIGHT + 1e-3f;
}

//ZoneUpperDoor's storey from building plot b, its floor `floor` half storeys up, onto plot u - before the one door a level.
static int UpperDoorStorey(const ZoneState& z, int b, int u, int floor){
    if (u < 0 || u >= (int)z.building.size() || !ZoneTerraceStands(z,u) || z.storeys[u] > 0 || ZoneShutToWalkers(z,u)){
        return -1;
    }
    //In half storeys, as floors are counted: a terrace's top is whole storeys, a floor whole or half.
    int top = (int)std::lround(ZoneTerraceTop(z,u) / (ZONE_STOREY_HEIGHT * 0.5f));
    //The lowest storey whose floor is at the top or over it - by at most a half.
    int storey = (top > floor) ? (top - floor + 1) / 2 : 0;
    if (floor + 2 * storey - top > 1 || storey >= z.standing[b]){
        return -1;
    }
    return storey;
}

int ZoneUpperDoor(const ChasmWorld& w, const ZoneState& z, int b, int u){
    if (b < 0 || b >= (int)z.building.size() || z.standing[b] == 0 || !ZoneKindHasBody(z.KindOf(b))){
        return -1;
    }
    int floor = BuildingFootHalves(w,z,z.KindOf(b),b);
    int storey = UpperDoorStorey(z,b,u,floor);
    if (storey < 0){
        return -1;
    }
    //ONE DOOR A LEVEL from a plot: onto the first of its neighbours that storey opens onto - a plot meeting a terrace
    //along two edges gets one door onto it, not one an edge.
    int nb[8];
    int n = ZonePlotNeighbours(w,b,nb,8);
    for (int i = 0; i < n && nb[i] != u; i++){
        if (UpperDoorStorey(z,b,nb[i],floor) == storey){
            return -1;
        }
    }
    return storey;
}

void ZoneCellPlots(const ChasmWorld& w, int coarse, std::vector<int>& out){
    out.clear();
    const Grid& g = *w.grid;
    if (coarse < 0 || coarse >= (int)g.coarse.quads.size()){
        return;
    }
    for (int q = coarse * 4; q < coarse * 4 + 4; q++){
        for (int k = 0; k < 4; k++){
            int v = g.fine.quads[q].v[k];
            if (std::find(out.begin(),out.end(),v) == out.end()){
                out.push_back(v);
            }
        }
    }
}

bool ZoneCanTerrace(const ChasmWorld& w, const ZoneState& z, int plot, int top, const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (plot < 0 || plot >= (int)z.terrace.size()){
        return refuse("no such plot");
    }
    if (ZoneTerraced(z,plot)){
        return refuse("a terrace is there");
    }
    //The ground a building wants - one ground all round, dry, off the mountain, no field - at any slope: levelling it is
    //what a terrace is for.
    if (!PlotIsBuildable(w,z,plot,why,1e9f)){
        return false;
    }
    if (z.storeys[plot] > 0 || z.ArchOf(plot) > 0){
        return refuse("a building is there - a terrace goes first, then what stands on it");
    }
    if (z.ground[plot] != ZONE_GROUND_NONE){
        return refuse("ground is laid there - a terrace goes first, then what is laid on it");
    }
    if (ZoneCastleShape(z,plot) != ZONE_CASTLE_NONE){
        return refuse("a castle piece stands there");
    }
    if (ZoneIsLadderPlot(w,z,plot)){
        return refuse("a tower's ladder stands there");
    }
    if (PlotIsWinchLanding(w,z,plot)){
        return refuse("a winch lands there");
    }
    if (top == ZONE_NO_TERRACE){
        return true;
    }
    //Built up only: over every bit of the ground under it, and not so far over the lowest that the wall is a cliff.
    float lowest, highest;
    ZonePlotGroundRange(w,plot,lowest,highest);
    float y = (float)top * ZONE_STOREY_HEIGHT;
    if (y < highest - 1e-3f){
        return refuse("the ground is higher than the terrace - it is only built up, never dug");
    }
    if (y - lowest > ZONE_TERRACE_MOST_FILL){
        return refuse("too far below the terrace - at most two storeys of fill");
    }
    return true;
}

int ZoneTerraceTopOver(const ChasmWorld& w, const ZoneState& z, const std::vector<int>& plots, int lead){
    if (plots.empty()){
        return ZONE_NO_TERRACE;
    }
    for (int i = 0; i < lead && i < (int)plots.size(); i++){
        if (ZoneTerraced(z,plots[i])){
            return z.terrace[plots[i]];
        }
    }
    float highest = -1e30f;
    for (int v : plots){
        if (ZoneCanTerrace(w,z,v,ZONE_NO_TERRACE,NULL)){
            float lo, hi;
            ZonePlotGroundRange(w,v,lo,hi);
            highest = std::max(highest,hi);
        }
    }
    if (highest < -1e29f){
        return ZONE_NO_TERRACE;
    }
    int top = (int)std::ceil(highest / ZONE_STOREY_HEIGHT - 1e-4f);
    return std::max(INT8_MIN + 1,std::min(INT8_MAX,top));
}

const char* ZoneCastleShapeName(int shape){
    switch (shape){
        case ZONE_CASTLE_BLOCK: return "block";
        case ZONE_CASTLE_ARCH:  return "arch";
        case ZONE_CASTLE_STAIR: return "stair";
        case ZONE_CASTLE_GATE:  return "gate";
        case ZONE_CASTLE_LANDING:   return "landing";
        case ZONE_CASTLE_TURRET:    return "turret";
        default:                return "none";
    }
}

int ZoneCastleShapeByName(const std::string& name){
    for (int s = ZONE_CASTLE_BLOCK; s < ZONE_CASTLE_SHAPES; s++){
        if (name == ZoneCastleShapeName(s)){
            return s;
        }
    }
    return -1;
}

bool ZoneCastleGoesOver(const ZoneState& z, int plot, int shape, int storeys){
    int under = ZoneCastleShape(z,plot);
    if (under == ZONE_CASTLE_GATE || shape == ZONE_CASTLE_GATE){
        return false;       //a gate's passage is its own: nothing over it, and it over nothing
    }
    if (shape == ZONE_CASTLE_ARCH){
        return under != ZONE_CASTLE_NONE && storeys >= ZoneCastleStoreys(z,plot) + ZONE_CASTLE_HEADROOM;
    }
    return shape == ZONE_CASTLE_STAIR && under == ZONE_CASTLE_ARCH && storeys == ZoneCastleStoreys(z,plot) + 1;
}

int ZoneCastleLevelFor(const ChasmWorld& w, const ZoneState& z, int plot, uint32_t stroke){
    //(A stair's drag started on a piece has painted nothing yet, so has no level of its own.)
    if (stroke != 0 && stroke == z.castle_stroke && z.castle_stroke_base != ZONE_CASTLE_NO_BASE){
        return z.castle_stroke_base;
    }
    if (ZoneCastleShape(z,plot) != ZONE_CASTLE_NONE){
        return ZoneCastleBase(z,plot);
    }
    float lowest = 0.0f;
    float highest = 0.0f;
    ZonePlotGroundRange(w,plot,lowest,highest);
    return (int)std::ceil((highest - ZONE_CASTLE_SETTLE) / ZONE_STOREY_HEIGHT - 1e-4f);
}

bool ZoneCanCastle(const ChasmWorld& w, const ZoneState& z, int plot, int shape, int storeys, int base, const char** why){
    auto refuse = [why](const char* reason){
        if (why){
            *why = reason;
        }
        return false;
    };
    if (shape <= ZONE_CASTLE_NONE || shape >= ZONE_CASTLE_SHAPES){
        return refuse("no such castle piece");
    }
    if (storeys < 1 || storeys > ZONE_CASTLE_MAX_STOREYS){
        return refuse("a castle piece is one to four storeys");
    }
    //Erased only with nothing on it, a terrace never has a piece taken out from under it: beside one, a deck level with
    //its top goes on onto it (Walkers.h, LAYERS).
    if (ZoneTerraced(z,plot)){
        return refuse("not on a terrace");
    }
    //Over the piece there: its plot passed the rules when it was put down.
    if (ZoneCastleGoesOver(z,plot,shape,storeys)){
        return true;
    }
    //In its place, under a piece over it: only one that still carries that.
    int over = ZoneCastleOverStoreys(z,plot);
    if (ZoneCastleOverShape(z,plot) == ZONE_CASTLE_ARCH && over < storeys + ZONE_CASTLE_HEADROOM){
        return refuse("an arch stands over it - two storeys over what is under it, at least");
    }
    if (ZoneCastleOverSits(z,plot) && !(shape == ZONE_CASTLE_ARCH && storeys + 1 == over)){
        return refuse("a stair stands on it - an arch a storey under it carries it");
    }
    if (shape == ZONE_CASTLE_ARCH && storeys < ZONE_CASTLE_ARCH_LOWEST){
        return refuse("an arch is two storeys at least - its passage clears a gate");
    }
    if (shape == ZONE_CASTLE_GATE && storeys < ZONE_CASTLE_ARCH_LOWEST){
        return refuse("a gate is two storeys at least - a one-storey wall leaves the road open");
    }
    //LEVEL (ZoneCastleBase): a storey over all the ground under it - an arch two, its passage's, and a gate's passage,
    //always a two-storey wall's, its level itself - less what it may settle into it, or the hill has risen past the
    //drag's level there.
    float lowest = 0.0f;
    float highest = 0.0f;
    ZonePlotGroundRange(w,plot,lowest,highest);
    int least = (shape == ZONE_CASTLE_ARCH) ? ZONE_CASTLE_ARCH_LOWEST : (shape == ZONE_CASTLE_GATE) ? storeys : 1;
    if (highest > (float)(base + storeys - least) * ZONE_STOREY_HEIGHT + ZONE_CASTLE_SETTLE + 1e-3f){
        return refuse("the ground rises past this level - start a new drag up there, or a stair");
    }
    //A road's rules: it may stand beside the wet, on a road's slope - an arch is where a road goes.
    if (!PlotIsBuildable(w,z,plot,why,ZONE_RISE_ROAD,false,true)){
        return false;
    }
    if (z.storeys[plot] > 0){
        return refuse("a building is there");
    }
    if (ZoneIsLadderPlot(w,z,plot)){
        return refuse("a tower's ladder stands there");
    }
    int ground = z.ground[plot];
    if (shape == ZONE_CASTLE_GATE){
        return (ground == ZONE_GROUND_ROAD) ? true : refuse("a gate is where a road goes through a wall");
    }
    if (ground != ZONE_GROUND_NONE && !(shape == ZONE_CASTLE_ARCH && ground == ZONE_GROUND_ROAD)){
        return refuse((shape == ZONE_CASTLE_ARCH) ? "an arch goes over open ground or a road"
                                                  : "a block or a stair goes on open ground");
    }
    return true;
}

namespace {

vec2 StairUnit(const vec2& d){
    float l = d.length();
    return (l > 1e-6f) ? d * (1.0f / l) : vec2(0.0f,0.0f);
}

bool PlotsJoin(const ChasmWorld& w, int a, int b){
    int nb[8];
    int n = ZonePlotNeighbours(w,a,nb,8);
    for (int i = 0; i < n; i++){
        if (nb[i] == b){
            return true;
        }
    }
    return false;
}

//Whether plot u has a FLAT walkable node `halves` high (WalkNodeHalves): a deck - a piece's top, an arch over it, a
//landing, a turret's top, a guard tower's platform - or a stone tower's floor; with `f_ground` its ground too, a
//terrace's top or the hill, which rounds to the half storey under it.
bool StairFlatAt(const ChasmWorld& w, const ZoneState& z, int u, int halves, bool f_ground){
    int ls[WALK_MOST_DECKS];
    int c = WalkDeckLayers(z,u,ls);
    for (int j = 0; j < c; j++){
        if (!WalkLayerStair(ls[j]) && WalkNodeHalves(w,z,u,ls[j]) == halves){
            return true;
        }
    }
    if (ZoneTowerStone(z,u) && z.standing[u] > 0){
        int lowest = 0;
        int highest = 0;
        StoneTowerFloors(w,z,u,lowest,highest);
        if (halves >= lowest && halves <= highest && ((halves - lowest) & 1) == 0){
            return true;
        }
    }
    if (f_ground && !ZoneCastleShuts(z,u) && z.storeys[u] == 0){
        int g = WalkNodeHalves(w,z,u,WALK_GROUND);
        return g == halves || g == halves - 1;
    }
    return false;
}

//The deck a stair's drag pressed on plot u starts from (ZonePlanStair), in whole storeys from y = 0: its stair's top, a
//castle piece's - an arch over it its - or a stone tower's guard room.
int StairStartTop(const ChasmWorld& w, const ZoneState& z, int u){
    if (ZoneHasStair(z,u)){
        return ZoneStairTop(z,u);
    }
    if (ZoneCastleShape(z,u) != ZONE_CASTLE_NONE){
        return ZoneCastleBase(z,u) + std::max(ZoneCastleStoreys(z,u),ZoneCastleOverStoreys(z,u));
    }
    return ZoneTowerLevel(w,z,u) + z.standing[u];
}

}

int ZoneStairDowns(const ChasmWorld& w, const ZoneState& z, int v, int* out, int max){
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    int c = 0;
    for (int i = 0; i < n && c < max; i++){
        if (ZoneHasStair(z,nb[i]) && ZoneStairUp(z,nb[i]) == v){
            out[c++] = nb[i];
        }
    }
    return c;
}

vec2 ZoneStairUpDir(const ChasmWorld& w, const ZoneState& z, int v){
    const vec2 at = w.grid->fine.pos[v];
    int up = ZoneStairUp(z,v);
    if (up >= 0){
        return StairUnit(w.grid->fine.pos[up] - at);
    }
    int top = 2 * ZoneStairTop(z,v);
    vec2 decks(0.0f,0.0f);
    vec2 below(0.0f,0.0f);
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    for (int i = 0; i < n; i++){
        int u = nb[i];
        vec2 d = StairUnit(w.grid->fine.pos[u] - at);
        if (ZoneHasStair(z,u) && ZoneStairUp(z,u) == v){
            below = below - d;
        }else if (w.terrain->SameGround(v,u) && StairFlatAt(w,z,u,top,true)){
            decks = decks + d;
        }
    }
    return StairUnit((decks.dot(decks) > 1e-6f) ? decks : below);
}

bool ZoneStairFootMeets(const ChasmWorld& w, const ZoneState& z, int v, const vec2& up, int halves){
    const vec2 at = w.grid->fine.pos[v];
    bool f_any_side = up.dot(up) < 1e-6f;
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    for (int i = 0; i < n; i++){
        int u = nb[i];
        if (!w.terrain->SameGround(v,u) || (ZoneHasStair(z,u) && ZoneStairUp(z,u) == v)){
            continue;
        }
        if ((f_any_side || StairUnit(w.grid->fine.pos[u] - at).dot(up) < -ZONE_STAIR_SIDE) && StairFlatAt(w,z,u,halves,true)){
            return true;
        }
    }
    return false;
}

int ZoneStairForm(const ChasmWorld& w, const ZoneState& z, int v){
    int shape = ZoneCastleShape(z,v);
    if (!ZoneCastleStairForm(shape)){
        return shape;
    }
    int top = ZoneStairTop(z,v);
    int downs[8];
    int n = ZoneStairDowns(w,z,v,downs,8);
    for (int i = 0; i < n; i++){
        if (ZoneStairTop(z,downs[i]) == top){
            return ZONE_CASTLE_LANDING;
        }
    }
    if (n > 0 || ZoneCastleStoreys(z,v) <= 1){
        return ZONE_CASTLE_STAIR;
    }
    return ZoneStairFootMeets(w,z,v,ZoneStairUpDir(w,z,v),2 * (top - 1)) ? ZONE_CASTLE_STAIR : ZONE_CASTLE_TURRET;
}

bool ZonePlanStair(const ChasmWorld& w, const ZoneState& z, int plot, uint32_t stroke, ZoneStairStep& st){
    st = ZoneStairStep();
    auto refuse = [&](const char* why){
        st.why = why;
        return false;
    };
    if (plot < 0 || plot >= (int)z.castle.size()){
        return refuse("no such plot");
    }
    bool f_first = stroke == 0 || stroke != z.castle_stroke || z.castle_stroke_last < 0;
    int last = f_first ? -1 : z.castle_stroke_last;
    if (plot == last){
        st.f_quiet = true;
        return false;
    }
    const std::vector<vec2>& pos = w.grid->fine.pos;
    int top = 0;        //whole storeys from y = 0
    if (f_first){
        //Pressed on a stair, a castle piece or a stone tower, the drag starts from its deck.
        if (ZoneHasStair(z,plot) || ZoneCastleShape(z,plot) != ZONE_CASTLE_NONE || ZoneTowerStone(z,plot)){
            st.start = plot;
            return true;
        }
        //On open ground, down from the highest flat deck beside it it can reach - or up from the ground, with none.
        st.base = ZoneCastleLevelFor(w,z,plot,0);
        int best = INT_MIN;
        int nb[8];
        int n = ZonePlotNeighbours(w,plot,nb,8);
        for (int i = 0; i < n; i++){
            if (!w.terrain->SameGround(plot,nb[i])){
                continue;
            }
            for (int t = st.base + ZONE_CASTLE_MAX_STOREYS; t > std::max(best,st.base); t--){
                if (StairFlatAt(w,z,nb[i],2 * t,false)){
                    best = t;
                    break;
                }
            }
        }
        st.f_climb = best == INT_MIN;
        top = st.f_climb ? st.base + 1 : best;
    }else{
        if (!PlotsJoin(w,last,plot)){
            return refuse("a stair goes on from the plot before it - drag it plot by plot");
        }
        if (ZoneHasStair(z,plot)){
            st.f_quiet = true;      //back over its own stair
            return false;
        }
        st.f_climb = z.f_castle_stroke_climb;
        st.base = (z.castle_stroke_base != ZONE_CASTLE_NO_BASE) ? z.castle_stroke_base : ZoneCastleLevelFor(w,z,plot,0);
        vec2 on = StairUnit(pos[plot] - pos[last]);     //the way the drag goes
        bool f_over_last = ZoneCastleOverSits(z,last);
        int downs[8];
        int n = ZoneHasStair(z,last) ? ZoneStairDowns(w,z,last,downs,8) : 0;
        if (!st.f_climb && !ZoneHasStair(z,last)){
            top = StairStartTop(w,z,last);      //the first stair down from the piece it started from
        }else if (!st.f_climb){
            //On down from `last`: a storey lower going straight on; turning, the same height - `last` the landing it
            //turns on, which a flight that already goes on down from it cannot be. From a landing, at its height.
            int lt = ZoneStairTop(z,last);
            bool f_straight = (ZoneStairUpDir(w,z,last) * -1.0f).dot(on) > ZONE_STAIR_STRAIGHT;
            bool f_landing = false;
            for (int i = 0; i < n; i++){
                f_landing = f_landing || ZoneStairTop(z,downs[i]) == lt;
            }
            if (f_landing){
                top = lt;
            }else if (!f_straight && (n > 0 || f_over_last)){
                return refuse(f_over_last ? "a stair over an arch goes straight on"
                                          : "this flight goes on down already - a stair turns on a landing");
            }else{
                top = f_straight ? lt - 1 : lt;
            }
            st.up = last;
        }else{
            //Up from the ground: `last` the top so far, which climbs to this one now - a landing at the height of the
            //stair below it, if the way turns there. Come to a deck at the height it has climbed to, it goes on onto it
            //level: a landing beside it, rather than a storey past it.
            int lt = ZoneStairTop(z,last);
            if (n > 0 && StairUnit(pos[last] - pos[downs[0]]).dot(on) <= ZONE_STAIR_STRAIGHT){
                if (f_over_last){
                    return refuse("a stair over an arch goes straight on");
                }
                lt = ZoneStairTop(z,downs[0]);
                st.landing = last;
                st.landing_storeys = lt - ZoneCastleBase(z,last);
            }
            bool f_deck = false;
            int nb[8];
            int c = ZonePlotNeighbours(w,plot,nb,8);
            for (int i = 0; i < c && !f_deck; i++){
                f_deck = nb[i] != last && w.terrain->SameGround(plot,nb[i]) && !ZoneHasStair(z,nb[i])
                         && StairFlatAt(w,z,nb[i],2 * lt,false);
            }
            top = f_deck ? lt : lt + 1;
            st.relink = last;
        }
        if (!st.f_climb && top <= st.base){
            return refuse("the stair is down at the ground - it ends at the plot before");
        }
    }
    //Over an arch a storey under it, as before; never in another piece's place.
    int shape = ZoneCastleShape(z,plot);
    if (shape == ZONE_CASTLE_ARCH && ZoneCastleOverShape(z,plot) == ZONE_CASTLE_NONE
        && top - ZoneCastleBase(z,plot) == ZoneCastleStoreys(z,plot) + 1){
        st.f_over = true;
        st.base = ZoneCastleBase(z,plot);
    }else if (shape != ZONE_CASTLE_NONE || ZoneTowerStone(z,plot)){
        return refuse("a stair goes on open ground, or over an arch a storey under it");
    }
    st.storeys = top - st.base;
    if (st.storeys < 1 || st.storeys > ZONE_CASTLE_MAX_STOREYS){
        return refuse("a stair is one to four storeys over its level");
    }
    const char* why = NULL;
    if (!st.f_over && !ZoneCanCastle(w,z,plot,ZONE_CASTLE_STAIR,st.storeys,st.base,&why)){
        return refuse(why);
    }
    return true;
}

//A field's cell, or with `f_dock` a dock's: a balcony's too, within ZONE_DOCK_REACH of the chasm's edge.
static bool CellBuildable(const ChasmWorld& w, const ZoneState& z, int coarse, const char** why, bool f_dock){
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
            if (level == TERRAIN_BALCONY && !f_dock){
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
            //A cell on a terrace (terraces_plan.md) once it stands, its top the ground: all nine on one terrace is flat.
            if (ZoneTerraced(z,v) && !ZoneTerraceStands(z,v)){
                return refuse("the terrace is still being built");
            }
            lowest = std::min(lowest,ZoneGroundAt(w,z,v));
            highest = std::max(highest,ZoneGroundAt(w,z,v));
        }
    }
    if (highest - lowest > ZONE_RISE_FIELD){
        return refuse("too steep");
    }
    if (f_dock){
        vec2 mid(0.0f,0.0f);
        for (int q = coarse * 4; q < coarse * 4 + 4; q++){
            mid += g.FineQuadCentre(q) * 0.25f;
        }
        if (ChasmRiftDistance(g.layout,mid) > ZONE_DOCK_REACH){
            return refuse("too far from the chasm's edge - a zeppelin floats only over the chasm");
        }
    }
    return true;
}

bool ZoneCanField(const ChasmWorld& w, const ZoneState& z, int coarse, const char** why){
    return CellBuildable(w,z,coarse,why,false);
}

bool ZoneCanDock(const ChasmWorld& w, const ZoneState& z, int coarse, const char** why){
    return CellBuildable(w,z,coarse,why,true);
}

bool ZoneDockFrame(const ChasmWorld& w, const ZoneState& z, uint32_t id, ZoneDockLie& out){
    out = ZoneDockLie();
    if (!id || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_DOCK || z.buildings[id].size <= 0){
        return false;
    }
    const Grid& g = *w.grid;
    std::vector<vec2> mids;
    for (size_t c = 0; c < z.field.size(); c++){
        if (z.field[c] == id){
            vec2 mid(0.0f,0.0f);
            for (int q = (int)c * 4; q < (int)c * 4 + 4; q++){
                mid += g.FineQuadCentre(q) * 0.25f;
            }
            mids.push_back(mid);
        }
    }
    if (mids.empty()){
        return false;
    }
    vec2 centre(0.0f,0.0f);
    for (const vec2& m : mids){
        centre += m * (1.0f / (float)mids.size());
    }
    //Its long way: the cells' middles' principal axis.
    float sxx = 0.0f, sxy = 0.0f, syy = 0.0f;
    for (const vec2& m : mids){
        vec2 d = m - centre;
        sxx += d.x * d.x;
        sxy += d.x * d.y;
        syy += d.y * d.y;
    }
    float along = 0.5f * std::atan2(2.0f * sxy,sxx - syy);
    out.centre = centre;
    GridPick under = w.picker->Pick(centre);
    out.ground = (under.f_hit && under.plot >= 0) ? ZoneGroundAt(w,z,under.plot) : 0.0f;
    auto toward_chasm = [&](vec2 d){
        float ahead = ChasmRiftDistance(g.layout,centre + d * 5.0f);
        float behind = ChasmRiftDistance(g.layout,centre - d * 5.0f);
        return (behind < ahead - 0.01f) ? d * -1.0f : d;
    };
    out.nose = toward_chasm(vec2(std::cos(along),std::sin(along)));
    for (int t = 0; t <= 2 * ZONE_DOCK_FIT_TURNS; t++){
        int turn = (t + 1) / 2 * ((t % 2) ? 1 : -1);        //0, +1, -1, +2, -2...
        float a = along + turn * ZONE_DOCK_FIT_STEP;
        vec2 d(std::cos(a),std::sin(a));
        vec2 across(-d.y,d.x);
        bool f_on = true;
        for (float s = -ZONE_DOCK_FIT_LENGTH * 0.5f; s <= ZONE_DOCK_FIT_LENGTH * 0.5f + 1e-3f && f_on; s += 1.0f){
            for (float c = -ZONE_DOCK_FIT_WIDTH * 0.5f; c <= ZONE_DOCK_FIT_WIDTH * 0.5f + 1e-3f && f_on; c += ZONE_DOCK_FIT_WIDTH / 4.0f){
                GridPick p = w.picker->Pick(centre + d * s + across * c);
                f_on = p.f_hit && p.coarse_quad >= 0 && p.coarse_quad < (int)z.field.size() && z.field[p.coarse_quad] == id;
            }
        }
        if (f_on){
            out.nose = toward_chasm(d);
            out.f_fits = true;
            break;
        }
    }
    return true;
}

bool ZoneMastAt(const ChasmWorld& w, int plot, ZoneMastPlace& out){
    const Terrain& t = *w.terrain;
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    if (plot < 0 || plot >= (int)g.fine.pos.size()){
        return false;
    }
    vec2 p = g.fine.pos[plot];
    vec2 sum(0.0f,0.0f);
    float nearest = 1e30f;
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        const GridQuad& q = g.fine.quads[picker.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            if (t.steps[q.v[k]] > t.steps[plot]){
                vec2 d = g.fine.pos[q.v[k]] - p;
                sum = sum + d;
                nearest = std::min(nearest,d.length());
            }
        }
    }
    float l = sum.length();
    if (l < 1e-4f || nearest > 1e29f){
        return false;
    }
    out.dir = sum * (1.0f / l);
    out.foot = p + out.dir * (nearest * 0.5f);
    vec2 cup = out.foot + out.dir * ZONE_MAST_PAST_LIP;
    out.cup = vec3(cup.x,t.ground[plot] + ZONE_MAST_CUP,cup.y);
    return true;
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

#define ZONE_CAMP_FIRE_OUT  1.3f    //from the tent's plot to its cell's middle, and this much of the way again: clear of the tent

int ZoneCampFireTent(const ChasmWorld& w, const ZoneState& z, int fine_quad){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    if (fine_quad < 0 || fine_quad >= (int)g.fine.quads.size()){
        return -1;
    }
    const GridQuad& quad = g.fine.quads[fine_quad];
    if (quad.parent >= 0 && quad.parent < (int)z.field.size() && z.field[quad.parent]){
        return -1;      //a field's cell is sown, not sat round
    }
    int tent = -1;
    for (int v : quad.v){
        if (tent < 0 && z.KindOf(v) == ZONE_KIND_CAMP && z.standing[v] > 0){
            tent = v;
        }else if (z.building[v] || z.ground[v] != ZONE_GROUND_NONE || t.wet[v] || t.Mountain(v)){
            return -1;  //a second tent, anything built or laid, the water or the mountain: no room for a fire
        }
    }
    if (tent < 0){
        return -1;
    }
    for (int v : quad.v){
        if (!t.SameGround(tent,v)){
            return -1;  //and not over a terrace's edge
        }
    }
    uint32_t h = (uint32_t)fine_quad * 0x85EBCA6Bu + 0xF12Eu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return (h % ZONE_CAMP_FIRE_EVERY == 0) ? tent : -1;
}

vec2 ZoneCampFireAt(const ChasmWorld& w, int fine_quad, int tent){
    vec2 p = w.grid->fine.pos[tent];
    return p + (w.grid->FineQuadCentre(fine_quad) - p) * ZONE_CAMP_FIRE_OUT;
}

int ZoneCampTents(const ZoneState& z, uint32_t id){
    if (id == 0 || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_CAMP){
        return 0;
    }
    int n = 0;
    for (size_t v = 0; v < z.building.size(); v++){
        n += (z.building[v] == id) ? 1 : 0;
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
    //The camp fits from v: as PlaceStartCamp lays it, a flood through the plots a tent may take, to `tents` tents.
    auto fits = [&](int v){
        if (!house(v)){
            return false;
        }
        int m = ++marks;
        camp_queue.assign(1,v);
        camp_mark[v] = m;
        int have = 0;
        for (size_t at = 0; at < camp_queue.size() && have < tents; at++){
            have++;
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
            have++;
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

/*
    WHERE THE SWAMP HUTS STAND (swamp_huts_plan.md): every swamp plot a hut may stand on, scored on
      - DEEP: in from the swamp's edge - full marks from ZONE_HUT_DEEP of its radius in, so they keep to the swamp's
        heart rather than its fringe, without all crowding the one deepest point;
      - BY THE WATER: best with ZONE_HUT_WATER_BEST of the ground round it wet - at a pool's edge, its stilts in the
        shallows - falling to nothing on a dry hummock and in the middle of the pools;
      - and a little of the seed's own, so two maps with alike swamps do not put their huts alike.
    Refused near the map's edge, within ZONE_HUT_CAMP_LEAST of the camp, and with a tree on a plot beside it - a hut
    stands in a clearing. Whether a walk from a hut's door reaches
    the camp over the ground as it is - no bridge - is noted: the huts whose zombies can walk to the colony are taken
    before those a river cuts off, so a map's threat is a real one where the swamp allows it.
*/
static float HutJitter(uint32_t seed, int plot){
    uint32_t h = (uint32_t)plot * 0x9E3779B1u + seed * 0x85EBCA77u + 0x5A4Bu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return (float)(h % 1000) * 0.001f;
}
#define ZONE_HUT_W_JITTER   0.3f    //the seed's share of a spot's score, against 1 each for deep and by the water

void ZoneSwampHutSpots(const ChasmWorld& w, int camp, std::vector<ZoneHutSpot>& out){
    out.clear();
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const int n = (int)g.fine.pos.size();
    const ChasmLayout::Region& swamp = g.layout.swamp;
    if (swamp.radius <= 0.0f){
        return;
    }
    const ZoneState none;           //a new map's: nothing built yet
    //Every plot a walk from the camp reaches over the ground: what a zombie could walk to before anything is built.
    std::vector<uint8_t> reach(n,0);
    std::vector<int> queue;
    if (camp >= 0 && camp < n){
        queue.assign(1,camp);
        reach[camp] = 1;
        for (size_t i = 0; i < queue.size(); i++){
            int nb[8];
            int k = ZonePlotNeighbours(w,queue[i],nb,8);
            for (int j = 0; j < k; j++){
                if (!reach[nb[j]] && GroundStep(w,queue[i],nb[j])){
                    reach[nb[j]] = 1;
                    queue.push_back(nb[j]);
                }
            }
        }
    }
    //The plots a tree stands on: a hut stands in a clearing, as no willow beside it could be drawn without growing
    //through its walls (one on its own plot it hides, as any building does).
    std::vector<uint8_t> wooded(n,0);
    if (w.forest){
        for (const PropInstance& pr : w.forest->props){
            if (ForestIsTree(pr.kind) && pr.plot >= 0 && pr.plot < n){
                wooded[pr.plot] = 1;
            }
        }
    }
    auto clearing = [&](int v){
        int nb[8];
        int k = ZonePlotNeighbours(w,v,nb,8);
        for (int j = 0; j < k; j++){
            if (wooded[nb[j]]){
                return false;
            }
        }
        return true;
    };
    std::vector<int> mark(n,0);
    int marks = 0;
    for (int v = 0; v < n; v++){
        if (t.biome[v] != TERRAIN_BIOME_SWAMP){
            continue;
        }
        ZoneHutSpot s;
        s.plot = v;
        const vec2 at = g.fine.pos[v];
        s.deep = std::max(0.0f,std::min(1.0f,(swamp.radius - (at - swamp.centre).length()) / (swamp.radius * ZONE_HUT_DEEP)));
        s.camp = (camp >= 0 && camp < n) ? (at - g.fine.pos[camp]).length() : -1.0f;
        float edge = std::min(std::min(at.x - g.bounds_min.x,g.bounds_max.x - at.x),
                              std::min(at.y - g.bounds_min.y,g.bounds_max.y - at.y));
        const char* why = NULL;
        if (!HutIsBuildable(w,none,v,&why)){
            s.refused = why;
        }else if (edge < ZONE_HUT_EDGE_LEAST){
            s.refused = "too near the map's edge";
        }else if (s.camp >= 0.0f && s.camp < ZONE_HUT_CAMP_LEAST){
            s.refused = "too near the camp";
        }else if (!clearing(v)){
            s.refused = "a tree beside it";
        }
        if (s.refused){
            out.push_back(s);
            continue;
        }
        //The pools round it: the share of the plots within ZONE_HUT_WATER_NEAR that are wet.
        int m = ++marks;
        queue.assign(1,v);
        mark[v] = m;
        int wet = 0;
        for (size_t i = 0; i < queue.size(); i++){
            wet += t.wet[queue[i]] ? 1 : 0;
            int nb[8];
            int k = ZonePlotNeighbours(w,queue[i],nb,8);
            for (int j = 0; j < k; j++){
                if (mark[nb[j]] != m && (g.fine.pos[nb[j]] - at).length() <= ZONE_HUT_WATER_NEAR){
                    mark[nb[j]] = m;
                    queue.push_back(nb[j]);
                }
            }
        }
        s.water = (float)wet / (float)queue.size();
        float by_water = (s.water <= ZONE_HUT_WATER_BEST) ? s.water / ZONE_HUT_WATER_BEST
                                                         : std::max(0.0f,1.0f - (s.water - ZONE_HUT_WATER_BEST) / (1.0f - ZONE_HUT_WATER_BEST));
        int door = ZoneHutDoor(w,v);
        s.f_walk = door >= 0 && reach[door];
        s.score = s.deep + by_water + ZONE_HUT_W_JITTER * HutJitter(g.settings.seed,v);
        out.push_back(s);
    }
}

void ZoneSwampHutPlots(const ChasmWorld& w, int camp, std::vector<int>& out){
    out.clear();
    std::vector<ZoneHutSpot> spots;
    ZoneSwampHutSpots(w,camp,spots);
    std::vector<const ZoneHutSpot*> order;
    for (const ZoneHutSpot& s : spots){
        if (!s.refused){
            order.push_back(&s);
        }
    }
    std::sort(order.begin(),order.end(),[](const ZoneHutSpot* a, const ZoneHutSpot* b){
        if (a->f_walk != b->f_walk){
            return a->f_walk;
        }
        return (a->score != b->score) ? a->score > b->score : a->plot < b->plot;
    });
    const Grid& g = *w.grid;
    for (const ZoneHutSpot* s : order){
        if ((int)out.size() >= ZONE_SWAMP_HUTS){
            break;
        }
        bool f_alone = true;
        for (int o : out){
            f_alone = f_alone && (g.fine.pos[o] - g.fine.pos[s->plot]).length() >= ZONE_HUT_APART;
        }
        if (f_alone){
            out.push_back(s->plot);
        }
    }
}

int Zones::PlaceSwampHuts(){
    if (!state.world){
        return 0;
    }
    const ChasmWorld& w = *state.world;
    //The start camp's first plot: the huts keep their distance from the colony.
    int camp = -1;
    for (size_t v = 0; v < state.building.size() && camp < 0; v++){
        uint32_t id = state.building[v];
        if (id && id < state.buildings.size() && state.buildings[id].kind == ZONE_KIND_CAMP){
            camp = (int)v;
        }
    }
    std::vector<int> plots;
    ZoneSwampHutPlots(w,camp,plots);
    //Stroke numbers no drag reaches, as the camp's; a hut is one plot, so each is a building of its own anyway.
    const uint32_t HUT_STROKE = 0x7FFF00u;
    int placed = 0;
    for (size_t i = 0; i < plots.size(); i++){
        placed += Apply(ZONE_OP_BUILD_PAINT,(uint32_t)plots[i],ZONE_KIND_HUT,HUT_STROKE + (uint32_t)i) ? 1 : 0;
    }
    state.stroke = 0;
    state.stroke_building = 0;
    last_refusal.clear();
    return placed;
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
    state.portcullis.assign(g.fine.pos.size(),0);
    state.ground_owner.assign(g.fine.pos.size(),0);
    state.castle.assign(g.fine.pos.size(),0);
    state.castle_over.assign(g.fine.pos.size(),0);
    state.castle_base.assign(g.fine.pos.size(),0);
    state.castle_up.assign(g.fine.pos.size(),-1);
    state.castle_stroke = 0;
    state.castle_stroke_base = 0;
    state.castle_stroke_last = -1;
    state.f_castle_stroke_climb = false;
    state.terrace.assign(g.fine.pos.size(),ZONE_NO_TERRACE);
    state.terrace_built.assign(g.fine.pos.size(),0);
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
    RefreshArches(false);
}

void Zones::Adopt(const ZoneState& published){
    if (state.world != published.world || coarse_across.empty()){
        Reset(published.world);
    }
    state = published;
    last_refusal.clear();
    last_arch_lost = 0;
}

#define ZONE_PITCH_STROKE   0x7FFFFEu   //a stroke number no drag reaches: a pitched tent joins no building by it

bool Zones::PitchTent(int plot, uint32_t camp){
    uint32_t stroke = state.stroke;
    uint32_t building = state.stroke_building;
    bool f_done = Apply(ZONE_OP_BUILD_PAINT,(uint32_t)plot,ZONE_KIND_CAMP,ZONE_PITCH_STROKE,false,camp);
    state.stroke = stroke;
    state.stroke_building = building;
    return f_done;
}

//A fine quad's look changed: its chunk has to be rebuilt.
void Zones::Touch(int fine_quad){
    int c = TerrainChunkOfQuad(*state.world->grid,*state.world->mesh,fine_quad);
    state.chunk_version[c]++;
}

void Zones::RefreshStairs(int v){
    const ChasmWorld& w = *state.world;
    const GridPicker& p = *w.picker;
    //One form leans on its neighbours' - a landing's deck is a deck to come down onto - so round again until none
    //changes; a few rounds at most, as a stair is a few plots.
    for (int round = 0; round < 4; round++){
        bool f_changed = false;
        auto refresh = [&](int u){
            int shape = ZoneCastleShape(state,u);
            if (!ZoneCastleStairForm(shape)){
                return;
            }
            int form = ZoneStairForm(w,state,u);
            if (form == shape){
                return;
            }
            state.castle[u] = ZoneCastlePiece(form,ZoneCastleStoreys(state,u));
            f_changed = true;
            //Its cells and the ones round them: its neighbours' sides meet it.
            for (int i = 0; i < p.PlotQuadCount(u); i++){
                const GridQuad& q = w.grid->fine.quads[p.PlotQuadCorner(u,i) / 4];
                for (int k = 0; k < 4; k++){
                    for (int j = 0; j < p.PlotQuadCount(q.v[k]); j++){
                        Touch(p.PlotQuadCorner(q.v[k],j) / 4);
                    }
                }
            }
        };
        if (v < 0){
            for (int u = 0; u < (int)state.castle.size(); u++){
                refresh(u);
            }
        }else{
            refresh(v);
            int nb[8];
            int n = ZonePlotNeighbours(w,v,nb,8);
            for (int i = 0; i < n; i++){
                refresh(nb[i]);
                int nb2[8];
                int n2 = ZonePlotNeighbours(w,nb[i],nb2,8);
                for (int j = 0; j < n2; j++){
                    refresh(nb2[j]);
                }
            }
        }
        if (!f_changed){
            break;
        }
    }
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
    if (!f_field && state.buildings[id].kind == ZONE_KIND_CAMP){
        return;     //a camp's tents need not touch (THE CAMP, Zones.h): one camp, however they stand
    }
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
        state.buildings[fresh].f_stopped = state.buildings[id].f_stopped;      //a stopped workplace's pieces stay stopped
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
    last_arch_lost = 0;
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
            if (kind == ZONE_KIND_WELL && f_play){
                last_refusal = "a well is a debug tool, never a player's (water_tools_plan.md)";
                return false;
            }
            if (ZoneKindWild(kind) && f_play){
                last_refusal = "a swamp hut is never the colony's to build";
                return false;
            }
            if (plot < 0 || plot >= (int)state.storeys.size()){
                last_refusal = "no such plot";
                return false;
            }
            uint32_t on = state.building[plot];
            if (on && f_play && ZoneKindWild(state.buildings[on].kind)){
                last_refusal = "a swamp hut stands there";
                return false;
            }
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
            //new one - a stroke that jumps a gap, or has filled its building (ZoneKindMaxPlots), goes on with the next.
            //A winch is a plot of its own: a stroke of them makes one each.
            //A building named in the command is extended or nothing: a plot that cannot join it is refused.
            if (owner && !ZoneCanExtend(w,state,plot,kind,owner,&why)){
                last_refusal = why;
                return false;
            }
            uint32_t joining = owner ? owner : ZoneKindOnePlot(kind) ? 0 : stroke_target(kind);
            //A diagonal step brings the plot at the cell's corner between first, so the building stays one piece.
            //...but a camp's tents need not touch (THE CAMP, Zones.h): no corner between, and a stroke of them goes on.
            bool f_apart = kind == ZONE_KIND_CAMP;
            int bridge = (joining && !f_apart && !plot_touches(plot,joining)) ? ZoneJoinCorner(w,state,plot,kind,joining) : -1;
            int room = (bridge >= 0) ? 2 : 1;
            if (!owner && joining && ((!f_apart && !plot_touches(plot,joining) && bridge < 0) ||
                            (ZoneKindMaxPlots(kind) && state.buildings[joining].size + room > ZoneKindMaxPlots(kind)))){
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
            //A guard tower's ladder, its door: the open side nearest the camp (ZoneCanBuild saw there is one).
            if (kind == ZONE_KIND_TOWER){
                state.buildings[joining].ladder = ZoneTowerDefaultLadder(w,state,plot);
            }
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
            //A swamp hut is the swamp's (ZoneKindWild): a player's erase brush passes over it. Debug may take one off.
            if (f_play && ZoneKindWild(state.buildings[id].kind)){
                last_refusal = "the swamp hut is not the colony's to take down";
                return false;
            }
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
        case ZONE_OP_RANGE_PAINT:
        case ZONE_OP_DOCK_PAINT:{
            //A range is painted as a field is, on the same cells and by the same rules (range_plan.md); a dock by
            //its own (ZoneCanDock, steam_plan.md step 5), and all of it on one ground.
            int cell_kind = ZoneCellOpKind(op);
            auto can = [&](int cell, const char** reason){
                return (cell_kind == ZONE_KIND_DOCK) ? ZoneCanDock(w,state,cell,reason) : ZoneCanField(w,state,cell,reason);
            };
            int c = (int)index;
            if (c >= 0 && c < (int)state.field.size() && state.field[c]){
                if (stroke != state.stroke){
                    state.stroke = stroke;
                    state.stroke_building = state.field[c];
                }
                return false;
            }
            uint32_t joining = stroke_target(cell_kind);
            //Full (ZoneKindMaxPlots), the stroke goes on with a new one, as a house's does.
            int most = ZoneKindMaxPlots(cell_kind);
            if (joining && most && state.buildings[joining].size >= most){
                joining = 0;
            }
            //A diagonal step, as for buildings: through a cell beside both, the first that can be a field.
            if (joining && !cell_touches(c,joining)){
                int bridge = -1;
                for (int k = 0; k < 4 && bridge < 0; k++){
                    int n = coarse_across[c * 4 + k];
                    if (n >= 0 && !state.field[n] && cell_touches(n,joining) && can(n,NULL)){
                        bridge = n;
                    }
                }
                if (bridge >= 0 && (!most || state.buildings[joining].size + 2 <= most) && can(c,NULL)){
                    state.field[bridge] = joining;
                    state.field_built[bridge] = f_play ? 0 : 1;
                    state.buildings[joining].size++;
                    state.field_cells++;
                    touch_field(bridge);
                }else{
                    joining = 0;
                }
            }
            if (!can(c,&why)){
                last_refusal = why;
                return false;
            }
            //A dock across a cliff from the rest of it is no dock: a zeppelin lies on one floor.
            if (joining && cell_kind == ZONE_KIND_DOCK){
                for (size_t o = 0; o < state.field.size(); o++){
                    if (state.field[o] == joining){
                        if (!w.terrain->SameGround(w.grid->fine.quads[c * 4].v[0],w.grid->fine.quads[o * 4].v[0])){
                            last_refusal = "across a cliff from the rest of the dock";
                            return false;
                        }
                        break;
                    }
                }
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
        case ZONE_OP_SITE_CANCEL:
        case ZONE_OP_FIELD_CANCEL:{
            /*
                A SITE TAKEN OFF WHOLE (the user's, 2026-10-16): a card's Cancel, so a bridge - placed whole - goes the
                same way rather than plot by plot. Only while nothing of it stands: what is built is pulled down by the
                eraser. What it was brought is left where it stood, as a crate (Economy::Derive).
            */
            bool f_cells = op == ZONE_OP_FIELD_CANCEL;
            int c = (int)index;
            uint32_t id = f_cells ? ((c >= 0 && c < (int)state.field.size()) ? state.field[c] : 0)
                                  : ((plot >= 0 && plot < (int)state.building.size()) ? state.building[plot] : 0);
            if (!id){
                last_refusal = f_cells ? "no field there" : "nothing built there";
                return false;
            }
            for (size_t v = 0; v < state.building.size(); v++){
                if (state.building[v] == id && state.standing[v] > 0){
                    last_refusal = "part of it stands";
                    return false;
                }
            }
            for (size_t q = 0; q < state.field.size(); q++){
                if (state.field[q] == id && state.field_built[q]){
                    last_refusal = "part of it stands";
                    return false;
                }
            }
            for (size_t v = 0; v < state.building.size(); v++){
                if (state.building[v] == id){
                    state.storeys[v] = 0;
                    state.building[v] = 0;
                    state.built_plots--;
                    touch_road((int)v);
                }
            }
            for (size_t q = 0; q < state.field.size(); q++){
                if (state.field[q] == id){
                    state.field[q] = 0;
                    state.field_cells--;
                    touch_field((int)q);
                }
            }
            state.buildings[id].size = 0;
            state.buildings[id].kind = ZONE_KIND_NONE;    //gone; its id is never handed out again
            for (size_t v = 0; v < state.ground_owner.size(); v++){
                if (state.ground_owner[v] == id){
                    state.ground_owner[v] = 0;
                    touch_plot((int)v);
                }
            }
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
        case ZONE_OP_RANGE_STOP:{
            //A range's "stop work" (stopping_work_plan.md), as a workplace's by a plot of it - by a cell.
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c]
                || !ZoneKindStoppable(state.buildings[state.field[c]].kind)){
                last_refusal = "no range there";
                return false;
            }
            uint32_t id = state.field[c];
            uint8_t want = kind ? 1 : 0;
            if (state.buildings[id].f_stopped == want){
                return false;
            }
            state.buildings[id].f_stopped = want;
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
            //An ARCH's next floor over its road (arches_plan.md step 3): its owner's builders' too.
            if (plot >= 0 && plot < (int)state.building.size() && !state.building[plot]
                && state.ArchBuiltOf(plot) < ZoneArchFloors(state.ArchOf(plot))){
                state.arch_built[plot]++;
                touch_road(plot);
                break;
            }
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
            //Not where a tower's ladder stands - a gate there could be shut on it (defence_plan.md).
            if (kind == ZONE_GROUND_WALL && ZoneIsLadderPlot(w,state,plot)){
                last_refusal = "a tower's ladder stands there";
                return false;
            }
            //Where a wall and a road cross the plot stays road, the wall's gate (Zones.h, WALLS): a wall
            //drawn over a road leaves it, and gates it.
            if (kind == ZONE_GROUND_WALL && state.ground[plot] == ZONE_GROUND_ROAD){
                if (state.gate[plot]){
                    return false;
                }
                /*
                    ...but a player's never under an arch with a floor built (arches_plan.md step 4, the user's): no arch
                    stands over a gate, so the gate would take it down - floors paid for, and a house's places. The wall
                    has a gap there until a gatehouse; taking the arch down first (a storey off a side) closes it.
                */
                if (f_play && state.ArchBuiltOf(plot) > 0){
                    last_refusal = "an arch stands over the road";
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
            //...and through a castle's wall (castle_plan.md): a block two storeys or more becomes its GATE, its doors and
            //portcullis open; a one-storey one is taken, a gap in the wall - as is one the ground rises into past its
            //level, which leaves no passage (ZoneCanCastle's gate).
            if (kind == ZONE_GROUND_ROAD && ZoneCastleShape(state,plot) == ZONE_CASTLE_BLOCK){
                int storeys = ZoneCastleStoreys(state,plot);
                float lowest = 0.0f;
                float highest = 0.0f;
                ZonePlotGroundRange(w,plot,lowest,highest);
                bool f_room = highest <= ZoneCastleBase(state,plot) * ZONE_STOREY_HEIGHT + ZONE_CASTLE_SETTLE + 1e-3f;
                if (storeys >= ZONE_CASTLE_ARCH_LOWEST && f_room){
                    state.castle[plot] = ZoneCastlePiece(ZONE_CASTLE_GATE,storeys);
                    state.gate[plot] = ZONE_GATE_OPEN;
                }else{
                    state.castle[plot] = 0;
                }
                state.portcullis[plot] = 0;
                touch_ring(plot);
            }
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
                if (ZoneCastleGate(state,plot)){
                    last_refusal = "a castle's gate - the castle's tool takes it";
                    return false;
                }
                state.gate[plot] = 0;
                touch_road(plot);
                break;
            }
            //A castle gate's road taken up (castle_plan.md): the wall closes over it again, a block.
            if (ZoneCastleGate(state,plot)){
                state.castle[plot] = ZoneCastlePiece(ZONE_CASTLE_BLOCK,ZoneCastleStoreys(state,plot));
                state.portcullis[plot] = 0;
                touch_ring(plot);
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
        case ZONE_OP_GROUND_BREAK:{
            /*
                A palisade plot the zombies broke (zombies_plan.md step 4): back to a wall SITE - planned, staked out,
                open ground - as a wall still to build is, so the line's other plots end against a gap and the line
                works build it again. Only one that stands.
            */
            if (plot < 0 || plot >= (int)state.ground.size() || !ZoneWallStands(state,plot)){
                last_refusal = "no standing wall there";
                return false;
            }
            state.ground_built[plot] = 0;
            touch_road(plot);
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
        case ZONE_OP_WORK_STOP:{
            /*
                The player's "stop work" on a workplace (stopping_work_plan.md, the user's 2026-10-12) - a collector
                with water plenty in store, say. Nothing to draw: the economy takes its places away, and its card
                says so.
            */
            if (plot < 0 || plot >= (int)state.building.size() || state.building[plot] == 0
                || !ZoneKindStoppable(state.buildings[state.building[plot]].kind)){
                last_refusal = "no workplace there to stop";
                return false;
            }
            uint32_t id = state.building[plot];
            uint8_t want = kind ? 1 : 0;
            if (state.buildings[id].f_stopped == want){
                return false;
            }
            state.buildings[id].f_stopped = want;
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
        case ZONE_OP_PORTCULLIS:{
            /*
                A castle gate's PORTCULLIS let down, or raised (castle_plan.md) - the player's, from the gate's card, apart
                from its doors (ZONE_OP_GATE_SHUT). A castle gate joined to this one goes with it, as a gate's door does.
            */
            if (plot < 0 || plot >= (int)state.portcullis.size() || !ZoneCastleGate(state,plot) || !state.gate[plot]){
                last_refusal = "no castle gate there";
                return false;
            }
            uint8_t want = kind ? 1 : 0;
            if (state.portcullis[plot] == want){
                return false;
            }
            std::vector<int> todo(1,plot);
            while (!todo.empty()){
                int v = todo.back();
                todo.pop_back();
                if (state.portcullis[v] == want){
                    continue;
                }
                state.portcullis[v] = want;
                touch_road(v);      //its portcullis drawn again
                int nb[8];
                int n = ZonePlotNeighbours(w,v,nb,8);
                for (int i = 0; i < n; i++){
                    if (ZoneCastleGate(state,nb[i]) && state.gate[nb[i]] && state.portcullis[nb[i]] != want){
                        todo.push_back(nb[i]);
                    }
                }
            }
            break;
        }
        case ZONE_OP_TOWER_LADDER:{
            /*
                A guard tower's LADDER moved (defence_plan.md) - the player's, from its card: toward the neighbour
                named, or with -1 to the next open side round it, counter-clockwise from where it stands. It is the
                tower's door, so the walkers' way on and off moves with it.
            */
            uint32_t id = (plot >= 0 && plot < (int)state.building.size()) ? state.building[plot] : 0;
            if (!id || state.buildings[id].kind != ZONE_KIND_TOWER){
                last_refusal = "no tower there";
                return false;
            }
            ZoneBuilding& b = state.buildings[id];
            int to = kind;
            if (to < 0){
                std::vector<std::pair<float,int>> open;
                int nb[8];
                int n = ZonePlotNeighbours(w,plot,nb,8);
                vec2 p = w.grid->fine.pos[plot];
                for (int i = 0; i < n; i++){
                    if (ZoneTowerLadderOk(w,state,plot,nb[i])){
                        vec2 d = w.grid->fine.pos[nb[i]] - p;
                        open.push_back(std::make_pair(std::atan2(d.y,d.x),nb[i]));
                    }
                }
                std::sort(open.begin(),open.end());
                for (size_t i = 0; i < open.size() && to < 0; i++){
                    if (open[i].second == b.ladder){
                        to = open[(i + 1) % open.size()].second;
                    }
                }
                if (to < 0 && !open.empty()){
                    to = open.front().second;
                }
            }
            if (to == b.ladder){
                last_refusal = "no other open side for its ladder";
                return false;
            }
            if (!ZoneTowerLadderOk(w,state,plot,to)){
                last_refusal = "its ladder cannot stand there";
                return false;
            }
            b.ladder = to;
            touch_road(plot);       //the tower drawn again, and the plots round it
            break;
        }
        case ZONE_OP_CASTLE:{
            //The debug castle (ramparts_plan.md): a prototype, so never a player's.
            if (f_play){
                last_refusal = "the castle is a debug prototype - not in play";
                return false;
            }
            if (plot < 0 || plot >= (int)state.castle.size()){
                last_refusal = "no such plot";
                return false;
            }
            int piece = std::max(0,kind) & 0xFF;
            int was = ZoneCastleShape(state,plot);
            /*
                A STAIR (castle_plan.md "The stairs"): its drag says where it goes and how high (ZonePlanStair) - the
                storeys asked for are not used - and its form comes of what is round it once it is down (RefreshStairs).
            */
            if (piece != 0 && ZoneCastleStairForm(piece >> 4)){
                ZoneStairStep st;
                if (!ZonePlanStair(w,state,plot,stroke,st)){
                    if (!st.f_quiet){
                        last_refusal = st.why ? st.why : "";
                    }
                    return false;
                }
                if (st.start >= 0){
                    //Started from a deck: nothing on the map yet, and no level until its first stair.
                    state.castle_stroke = stroke;
                    state.castle_stroke_base = ZONE_CASTLE_NO_BASE;
                    state.castle_stroke_last = st.start;
                    state.f_castle_stroke_climb = false;
                    return false;
                }
                int last = (stroke != 0 && stroke == state.castle_stroke) ? state.castle_stroke_last : -1;
                if (st.landing >= 0){
                    state.castle[st.landing] = ZoneCastlePiece(ZoneCastleShape(state,st.landing),st.landing_storeys);
                    touch_ring(st.landing);
                }
                if (st.relink >= 0){
                    state.castle_up[st.relink] = plot;
                    touch_ring(st.relink);
                }
                uint8_t stair = ZoneCastlePiece(ZONE_CASTLE_STAIR,st.storeys);
                if (st.f_over){
                    state.castle_over[plot] = stair;
                }else{
                    state.castle[plot] = stair;
                    state.castle_base[plot] = (int8_t)st.base;
                }
                state.castle_up[plot] = st.up;
                //The drag's level is its first stair's - one over an arch keeps the drag's, standing on the arch's.
                bool f_level = last >= 0 && state.castle_stroke_base != ZONE_CASTLE_NO_BASE;
                state.castle_stroke_base = (st.f_over && f_level) ? state.castle_stroke_base : st.base;
                state.castle_stroke = stroke;
                state.castle_stroke_last = plot;
                state.f_castle_stroke_climb = st.f_climb;
                touch_ring(plot);
                if (last >= 0){
                    touch_ring(last);
                    RefreshStairs(last);
                }
                RefreshStairs(plot);
                break;
            }
            //A stair is taken off from its foot up: the flights below it first.
            bool f_stair_off = state.castle_over[plot] ? ZoneCastleOverSits(state,plot) : ZoneHasStair(state,plot);
            int stair_up = f_stair_off ? ZoneStairUp(state,plot) : -1;
            if (piece == 0 && f_stair_off){
                int downs[8];
                if (ZoneStairDowns(w,state,plot,downs,8) > 0){
                    last_refusal = "the stair below it comes off first - a stair is taken off from its foot up";
                    return false;
                }
            }
            if (piece == 0){
                //From the top down: a piece over a piece first.
                if (state.castle_over[plot]){
                    state.castle_over[plot] = 0;
                }else if (state.castle[plot]){
                    state.castle[plot] = 0;
                    //A gate taken off its road: no gate there any more, its doors and portcullis with it.
                    if (was == ZONE_CASTLE_GATE){
                        state.gate[plot] = 0;
                        state.portcullis[plot] = 0;
                    }
                }else{
                    last_refusal = "no castle piece there";
                    return false;
                }
            }else{
                int shape = piece >> 4;
                int storeys = piece & 15;
                //A wall dragged across a road (castle_plan.md): its GATE there, two storeys or more - one storey leaves
                //the road open, a gap in the wall.
                if (shape == ZONE_CASTLE_BLOCK && state.ground[plot] == ZONE_GROUND_ROAD
                    && !ZoneCastleGoesOver(state,plot,shape,storeys)){
                    if (storeys < ZONE_CASTLE_ARCH_LOWEST){
                        last_refusal = "a one-storey wall leaves the road open - a gate takes two storeys";
                        return false;
                    }
                    shape = ZONE_CASTLE_GATE;
                    piece = ZoneCastlePiece(shape,storeys);
                }
                bool f_over = ZoneCastleGoesOver(state,plot,shape,storeys);
                //Its LEVEL (ZoneCastleLevelFor): the drag's, so a wall over a hill is level - one over a piece its.
                int base = f_over ? state.castle_base[plot] : ZoneCastleLevelFor(w,state,plot,stroke);
                if ((f_over ? state.castle_over[plot] : state.castle[plot]) == piece && (f_over || state.castle_base[plot] == base)){
                    return false;   //the same piece again: nothing to do, as a drag over its own plots
                }
                if (!f_over && state.castle_over[plot] && state.castle_base[plot] != base){
                    last_refusal = "a piece stands over it, at its own level";
                    return false;
                }
                //Over the piece there, or in its place - swapped for it as the piece over it allows; a stair's only with
                //no stair below it, as it would be taken off.
                if (!ZoneCanCastle(w,state,plot,shape,storeys,base,&why)){
                    last_refusal = why;
                    return false;
                }
                int downs[8];
                if (!f_over && f_stair_off && ZoneStairDowns(w,state,plot,downs,8) > 0){
                    last_refusal = "the stair below it comes off first - a stair is taken off from its foot up";
                    return false;
                }
                if (f_over){
                    state.castle_over[plot] = (uint8_t)piece;
                }else{
                    state.castle[plot] = (uint8_t)piece;
                    state.castle_base[plot] = (int8_t)base;
                    state.castle_up[plot] = -1;
                    //A gate is a gate on its road (ZoneState::gate, its doors), open as it is made; any other piece in
                    //its place takes it away, and its portcullis.
                    if (shape == ZONE_CASTLE_GATE){
                        if (!state.gate[plot]){
                            state.gate[plot] = ZONE_GATE_OPEN;
                        }
                    }else if (was == ZONE_CASTLE_GATE){
                        state.gate[plot] = 0;
                        state.portcullis[plot] = 0;
                    }
                }
                if (stroke != 0){
                    state.castle_stroke = stroke;
                    state.castle_stroke_base = base;
                    state.castle_stroke_last = -1;
                }
            }
            if (piece == 0 && f_stair_off){
                state.castle_up[plot] = -1;
                if (stair_up >= 0){
                    touch_ring(stair_up);
                    RefreshStairs(stair_up);
                }
            }
            touch_ring(plot);       //a stair's slope meets its neighbours, so they are drawn again too
            RefreshStairs(plot);    //...and the stairs round it may take another form: a deck to come down onto, or not
            break;
        }
        /*
            A TERRACE (terraces_plan.md stage 2): its top in `kind`, a whole number of storeys - the drag's, worked out over
            all of it (ZoneTerraceTopOver), so a patch is one platform. A play command's is a site until the builders
            raise it; a debug one stands at once. Its walls are drawn from both sides of an edge, so the ring round it
            is drawn again.
        */
        case ZONE_OP_TERRACE:{
            if (plot < 0 || plot >= (int)state.terrace.size()){
                last_refusal = "no such plot";
                return false;
            }
            if (ZoneTerraced(state,plot) && state.terrace[plot] == kind){
                return false;       //the same terrace again: a drag back over its own plots
            }
            if (kind <= INT8_MIN || kind > INT8_MAX){
                last_refusal = "no such height";
                return false;
            }
            if (!ZoneCanTerrace(w,state,plot,kind,&why)){
                last_refusal = why;
                return false;
            }
            state.terrace[plot] = (int8_t)kind;
            state.terrace_built[plot] = f_play ? 0 : 1;
            touch_ring(plot);
            break;
        }
        case ZONE_OP_TERRACE_ERASE:{
            if (!ZoneTerraced(state,plot)){
                last_refusal = "no terrace there";
                return false;
            }
            //Nothing on it: what stands there goes first, as the eraser takes it (PlanDrag).
            bool f_field = false;
            for (int i = 0; i < p.PlotQuadCount(plot); i++){
                int parent = w.grid->fine.quads[p.PlotQuadCorner(plot,i) / 4].parent;
                f_field = f_field || (parent >= 0 && parent < (int)state.field.size() && state.field[parent]);
            }
            if (state.storeys[plot] > 0 || state.ground[plot] != ZONE_GROUND_NONE || f_field){
                last_refusal = "something stands on the terrace - it goes first";
                return false;
            }
            state.terrace[plot] = ZONE_NO_TERRACE;
            state.terrace_built[plot] = 0;
            touch_ring(plot);
            break;
        }
        case ZONE_OP_TERRACE_RAISE:{
            if (!ZoneTerraced(state,plot) || state.terrace_built[plot]){
                last_refusal = "no terrace site there";
                return false;
            }
            state.terrace_built[plot] = 1;
            touch_ring(plot);
            break;
        }
        case ZONE_OP_TOWER_STONE:{
            //A guard tower built of stone (ramparts_plan.md) - the castle prototype's, so not yet a player's.
            if (f_play){
                last_refusal = "the stone tower is a debug prototype - not in play";
                return false;
            }
            uint32_t id = (plot >= 0 && plot < (int)state.building.size()) ? state.building[plot] : 0;
            if (!id || state.buildings[id].kind != ZONE_KIND_TOWER){
                last_refusal = "no tower there";
                return false;
            }
            //Its height (castle_plan.md): 2 to 4 storeys of stone, as asked, or as it is for a bare 1 - a wooden one's
            //own again. Debug places it standing whole, so what stands goes with it.
            uint8_t want = (kind != 0) ? 1 : 0;
            int storeys = !want ? ZoneKindMaxStoreys(ZONE_KIND_TOWER)
                        : (kind >= ZONE_STONE_TOWER_LEAST) ? std::min(kind,ZONE_STONE_TOWER_MOST) : state.storeys[plot];
            if (state.buildings[id].f_stone == want && state.storeys[plot] == storeys){
                last_refusal = want ? "it is stone already" : "it is wood already";
                return false;
            }
            bool f_whole = state.standing[plot] >= state.storeys[plot];
            state.buildings[id].f_stone = want;
            state.storeys[plot] = (uint8_t)storeys;
            state.standing[plot] = (uint8_t)(f_whole ? storeys : std::min((int)state.standing[plot],storeys));
            touch_road(plot);       //the tower drawn again, and the walls ending on it - and the castle round it
            break;
        }
        default:
            last_refusal = "unknown zone op";
            return false;
    }
    //The builders' raises are no placement: an arch they bring about is a site, as a storey a player adds is.
    last_arch_lost = RefreshArches(!f_play && op != ZONE_OP_BUILD_RAISE && op != ZONE_OP_GROUND_RAISE
                                   && op != ZONE_OP_FIELD_RAISE && op != ZONE_OP_GROUND_BREAK);
    if (op == ZONE_OP_BUILD_ADD || op == ZONE_OP_BUILD_REMOVE || op == ZONE_OP_BUILD_ERASE || op == ZONE_OP_BUILD_RAISE
        || op == ZONE_OP_FIELD_ERASE || op == ZONE_OP_FIELD_RAISE || op == ZONE_OP_SITE_CANCEL || op == ZONE_OP_FIELD_CANCEL){
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
        //An arch's floors still to build are its owner's (arches_plan.md step 3).
        if (state.ArchOwnerOf((int)v) && state.ArchBuiltOf((int)v) < ZoneArchFloors(state.ArchOf((int)v))){
            open[state.ArchOwnerOf((int)v)] = 1;
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

int Zones::RestoreCastle(const std::vector<ZoneSavedCastle>& pieces){
    //Each at its saved level, as a drag of its own that took it - a stroke no paint command uses; an older save's
    //where a piece painted there now goes.
    const uint32_t RESTORE_STROKE = 0xFFFFFFFFu;
    int refused = 0;
    for (const ZoneSavedCastle& pc : pieces){
        /*
            A STAIR (castle_plan.md "The stairs") as it was left, its height and the stair it climbs to: a drag laid
            it, which a save has not got - on open ground, or a flight over an arch a storey under it. Its form is
            worked out again once all are back.
        */
        if (ZoneCastleStairForm(pc.piece >> 4)){
            int plot = pc.plot;
            int storeys = pc.piece & 15;
            bool f_ok = plot >= 0 && plot < (int)state.castle.size() && storeys >= 1 && storeys <= ZONE_CASTLE_MAX_STOREYS
                        && !ZoneTerraced(state,plot) && state.storeys[plot] == 0;
            bool f_over = f_ok && ZoneCastleShape(state,plot) == ZONE_CASTLE_ARCH && !state.castle_over[plot]
                          && storeys == ZoneCastleStoreys(state,plot) + 1;
            if (!f_ok || (!f_over && ZoneCastleShape(state,plot) != ZONE_CASTLE_NONE)){
                refused++;
                continue;
            }
            if (f_over){
                state.castle_over[plot] = (uint8_t)ZoneCastlePiece(ZONE_CASTLE_STAIR,storeys);
            }else{
                state.castle[plot] = (uint8_t)ZoneCastlePiece(ZONE_CASTLE_STAIR,storeys);
                state.castle_base[plot] = (int8_t)((pc.base != ZONE_CASTLE_NO_BASE) ? pc.base
                                                                                    : ZoneCastleLevelFor(*state.world,state,plot,0));
            }
            state.castle_up[plot] = (pc.up >= 0 && pc.up < (int)state.castle.size()) ? pc.up : -1;
            continue;
        }
        uint32_t stroke = 0;
        if (pc.base != ZONE_CASTLE_NO_BASE){
            state.castle_stroke = RESTORE_STROKE;
            state.castle_stroke_base = pc.base;
            stroke = RESTORE_STROKE;
        }
        if (!Apply(ZONE_OP_CASTLE,(uint32_t)pc.plot,pc.piece,stroke,false)){
            refused++;
        }else if (pc.f_portcullis && ZoneCastleGate(state,pc.plot)){
            state.portcullis[pc.plot] = 1;      //a gate's portcullis as it was left (castle_plan.md)
        }
    }
    state.castle_stroke = 0;
    state.castle_stroke_last = -1;
    RefreshStairs(-1);
    return refused;
}

int Zones::Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<ZoneSavedBuilding>& buildings,
                   uint32_t next_id, const std::vector<ZoneSavedGround>& grounds,
                   uint32_t stroke, uint32_t stroke_building, const std::vector<ZoneSavedTerrace>& terraces){
    Reset(world);
    if (!world){
        return 0;
    }
    const ChasmWorld& w = *world;
    int refused = 0;
    //The terraces before anything that stands on them - each through the rules, a site as a play paint leaves it.
    for (const ZoneSavedTerrace& tr : terraces){
        if (!Apply(ZONE_OP_TERRACE,(uint32_t)tr.plot,tr.top,0,!tr.f_built)){
            refused++;
        }
    }
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
        state.buildings[b.id].f_stopped = (b.f_stopped && ZoneKindStoppable(b.kind)) ? 1 : 0;
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
            //A stone tower as high as it was (castle_plan.md): its stone comes back with its ladder, below.
            int most = (b.kind == ZONE_KIND_TOWER && b.f_stone) ? ZONE_STONE_TOWER_MOST : ZoneKindMaxStoreys(b.kind);
            state.storeys[v] = (uint8_t)std::max(1,std::min(most,b.plots[i].second));
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
    //A guard tower's ladder as saved, once everything round it is back; one from before ladders, or one that can no
    //longer stand where it was, takes its default.
    for (const ZoneSavedBuilding& b : buildings){
        if (b.kind != ZONE_KIND_TOWER || b.id == 0 || state.buildings[b.id].kind != ZONE_KIND_TOWER || b.plots.empty()){
            continue;
        }
        int v = b.plots[0].first;
        state.buildings[b.id].ladder = ZoneTowerLadderOk(w,state,v,b.ladder) ? b.ladder : ZoneTowerDefaultLadder(w,state,v);
        state.buildings[b.id].f_stone = b.f_stone ? 1 : 0;
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
    //Every arch's floors as built (arches_plan.md step 3), on its road plot - kept to what may stand now.
    state.arch_built.resize(state.building.size(),0);
    for (const ZoneSavedGround& gr : grounds){
        if (gr.arch > 0 && gr.plot >= 0 && gr.plot < (int)state.arch_built.size()){
            state.arch_built[gr.plot] = (uint8_t)std::min(gr.arch,255);
        }
    }
    //Everything is new: every chunk is drawn again.
    for (uint32_t& v : state.chunk_version){
        v++;
    }
    RefreshArches(false);
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
            b.f_stopped = state.buildings[id].f_stopped != 0;
            b.next_crop = state.buildings[id].next_crop;
            b.land_of = state.buildings[id].land_of;
            b.ladder = state.buildings[id].ladder;
            b.f_stone = state.buildings[id].f_stone != 0;
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
    info.f_stopped = z.buildings[id].f_stopped != 0;
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
        }else if (z.ArchOwnerOf((int)v) == id){
            //Its ARCHES' floors over their passages (arches_plan.md): its room and its places, as its own storeys -
            //those built; the rest its site's.
            int built = z.ArchBuiltOf((int)v);
            int site = ZoneArchFloors(z.ArchOf((int)v)) - built;
            info.storeys += built;
            info.floor_area += w.picker->PlotArea((int)v) * built;
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
        if (z.storeys[v] > ZoneBuildingMaxStoreys(z,(int)v) || !ZoneCanBuild(w,probe,(int)v,kind,join,&why)){
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
        and each within its kind's size (ZoneKindMaxPlots).
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
    int too_big = 0;
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
        if (ZoneKindMaxPlots(b.kind) && b.size > ZoneKindMaxPlots(b.kind)){
            too_big++;
        }
        //One piece: a flood from its first element reaches all of it - but a camp, whose tents need not touch.
        if (b.kind == ZONE_KIND_CAMP){
            continue;
        }
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
             "%i miscounted, %i in pieces, %i too big",live,z.built_plots,z.field_cells,z.grounds,bad,broken,
             in_pieces,too_big);
    r.f_pass = (bad == 0 && broken == 0 && in_pieces == 0 && too_big == 0);
    r.detail = buf;
    report.results.push_back(r);
    if (!r.f_pass){
        report.f_pass = false;
    }
}
#endif

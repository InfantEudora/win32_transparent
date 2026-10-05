#include "Zones.h"
#include "ChasmWorld.h"

#include <algorithm>
#include <cstdio>

const char* ZoneOpName(int op){
    switch (op){
        case ZONE_OP_HOUSE_PAINT:   return "house_paint";
        case ZONE_OP_HOUSE_ADD:     return "house_add";
        case ZONE_OP_HOUSE_REMOVE:  return "house_remove";
        case ZONE_OP_HOUSE_ERASE:   return "house_erase";
        case ZONE_OP_FIELD_PAINT:   return "field_paint";
        case ZONE_OP_FIELD_ERASE:   return "field_erase";
        case ZONE_OP_GROUND_PAINT:  return "ground_paint";
        case ZONE_OP_GROUND_ERASE:  return "ground_erase";
        default:                    return "none";
    }
}

const char* ZoneGroundName(int ground){
    switch (ground){
        case ZONE_GROUND_GARDEN:    return "garden";
        case ZONE_GROUND_TOWN:      return "town";
        case ZONE_GROUND_ROAD:      return "road";
        default:                    return "none";
    }
}

int ZoneBoundaryBetween(const ZoneState& z, int a, int b){
    bool in_a = z.storeys[a] > 0 || ZoneEnclosesGround(z.ground[a]);
    bool in_b = z.storeys[b] > 0 || ZoneEnclosesGround(z.ground[b]);
    if (in_a == in_b){
        return ZONE_BOUNDARY_NONE;
    }
    int inside = in_a ? a : b;
    if (z.storeys[inside] > 0){
        return ZONE_BOUNDARY_NONE;     //a house's own wall bounds it
    }
    return (z.ground[inside] == ZONE_GROUND_GARDEN) ? ZONE_BOUNDARY_GARDEN_WALL : ZONE_BOUNDARY_PALISADE;
}

namespace {

//Plot v's neighbours along fine edges, each once, in the plot quads' fixed order. Returns how many.
int PlotNeighbours(const ChasmWorld& w, int v, int* out, int max){
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

#define ZONE_GATE_AHEAD     0.34f   //cos of the most a gate may be off the road's line: about 70 degrees

}

int ZoneGateOf(const ChasmWorld& w, const ZoneState& z, int road){
    if (z.ground[road] != ZONE_GROUND_ROAD){
        return -1;
    }
    int nb[8];
    int n = PlotNeighbours(w,road,nb,8);
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

//A road plot with two road neighbours and every other neighbour a house of two storeys or more: what
//could be bridged. The storeys it would be bridged to, or 0. Its road neighbours in `roads_out`.
int ArchCandidate(const ChasmWorld& w, const ZoneState& z, int plot, int* roads_out){
    if (z.ground[plot] != ZONE_GROUND_ROAD){
        return 0;
    }
    int nb[8];
    int n = PlotNeighbours(w,plot,nb,8);
    int roads = 0;
    int top = ZONE_MAX_STOREYS;
    for (int i = 0; i < n; i++){
        int u = nb[i];
        if (z.ground[u] == ZONE_GROUND_ROAD){
            if (roads < 2){
                roads_out[roads] = u;
            }
            roads++;
        }else if (z.storeys[u] >= 2){
            top = std::min(top,(int)z.storeys[u]);
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
    What a house and ground both need of their plot: flat all round it - every corner of every fine
    cell round the plot on the plot's level, so nothing stands half over a cliff - and no field
    touching it.
*/
static bool PlotIsBuildable(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
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
    int level = w.terrain->level[plot];
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        int q = p.PlotQuadCorner(plot,i) / 4;
        const GridQuad& quad = g.fine.quads[q];
        for (int k = 0; k < 4; k++){
            if (w.terrain->level[quad.v[k]] != level){
                return refuse("too close to a cliff");
            }
            if (w.terrain->wet[quad.v[k]]){
                return refuse("too close to a river");
            }
            if (w.terrain->Mountain(quad.v[k])){
                return refuse("on the mountain");
            }
        }
        if (!z.field.empty() && z.field[quad.parent]){
            return refuse("a field is there");
        }
    }
    return true;
}

bool ZoneCanHouse(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
    if (!PlotIsBuildable(w,z,plot,why)){
        return false;
    }
    if (!z.ground.empty() && z.ground[plot] == ZONE_GROUND_ROAD){
        if (why){
            *why = "a road is there";
        }
        return false;
    }
    return true;
}

bool ZoneCanGround(const ChasmWorld& w, const ZoneState& z, int plot, const char** why, int kind){
    if (!PlotIsBuildable(w,z,plot,why)){
        return false;
    }
    if (kind == ZONE_GROUND_ROAD && !z.storeys.empty() && z.storeys[plot] > 0){
        if (why){
            *why = "a house is there";
        }
        return false;
    }
    /*
        A road stops at a garden's or a town's edge rather than eating into it - so a road dragged into
        a walled garden ends against the wall, which is exactly where a gate opens (Zones.h). Erase the
        ground first to run a road through.
    */
    if (kind == ZONE_GROUND_ROAD && !z.ground.empty() && ZoneEnclosesGround(z.ground[plot])){
        if (why){
            *why = "a garden or town is there";
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
    int level = -1;
    for (int q = coarse * 4; q < coarse * 4 + 4; q++){
        for (int k = 0; k < 4; k++){
            int v = g.fine.quads[q].v[k];
            if (level < 0){
                level = w.terrain->level[v];
            }else if (w.terrain->level[v] != level){
                return refuse("not flat - it crosses a cliff");
            }
            if (w.terrain->wet[v]){
                return refuse("too close to a river");
            }
            if (w.terrain->Mountain(v)){
                return refuse("on the mountain");
            }
            if (!z.storeys.empty() && z.storeys[v] > 0){
                return refuse("a house is there");
            }
            if (!z.ground.empty() && z.ground[v] != ZONE_GROUND_NONE){
                return refuse("a garden or town is there");
            }
        }
    }
    return true;
}

void Zones::Reset(std::shared_ptr<const ChasmWorld> world){
    state = ZoneState();
    state.world = world;
    if (!world){
        return;
    }
    state.storeys.assign(world->grid->fine.pos.size(),0);
    state.ground.assign(world->grid->fine.pos.size(),ZONE_GROUND_NONE);
    state.field.assign(world->grid->coarse.quads.size(),0);
    state.chunk_version.assign(world->mesh->chunks.size(),0);
    last_refusal.clear();
}

//A fine quad's look changed: its chunk has to be rebuilt.
void Zones::Touch(int fine_quad){
    int c = TerrainChunkOfQuad(*state.world->grid,*state.world->mesh,fine_quad);
    state.chunk_version[c]++;
}

bool Zones::Apply(int op, uint32_t index, int kind){
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
        meet there (RoadMesh.h), a gate by where the road ends, an arch by the houses either side - and
        moves the forest's clearance a cell out: every quad round every plot that shares a cell with it.
        Any ground or house changing does the same: a garden's wall beside a road's end makes a gate,
        a house beside a road makes or unmakes an arch over it.
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
    //a house going up can unmake the arch two plots along.
    auto touch_road = [&](int v){
        for (int i = 0; i < p.PlotQuadCount(v); i++){
            const GridQuad& q = w.grid->fine.quads[p.PlotQuadCorner(v,i) / 4];
            for (int k = 0; k < 4; k++){
                touch_ring(q.v[k]);
            }
        }
    };
    int plot = (int)index;
    switch (op){
        case ZONE_OP_HOUSE_PAINT:
        case ZONE_OP_HOUSE_ADD:{
            if (plot < 0 || plot >= (int)state.storeys.size()){
                last_refusal = "no such plot";
                return false;
            }
            uint8_t& s = state.storeys[plot];
            if (s == 0){
                if (!ZoneCanHouse(w,state,plot,&why)){
                    last_refusal = why;
                    return false;
                }
                s = 1;
                state.houses++;
            }else if (op == ZONE_OP_HOUSE_PAINT){
                return false;   //a drag does not stack - not a refusal, just nothing to do
            }else if (s >= ZONE_MAX_STOREYS){
                last_refusal = "at the most storeys a house may have";
                return false;
            }else{
                s++;
            }
            touch_road(plot);     //an arch over a road beside it may come or go
            break;
        }
        case ZONE_OP_HOUSE_REMOVE:
        case ZONE_OP_HOUSE_ERASE:{
            if (plot < 0 || plot >= (int)state.storeys.size() || state.storeys[plot] == 0){
                last_refusal = "no house there";
                return false;
            }
            uint8_t& s = state.storeys[plot];
            s = (op == ZONE_OP_HOUSE_ERASE) ? 0 : (uint8_t)(s - 1);
            if (s == 0){
                state.houses--;
            }
            touch_road(plot);     //an arch over a road beside it may come or go
            break;
        }
        case ZONE_OP_FIELD_PAINT:{
            int c = (int)index;
            if (c >= 0 && c < (int)state.field.size() && state.field[c]){
                return false;
            }
            if (!ZoneCanField(w,state,c,&why)){
                last_refusal = why;
                return false;
            }
            state.field[c] = 1;
            state.fields++;
            touch_field(c);
            break;
        }
        case ZONE_OP_FIELD_ERASE:{
            int c = (int)index;
            if (c < 0 || c >= (int)state.field.size() || !state.field[c]){
                last_refusal = "no field there";
                return false;
            }
            state.field[c] = 0;
            state.fields--;
            touch_field(c);
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
            touch_road(plot);       //a road's ends, gates and the forest's clearance - see touch_road
            break;
        }
        case ZONE_OP_GROUND_ERASE:{
            if (plot < 0 || plot >= (int)state.ground.size() || state.ground[plot] == ZONE_GROUND_NONE){
                last_refusal = "no garden or town there";
                return false;
            }
            state.ground[plot] = ZONE_GROUND_NONE;
            state.grounds--;
            touch_road(plot);
            break;
        }
        default:
            last_refusal = "unknown zone op";
            return false;
    }
    state.version++;
    return true;
}

int Zones::Restore(std::shared_ptr<const ChasmWorld> world,
                   const std::vector<std::pair<int,int>>& houses, const std::vector<int>& fields,
                   const std::vector<std::pair<int,int>>& grounds){
    Reset(world);
    int refused = 0;
    for (int c : fields){
        if (!Apply(ZONE_OP_FIELD_PAINT,(uint32_t)c)){
            refused++;
        }
    }
    for (const auto& gr : grounds){
        if (!Apply(ZONE_OP_GROUND_PAINT,(uint32_t)gr.first,gr.second)){
            refused++;
        }
    }
    for (const auto& h : houses){
        bool f_ok = true;
        for (int s = 0; s < h.second && f_ok; s++){
            f_ok = Apply(ZONE_OP_HOUSE_ADD,(uint32_t)h.first);
        }
        if (!f_ok){
            refused++;
        }
    }
    last_refusal.clear();
    return refused;
}

ZoneStats ComputeZoneStats(const ChasmWorld& w, const ZoneState& z){
    ZoneStats s;
    for (size_t v = 0; v < z.storeys.size(); v++){
        if (z.storeys[v]){
            s.houses++;
            s.storeys += z.storeys[v];
            s.house_floor_area += w.picker->PlotArea((int)v) * z.storeys[v];
        }
    }
    for (size_t c = 0; c < z.field.size(); c++){
        if (z.field[c]){
            s.fields++;
            s.field_area += w.picker->CoarseArea((int)c);
        }
    }
    for (size_t v = 0; v < z.ground.size(); v++){
        if (z.ground[v] == ZONE_GROUND_GARDEN){
            s.gardens++;
        }else if (z.ground[v] == ZONE_GROUND_TOWN){
            s.towns++;
        }else if (z.ground[v] == ZONE_GROUND_ROAD){
            s.roads++;
        }
        if (ZoneEnclosesGround(z.ground[v])){
            s.ground_area += w.picker->PlotArea((int)v);
        }
    }
    return s;
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
    //Every house and field must still be one its command would have allowed. Each is asked with
    //itself taken out, since a house never forbids itself.
    int bad = 0;
    ZoneState probe = z;
    for (size_t v = 0; v < z.storeys.size(); v++){
        if (!z.storeys[v]){
            continue;
        }
        probe.storeys[v] = 0;
        const char* why = NULL;
        if (z.storeys[v] > ZONE_MAX_STOREYS || !ZoneCanHouse(w,probe,(int)v,&why)){
            bad++;
            if (report.issues.size() < 200){
                GridIssue issue;
                issue.kind = "zone house";
                issue.index = (int)v;
                issue.where = w.grid->fine.pos[v];
                report.issues.push_back(issue);
            }
        }
        probe.storeys[v] = z.storeys[v];
    }
    for (size_t v = 0; v < z.ground.size(); v++){
        if (!z.ground[v]){
            continue;
        }
        probe.ground[v] = ZONE_GROUND_NONE;
        if (z.ground[v] >= ZONE_GROUND_COUNT || !ZoneCanGround(w,probe,(int)v,NULL,z.ground[v])){
            bad++;
            if (report.issues.size() < 200){
                GridIssue issue;
                issue.kind = "zone ground";
                issue.index = (int)v;
                issue.where = w.grid->fine.pos[v];
                report.issues.push_back(issue);
            }
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
            if (report.issues.size() < 200){
                GridIssue issue;
                issue.kind = "zone field";
                issue.index = (int)c;
                issue.where = w.grid->FineQuadCentre((int)c * 4 + 2);
                report.issues.push_back(issue);
            }
        }
        probe.field[c] = z.field[c];
    }
    snprintf(buf,sizeof(buf),"%i houses, %i ground plots, %i fields; %i break a rule",z.houses,z.grounds,z.fields,bad);
    r.f_pass = (bad == 0);
    r.detail = buf;
    report.results.push_back(r);
    if (!r.f_pass){
        report.f_pass = false;
    }
}
#endif

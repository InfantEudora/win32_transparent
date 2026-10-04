#include "Zones.h"
#include "ChasmWorld.h"

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
        default:                    return "none";
    }
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
        }
        if (!z.field.empty() && z.field[quad.parent]){
            return refuse("a field is there");
        }
    }
    return true;
}

bool ZoneCanHouse(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
    return PlotIsBuildable(w,z,plot,why);
}

bool ZoneCanGround(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
    return PlotIsBuildable(w,z,plot,why);
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
            touch_plot(plot);
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
            touch_plot(plot);
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
            if (!ZoneCanGround(w,state,plot,&why)){
                last_refusal = why;
                return false;
            }
            if (state.ground[plot] == ZONE_GROUND_NONE){
                state.grounds++;
            }
            state.ground[plot] = (uint8_t)kind;
            touch_plot(plot);
            break;
        }
        case ZONE_OP_GROUND_ERASE:{
            if (plot < 0 || plot >= (int)state.ground.size() || state.ground[plot] == ZONE_GROUND_NONE){
                last_refusal = "no garden or town there";
                return false;
            }
            state.ground[plot] = ZONE_GROUND_NONE;
            state.grounds--;
            touch_plot(plot);
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
        }
        if (z.ground[v]){
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
        if (z.ground[v] >= ZONE_GROUND_COUNT || !ZoneCanGround(w,probe,(int)v,NULL)){
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

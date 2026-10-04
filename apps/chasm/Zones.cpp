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
        default:                    return "none";
    }
}

bool ZoneCanHouse(const ChasmWorld& w, const ZoneState& z, int plot, const char** why){
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
    state.field.assign(world->grid->coarse.quads.size(),0);
    state.chunk_version.assign(world->mesh->chunks.size(),0);
    last_refusal.clear();
}

//A fine quad's look changed: its chunk has to be rebuilt.
void Zones::Touch(int fine_quad){
    int c = TerrainChunkOfQuad(*state.world->grid,*state.world->mesh,fine_quad);
    state.chunk_version[c]++;
}

bool Zones::Apply(int op, uint32_t index){
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
            for (int q = c * 4; q < c * 4 + 4; q++){
                Touch(q);
            }
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
            for (int q = c * 4; q < c * 4 + 4; q++){
                Touch(q);
            }
            break;
        }
        default:
            last_refusal = "unknown zone op";
            return false;
    }
    state.version++;
    return true;
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
    snprintf(buf,sizeof(buf),"%i houses, %i fields; %i break a rule",z.houses,z.fields,bad);
    r.f_pass = (bad == 0);
    r.detail = buf;
    report.results.push_back(r);
    if (!r.f_pass){
        report.f_pass = false;
    }
}
#endif

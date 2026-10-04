#include "Terrain.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

/*
    The heights. The plateau is the ground everything is measured from; the floor is deep enough
    that the walls read as many storeys (a fine cell, about a house, is 2 units across), and a shard
    stands about a third of the way down.
*/
const TerrainLevel terrain_levels[TERRAIN_NUM_LEVELS] = {
    {"plateau",   0.0f},
    {"shard",   -24.0f},
    {"floor",   -70.0f},
};

namespace {

bool InPolygon(const vec2& pt, const std::vector<vec2>& p){
    bool f_in = false;
    size_t n = p.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++){
        if ((p[i].y > pt.y) != (p[j].y > pt.y)){
            float x = p[j].x + (pt.y - p[j].y) * (p[i].x - p[j].x) / (p[i].y - p[j].y);
            if (pt.x < x){
                f_in = !f_in;
            }
        }
    }
    return f_in;
}

}

//The smoothed lines the grid pinned its chains to, not the settings' corner points - the chains
//lie exactly on these, so the levels split exactly along them.
std::vector<GridLine> TerrainFeatureLines(const Grid& g){
    if ((int)g.lines.size() <= g.feature_line_base){
        return std::vector<GridLine>();
    }
    return std::vector<GridLine>(g.lines.begin() + g.feature_line_base,g.lines.end());
}

void Terrain::Build(const Grid& g, const std::vector<GridLine>& features){
    auto t0 = std::chrono::steady_clock::now();
    size_t n = g.fine.pos.size();
    level.assign(n,TERRAIN_PLATEAU);
    for (int i = 0; i < TERRAIN_NUM_LEVELS; i++){
        level_count[i] = 0;
    }
    /*
        The rim is open, both ends on the south edge. The chasm polygon closes it a little PAST that
        edge rather than along it: a vertex on the map's south edge lies exactly on a closing line
        drawn along the edge, where the inside test can go either way - and did, building a cliff
        straight across the chasm's mouth. Every later feature is a shard, already closed.
    */
    std::vector<vec2> chasm;
    if (!features.empty() && features[0].points.size() >= 2){
        chasm = features[0].points;
        float past = (g.bounds_max.y - g.bounds_min.y) * 0.05f;
        vec2 first = chasm.front();
        vec2 last = chasm.back();
        //Each end pushed straight out through the edge it sits on - the south one, today.
        float dir_last = (last.y > (g.bounds_min.y + g.bounds_max.y) * 0.5f) ? 1.0f : -1.0f;
        float dir_first = (first.y > (g.bounds_min.y + g.bounds_max.y) * 0.5f) ? 1.0f : -1.0f;
        chasm.push_back(vec2(last.x,last.y + past * dir_last));
        chasm.push_back(vec2(first.x,first.y + past * dir_first));
    }
    for (size_t v = 0; v < n; v++){
        const vec2& p = g.fine.pos[v];
        int pin = g.fine.pin[v] - g.feature_line_base;
        uint8_t l = TERRAIN_PLATEAU;
        //Fixed vertices are the map's corners and the rim's two ends - plateau, both, and the rim's
        //ends sit exactly on the chasm polygon, where the inside test can go either way.
        if (g.fine.pin[v] == GRID_PIN_FIXED){
            l = TERRAIN_PLATEAU;
        }else if (pin == 0){
            l = TERRAIN_PLATEAU;
        }else if (pin >= 1){
            l = TERRAIN_SHARD;
        }else if (!chasm.empty() && InPolygon(p,chasm)){
            l = TERRAIN_FLOOR;
            for (size_t f = 1; f < features.size(); f++){
                if (InPolygon(p,features[f].points)){
                    l = TERRAIN_SHARD;
                    break;
                }
            }
        }
        level[v] = l;
        level_count[l]++;
    }
    build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

#ifdef DEBUG
void RunTerrainChecks(const Grid& g, const Terrain& t, const std::vector<GridLine>& features,
                      GridCheckReport& report){
    char buf[256];
    auto add_result = [&report](const char* name, bool f_pass, const std::string& detail){
        GridCheckResult r;
        r.name = name;
        r.f_pass = f_pass;
        r.detail = detail;
        report.results.push_back(r);
        if (!f_pass){
            report.f_pass = false;
        }
    };
    auto add_issue = [&report](const char* kind, int index, vec2 where, bool f_failure){
        if (report.issues.size() < 200){
            GridIssue issue;
            issue.kind = kind;
            issue.index = index;
            issue.where = where;
            issue.f_failure = f_failure;
            report.issues.push_back(issue);
        }
    };

    //--- Levels: every level present that the features call for -------------------------------
    {
        bool f_ok = t.level_count[TERRAIN_PLATEAU] > 0 &&
                    (features.empty() || t.level_count[TERRAIN_FLOOR] > 0) &&
                    (features.size() < 2 || t.level_count[TERRAIN_SHARD] > 0);
        snprintf(buf,sizeof(buf),"plateau %i, shard %i, floor %i vertices (%.1f ms)",
                 t.level_count[TERRAIN_PLATEAU],t.level_count[TERRAIN_SHARD],t.level_count[TERRAIN_FLOOR],t.build_ms);
        add_result("levels",f_ok,buf);
    }

    /*
        --- Steps: no cell spans more than two levels ------------------------------------------
        A cell's cliff is cut between its high corners and its low ones, and the mesh assumes the
        low ones share a height. A plateau corner and a floor corner and a shard corner in one cell
        means a shard touches the rim - which the features should never draw.
    */
    {
        int bad = 0;
        for (int q = 0; q < (int)g.fine.quads.size(); q++){
            bool seen[TERRAIN_NUM_LEVELS] = {};
            int distinct = 0;
            for (int k = 0; k < 4; k++){
                int l = t.level[g.fine.quads[q].v[k]];
                if (!seen[l]){
                    seen[l] = true;
                    distinct++;
                }
            }
            if (distinct > 2){
                bad++;
                add_issue("steps",q,g.FineQuadCentre(q),true);
            }
        }
        snprintf(buf,sizeof(buf),"%i cells span three levels",bad);
        add_result("steps",bad == 0,buf);
    }

    /*
        --- Pins agree: a vertex pinned to a feature stands on that feature's high side --------
        Only meaningful once the grid pins its features; before that nothing is pinned to them and
        this passes trivially - the detail says which.
    */
    {
        int pinned = 0;
        int wrong = 0;
        for (size_t v = 0; v < g.fine.pos.size(); v++){
            int f = g.fine.pin[v] - g.feature_line_base;
            if (f < 0){
                continue;
            }
            pinned++;
            int want = (f == 0) ? TERRAIN_PLATEAU : TERRAIN_SHARD;
            if (t.level[v] != want){
                wrong++;
                add_issue("pin level",(int)v,g.fine.pos[v],true);
            }
        }
        if (pinned == 0){
            add_result("pin levels",true,"no vertex is pinned to a feature yet - nothing to compare");
            report.results.back().f_skipped = true;
        }else{
            snprintf(buf,sizeof(buf),"%i of %i feature-pinned vertices on the wrong level",wrong,pinned);
            add_result("pin levels",wrong == 0,buf);
        }
    }
}
#endif

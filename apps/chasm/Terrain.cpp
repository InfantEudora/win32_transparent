#include "Terrain.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

/*
    The even-odd point-in-polygon test, for a hundred thousand points against a polygon of hundreds
    of edges - which, testing every edge, took 43 ms. Only an edge that straddles the point's z can
    be crossed, so the edges are bucketed into bands of z and a point tests just its own band's.
    Each edge's test is the plain one, unchanged, and the answer is the parity of the crossings,
    which does not depend on the order they are counted in - so it answers exactly what testing
    every edge would (checked vertex by vertex against that on seeds 1-3 when it was written).

    A band holds every edge whose z range touches it. Mapping z to a band subtracts and divides,
    both of which keep order, so an edge that straddles a point's z is always in that point's band.
*/
class CrossingTable{
public:
    explicit CrossingTable(const std::vector<vec2>& polygon) : p(polygon){
        size_t n = p.size();
        if (n < 3){
            return;
        }
        z_min = z_max = p[0].y;
        for (const vec2& q : p){
            z_min = std::min(z_min,q.y);
            z_max = std::max(z_max,q.y);
        }
        int bands = std::max(1,(int)n / 2);
        band_size = (z_max - z_min) / (float)bands;
        if (band_size <= 0.0f){
            return;
        }
        first.assign(bands + 1,0);
        auto band_of = [&](float z){ return std::max(0,std::min(bands - 1,(int)((z - z_min) / band_size))); };
        //Counted, then filled, so the edges sit in one array in band order.
        for (int pass = 0; pass < 2; pass++){
            std::vector<int> at;
            if (pass == 1){
                for (int b = 0; b < bands; b++){
                    first[b + 1] += first[b];
                }
                edges.resize(first[bands]);
                at.assign(first.begin(),first.end() - 1);
            }
            for (size_t i = 0, j = n - 1; i < n; j = i++){
                int b0 = band_of(std::min(p[i].y,p[j].y));
                int b1 = band_of(std::max(p[i].y,p[j].y));
                for (int b = b0; b <= b1; b++){
                    if (pass == 0){
                        first[b + 1]++;
                    }else{
                        edges[at[b]++] = (int)i;
                    }
                }
            }
        }
    }

    bool Inside(const vec2& pt) const{
        if (first.empty() || pt.y < z_min || pt.y > z_max){
            return false;   //no edge straddles it
        }
        int bands = (int)first.size() - 1;
        int b = std::max(0,std::min(bands - 1,(int)((pt.y - z_min) / band_size)));
        bool f_in = false;
        size_t n = p.size();
        for (int e = first[b]; e < first[b + 1]; e++){
            size_t i = (size_t)edges[e];
            size_t j = i == 0 ? n - 1 : i - 1;
            if ((p[i].y > pt.y) != (p[j].y > pt.y)){
                float x = p[j].x + (pt.y - p[j].y) * (p[i].x - p[j].x) / (p[i].y - p[j].y);
                if (pt.x < x){
                    f_in = !f_in;
                }
            }
        }
        return f_in;
    }

private:
    const std::vector<vec2>& p;
    float z_min = 0.0f;
    float z_max = 0.0f;
    float band_size = 0.0f;
    std::vector<int> first;     //band b's edges are edges[first[b] .. first[b + 1])
    std::vector<int> edges;     //each by the index of its second end; its first is the point before
};

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
    CrossingTable chasm_table(chasm);
    std::vector<CrossingTable> shard_tables;
    shard_tables.reserve(features.size());
    for (size_t f = 1; f < features.size(); f++){
        shard_tables.emplace_back(features[f].points);
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
        }else if (!chasm.empty() && chasm_table.Inside(p)){
            l = TERRAIN_FLOOR;
            for (const CrossingTable& shard : shard_tables){
                if (shard.Inside(p)){
                    l = TERRAIN_SHARD;
                    break;
                }
            }
        }
        level[v] = l;
        level_count[l]++;
    }
    BuildRivers(g,features);
    build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

//--- Rivers --------------------------------------------------------------------------------------

/*
    Three rivers, each from the map's edge across the plateau to the rim - two from the west, one
    from the east - wandering a little either side of a straight course. The last point of each is
    a little PAST the rim, inside the chasm, so the channel runs right through the lip; the line
    crossing the rim is where the fall is.
*/
std::vector<TerrainRiver> ChasmDefaultRivers(){
    std::vector<TerrainRiver> r(3);
    r[0].points = {{0.000f,0.300f},{0.070f,0.318f},{0.140f,0.296f},{0.215f,0.338f},{0.290f,0.322f},
                   {0.360f,0.372f},{0.420f,0.392f},{0.480f,0.400f}};
    r[0].width = 6.5f;
    r[1].points = {{0.000f,0.735f},{0.080f,0.712f},{0.160f,0.748f},{0.245f,0.700f},{0.320f,0.722f},
                   {0.385f,0.690f},{0.420f,0.676f},{0.470f,0.668f}};
    r[1].width = 5.5f;
    r[2].points = {{1.000f,0.470f},{0.925f,0.492f},{0.850f,0.462f},{0.775f,0.510f},{0.700f,0.488f},
                   {0.630f,0.522f},{0.575f,0.515f},{0.525f,0.520f}};
    r[2].width = 7.0f;
    return r;
}

namespace {

//Chaikin's corner cutting, ends kept: a hand-placed line of a few points made into a smooth curve.
std::vector<vec2> Smooth(const std::vector<vec2>& in, int rounds){
    std::vector<vec2> p = in;
    for (int r = 0; r < rounds && p.size() >= 3; r++){
        std::vector<vec2> q;
        q.push_back(p.front());
        for (size_t i = 0; i + 1 < p.size(); i++){
            q.push_back(p[i] * 0.75f + p[i + 1] * 0.25f);
            q.push_back(p[i] * 0.25f + p[i + 1] * 0.75f);
        }
        q.push_back(p.back());
        p.swap(q);
    }
    return p;
}

//Where segments a0-a1 and b0-b1 cross, as the share of the way along a; false if they do not.
bool SegmentsCross(const vec2& a0, const vec2& a1, const vec2& b0, const vec2& b1, float& t){
    vec2 r = a1 - a0;
    vec2 s = b1 - b0;
    float d = r.x * s.y - r.y * s.x;
    if (std::fabs(d) < 1e-9f){
        return false;
    }
    vec2 w = b0 - a0;
    t = (w.x * s.y - w.y * s.x) / d;
    float u = (w.x * r.y - w.y * r.x) / d;
    return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
}

//Distance from p to segment a-b, with how far along it (0..1) the nearest point is.
float SegmentDistance(const vec2& p, const vec2& a, const vec2& b, float& t){
    vec2 ab = b - a;
    float len2 = ab.dot(ab);
    t = (len2 > 1e-12f) ? std::max(0.0f,std::min(1.0f,(p - a).dot(ab) / len2)) : 0.0f;
    return (p - (a + ab * t)).length();
}

}

void Terrain::BuildRivers(const Grid& g, const std::vector<GridLine>& features){
    rivers.clear();
    falls.clear();
    vec2 lo = g.bounds_min;
    vec2 size = g.bounds_max - g.bounds_min;
    for (const TerrainRiver& def : ChasmDefaultRivers()){
        TerrainRiver r = def;
        for (vec2& p : r.points){
            p = lo + vec2(p.x * size.x,p.y * size.y);
        }
        r.points = Smooth(r.points,3);
        r.length = 0.0f;
        for (size_t i = 0; i + 1 < r.points.size(); i++){
            r.length += (r.points[i + 1] - r.points[i]).length();
        }
        rivers.push_back(r);
    }

    //--- Falls: each river's first crossing of the rim ---------------------------------------------
    if (!features.empty()){
        const std::vector<vec2>& rim = features[0].points;
        for (int ri = 0; ri < (int)rivers.size(); ri++){
            const TerrainRiver& r = rivers[ri];
            float along = 0.0f;
            bool f_found = false;
            for (size_t i = 0; i + 1 < r.points.size() && !f_found; i++){
                vec2 a = r.points[i];
                vec2 b = r.points[i + 1];
                for (size_t j = 0; j + 1 < rim.size(); j++){
                    float t;
                    if (!SegmentsCross(a,b,rim[j],rim[j + 1],t)){
                        continue;
                    }
                    TerrainFall f;
                    f.river = ri;
                    f.lip = a + (b - a) * t;
                    f.width = r.width;
                    f.along = along + (b - a).length() * t;
                    vec2 tangent = rim[j + 1] - rim[j];
                    tangent.normalize();
                    f.out = vec2(-tangent.y,tangent.x);
                    if (f.out.dot(b - a) < 0.0f){
                        f.out = -f.out;
                    }
                    f.across = vec2(-f.out.y,f.out.x);
                    falls.push_back(f);
                    f_found = true;
                    break;
                }
                along += (b - a).length();
            }
        }
    }

    //--- The edge raster: distance past the nearest water edge -------------------------------------
    const float reach = TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN + 2.0f;
    edge_cell = 1.0f;
    edge_origin = lo;
    edge_w = (int)std::ceil(size.x / edge_cell) + 2;
    edge_h = (int)std::ceil(size.y / edge_cell) + 2;
    edge.assign((size_t)edge_w * edge_h,1e3f);
    for (const TerrainRiver& r : rivers){
        float half = r.width * 0.5f;
        for (size_t i = 0; i + 1 < r.points.size(); i++){
            vec2 a = r.points[i];
            vec2 b = r.points[i + 1];
            float pad = half + reach;
            int x0 = std::max(0,(int)std::floor((std::min(a.x,b.x) - pad - edge_origin.x) / edge_cell));
            int x1 = std::min(edge_w - 1,(int)std::ceil((std::max(a.x,b.x) + pad - edge_origin.x) / edge_cell));
            int z0 = std::max(0,(int)std::floor((std::min(a.y,b.y) - pad - edge_origin.y) / edge_cell));
            int z1 = std::min(edge_h - 1,(int)std::ceil((std::max(a.y,b.y) + pad - edge_origin.y) / edge_cell));
            for (int z = z0; z <= z1; z++){
                for (int x = x0; x <= x1; x++){
                    vec2 p = edge_origin + vec2(x * edge_cell,z * edge_cell);
                    float t;
                    float e = SegmentDistance(p,a,b,t) - half;
                    float& cell = edge[(size_t)z * edge_w + x];
                    cell = std::min(cell,e);
                }
            }
        }
    }

    wet.assign(g.fine.pos.size(),0);
    wet_count = 0;
    for (size_t v = 0; v < g.fine.pos.size(); v++){
        if (EdgeDistance(g.fine.pos[v]) < TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN){
            wet[v] = 1;
            wet_count++;
        }
    }
}

float Terrain::EdgeDistance(const vec2& p) const{
    if (edge.empty()){
        return 1e3f;
    }
    float fx = (p.x - edge_origin.x) / edge_cell;
    float fz = (p.y - edge_origin.y) / edge_cell;
    fx = std::max(0.0f,std::min((float)(edge_w - 1) - 0.001f,fx));
    fz = std::max(0.0f,std::min((float)(edge_h - 1) - 0.001f,fz));
    int ix = (int)fx;
    int iz = (int)fz;
    float tx = fx - ix;
    float tz = fz - iz;
    const float* row0 = &edge[(size_t)iz * edge_w + ix];
    const float* row1 = row0 + edge_w;
    float a = row0[0] + (row0[1] - row0[0]) * tx;
    float b = row1[0] + (row1[1] - row1[0]) * tx;
    return a + (b - a) * tz;
}

float Terrain::RiverDip(const vec2& p) const{
    float e = EdgeDistance(p);
    if (e >= TERRAIN_RIVER_BANK){
        return 0.0f;
    }
    //Full depth a bank's width inside the water's edge, half at the edge, none a bank's width out.
    float t = std::max(0.0f,std::min(1.0f,(TERRAIN_RIVER_BANK - e) / (2.0f * TERRAIN_RIVER_BANK)));
    return TERRAIN_RIVER_DEPTH * t * t * (3.0f - 2.0f * t);
}

bool Terrain::RiverCoords(const vec2& p, float reach, int& river, float& along, float& across) const{
    float best = 1e30f;
    river = -1;
    for (int ri = 0; ri < (int)rivers.size(); ri++){
        const TerrainRiver& r = rivers[ri];
        float run = 0.0f;
        for (size_t i = 0; i + 1 < r.points.size(); i++){
            vec2 a = r.points[i];
            vec2 b = r.points[i + 1];
            float len = (b - a).length();
            float t;
            float d = SegmentDistance(p,a,b,t);
            if (d < best && d - r.width * 0.5f < reach){
                best = d;
                river = ri;
                along = run + len * t;
                vec2 ab = b - a;
                float side = ab.x * (p.y - a.y) - ab.y * (p.x - a.x);
                across = ((side < 0.0f) ? -d : d) / (r.width * 0.5f);
            }
            run += len;
        }
    }
    return river >= 0;
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
        --- Rivers: each one reaches the rim, and goes over it from the plateau ------------------
        A river that never crosses the rim has no fall and ends in the grass; one whose lip is not
        on plateau ground pours from nowhere.
    */
    {
        int bad = 0;
        for (const TerrainFall& f : t.falls){
            vec2 back = f.lip - f.out * 3.0f;
            vec2 ahead = f.lip + f.out * 4.0f;
            //No level lookup by position here, so the nearest vertex stands in.
            auto nearest = [&](const vec2& p){
                int best = 0;
                float bd = 1e30f;
                for (size_t v = 0; v < g.fine.pos.size(); v++){
                    float d = (g.fine.pos[v] - p).length();
                    if (d < bd){
                        bd = d;
                        best = (int)v;
                    }
                }
                return best;
            };
            if (t.level[nearest(back)] != TERRAIN_PLATEAU || t.level[nearest(ahead)] != TERRAIN_FLOOR){
                bad++;
                add_issue("fall",f.river,f.lip,true);
            }
        }
        bool f_ok = (t.falls.size() == t.rivers.size()) && bad == 0;
        snprintf(buf,sizeof(buf),"%i rivers, %i falls, %i not from plateau to floor; %i wet vertices",
                 (int)t.rivers.size(),(int)t.falls.size(),bad,t.wet_count);
        add_result("rivers",f_ok,buf);
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

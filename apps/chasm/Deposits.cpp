#include "Deposits.h"

#include <algorithm>
#include <chrono>
#include "Goods.h"

const char* DepositKindName(int kind){
    switch (kind){
        case DEPOSIT_SEAM:      return "seam";
        case DEPOSIT_MOUNTAIN:  return "mountain";
        case DEPOSIT_OUTCROP:   return "outcrop";
    }
    return "?";
}

//A seam's plots face one wall: each plot's way to its wall within about 45 degrees of the first's.
#define SEAM_SAME_WALL      0.7f

namespace {

//splitmix64, as the forest's: the only source of chance here.
uint64_t Mix(uint64_t x){
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

//0..1 from (seed, salt, plot).
float Roll(uint32_t seed, uint32_t salt, int v){
    uint64_t h = Mix(((uint64_t)seed << 32) ^ ((uint64_t)salt << 8) ^ (uint64_t)(uint32_t)v);
    return (float)(h >> 40) / (float)(1ull << 24);
}

//The plots across plot v's grid edges: the corners next to v in each cell round it - where a wall face between
//two plots stands (TerrainMesh.cpp draws walls on cells' edges, never across their diagonals).
void EdgeNeighbours(const Grid& g, const GridPicker& p, int v, std::vector<int>& out){
    out.clear();
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        int qc = p.PlotQuadCorner(v,i);
        const GridQuad& q = g.fine.quads[qc / 4];
        int k = qc % 4;
        int next[2] = {q.v[(k + 1) % 4],q.v[(k + 3) % 4]};
        for (int n : next){
            if (std::find(out.begin(),out.end(),n) == out.end()){
                out.push_back(n);
            }
        }
    }
}

//The forest's PlotIsFlat: not the map's edge or a river's bank, and every corner of every cell round it on its ground.
bool Flat(const Grid& g, const GridPicker& p, const Terrain& t, int v){
    if (g.fine.f_boundary[v] || t.wet[v]){
        return false;
    }
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        const GridQuad& q = g.fine.quads[p.PlotQuadCorner(v,i) / 4];
        for (int k = 0; k < 4; k++){
            if (!t.SameGround(q.v[k],v)){
                return false;
            }
        }
    }
    return true;
}

//Whether any corner of a cell round plot v is the mountain's.
bool TouchesMountain(const Grid& g, const GridPicker& p, const Terrain& t, int v){
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        const GridQuad& q = g.fine.quads[p.PlotQuadCorner(v,i) / 4];
        for (int k = 0; k < 4; k++){
            if (t.Mountain(q.v[k])){
                return true;
            }
        }
    }
    return false;
}

vec2 Middle(const Grid& g, const std::vector<int>& plots){
    vec2 c(0.0f,0.0f);
    for (int v : plots){
        c += g.fine.pos[v];
    }
    return plots.empty() ? c : c * (1.0f / (float)plots.size());
}

}

vec2 DepositDigWay(const Grid& g, const GridPicker& picker, const Terrain& t, const DepositData& d, int v){
    int dep = d.At(v);
    if (dep < 0 || d.deposits[dep].kind == DEPOSIT_OUTCROP){
        return vec2(0.0f,0.0f);
    }
    bool f_seam = d.deposits[dep].kind == DEPOSIT_SEAM;
    int best = -1;
    float best_score = 0.0f;
    for (int i = 0; i < picker.PlotQuadCount(v); i++){
        const GridQuad& q = g.fine.quads[picker.PlotQuadCorner(v,i) / 4];
        for (int k = 0; k < 4; k++){
            int c = q.v[k];
            //A seam's wall: the nearest higher corner. The mountain: the corner standing highest over the plot.
            float score = f_seam ? ((t.steps[c] < t.steps[v]) ? -(g.fine.pos[c] - g.fine.pos[v]).length() : -1e30f)
                                 : (t.Mountain(c) ? t.ground[c] - t.ground[v] : -1e30f);
            if (score > -1e29f && (best < 0 || score > best_score || (score == best_score && c < best))){
                best = c;
                best_score = score;
            }
        }
    }
    if (best < 0){
        return vec2(0.0f,0.0f);
    }
    vec2 way = g.fine.pos[best] - g.fine.pos[v];
    float len = way.length();
    return (len > 1e-6f) ? way / len : vec2(0.0f,0.0f);
}

void BuildDeposits(const Grid& g, const GridPicker& picker, const Terrain& t, DepositData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = DepositData();
    const int n = (int)g.fine.pos.size();
    const uint32_t seed = g.settings.seed;
    out.of_plot.assign(n,-1);
    std::vector<int> around;

    /*
        THE MOUNTAIN, first, so it is always deposit 0: one deposit, its foot every plateau plot off the mountain and
        not on a river's bank with a corner of a cell round it on the mountain - along the plateau, and round each
        pocket's floor, where the crags stand right behind. What a mine there wants of the ground is step 5's.
    */
    Deposit mountain;
    mountain.kind = DEPOSIT_MOUNTAIN;
    mountain.good = GOOD_STONE;
    for (int v = 0; v < n; v++){
        if (t.level[v] == TERRAIN_PLATEAU && !t.Mountain(v) && !t.wet[v] && !g.fine.f_boundary[v] &&
            TouchesMountain(g,picker,t,v)){
            mountain.plots.push_back(v);
        }
    }
    if (!mountain.plots.empty()){
        mountain.centre = Middle(g,mountain.plots);
        for (int v : mountain.plots){
            out.of_plot[v] = (int)out.deposits.size();
        }
        out.deposits.push_back(mountain);
    }

    /*
        SEAMS. The FOOT of a balcony's wall: its plots with a higher plot across a grid edge, so a wall face stands over
        them. Per balcony, in the order of its lowest plot, a roll of its own says whether it holds a seam; if it does,
        the seam starts at the foot plot with the lowest roll of another salt and grows along the foot, a plot at a
        time, by the foot plot next to the run nearest its start - a few starts tried, for a foot broken into short
        pieces. Every roll is the balcony's or the plot's own, so no seam moves another.
        Along ONE wall: a plot joins only if its wall lies the way the start's does (SEAM_SAME_WALL). A run grown round
        a notch's corner had its band on two walls facing each other, and from any one side showed a plot's worth.
        Never under the mountain: nobody walks, builds or puts a crate down there, and four of seed 12's nine seams lay
        out of everyone's reach. `balconies` counts the balconies off it.
    */
    std::vector<std::vector<int>> foot(t.patch_size.size());
    std::vector<int> first_plot(t.patch_size.size(),-1);
    std::vector<uint8_t> is_foot(n,0);
    std::vector<vec2> facing(n,vec2(0.0f,0.0f));     //a foot plot's way to its wall, unit
    for (int v = 0; v < n; v++){
        if (t.level[v] != TERRAIN_BALCONY || t.Mountain(v) || t.wet[v]){
            continue;
        }
        int p = t.patch[v];
        if (first_plot[p] < 0){
            first_plot[p] = v;
            out.balconies++;
        }
        if (g.fine.f_boundary[v]){
            continue;
        }
        EdgeNeighbours(g,picker,v,around);
        vec2 way(0.0f,0.0f);
        for (int c : around){
            if (t.steps[c] < t.steps[v]){
                vec2 d = g.fine.pos[c] - g.fine.pos[v];
                way += d / std::max(1e-6f,d.length());
            }
        }
        if (way.length() > 0.5f){      //walls on opposite sides, as in a crack one plot across, lean no way
            foot[p].push_back(v);
            is_foot[v] = 1;
            facing[v] = way / way.length();
        }
    }
    for (size_t p = 0; p < foot.size(); p++){
        if ((int)foot[p].size() < DEPOSIT_SEAM_PLOTS || Roll(seed,0x7365616Du,first_plot[p]) >= DEPOSIT_SEAM_CHANCE){
            continue;
        }
        std::vector<int> starts = foot[p];
        std::sort(starts.begin(),starts.end(),[&](int a, int b){
            float ra = Roll(seed,0x7365616Eu,a);
            float rb = Roll(seed,0x7365616Eu,b);
            return ra < rb || (ra == rb && a < b);
        });
        std::vector<int> run;
        for (size_t s = 0; s < starts.size() && s < 8 && (int)run.size() < DEPOSIT_SEAM_PLOTS; s++){
            run.assign(1,starts[s]);
            const vec2& from = g.fine.pos[starts[s]];
            while ((int)run.size() < DEPOSIT_SEAM_PLOTS){
                int best = -1;
                float best_d = 0.0f;
                for (int r : run){
                    EdgeNeighbours(g,picker,r,around);
                    for (int c : around){
                        if (!is_foot[c] || t.patch[c] != (int)p || std::find(run.begin(),run.end(),c) != run.end() ||
                            facing[c].dot(facing[starts[s]]) < SEAM_SAME_WALL){
                            continue;
                        }
                        float d = (g.fine.pos[c] - from).length();
                        if (best < 0 || d < best_d || (d == best_d && c < best)){
                            best = c;
                            best_d = d;
                        }
                    }
                }
                if (best < 0){
                    break;
                }
                run.push_back(best);
            }
        }
        if ((int)run.size() < DEPOSIT_SEAM_PLOTS){
            continue;
        }
        Deposit seam;
        seam.kind = DEPOSIT_SEAM;
        seam.good = GOOD_FLOATSTONE;
        seam.holds = DEPOSIT_SEAM_FLOATSTONE;
        seam.plots = run;
        seam.balcony = (int)p;
        seam.band = (int)t.steps[run[0]] - 1;
        for (int r : run){
            EdgeNeighbours(g,picker,r,around);
            for (int c : around){
                if (t.steps[c] < t.steps[r] && std::find(seam.wall.begin(),seam.wall.end(),c) == seam.wall.end()){
                    seam.wall.push_back(c);
                }
            }
        }
        std::sort(seam.wall.begin(),seam.wall.end());
        seam.centre = Middle(g,run);
        for (int r : run){
            out.of_plot[r] = (int)out.deposits.size();
        }
        out.deposits.push_back(seam);
    }

    /*
        OUTCROPS: a pocket's flat plots, off its edge - nothing touching the mountain, which is the mountain's own foot -
        each by a roll of its own, and none nearer another than DEPOSIT_OUTCROP_APART, the earlier plot kept.
    */
    std::vector<int> outcrops;
    for (int v = 0; v < n; v++){
        if (t.biome[v] != TERRAIN_BIOME_POCKET || t.level[v] != TERRAIN_PLATEAU || Roll(seed,0x6F757463u,v) >= DEPOSIT_OUTCROP_CHANCE ||
            !Flat(g,picker,t,v) || TouchesMountain(g,picker,t,v)){
            continue;
        }
        bool f_near = false;
        for (int o : outcrops){
            f_near = f_near || (g.fine.pos[o] - g.fine.pos[v]).length() < DEPOSIT_OUTCROP_APART;
        }
        if (f_near){
            continue;
        }
        outcrops.push_back(v);
        Deposit outcrop;
        outcrop.kind = DEPOSIT_OUTCROP;
        outcrop.good = GOOD_STONE;
        outcrop.plots.assign(1,v);
        outcrop.centre = g.fine.pos[v];
        out.of_plot[v] = (int)out.deposits.size();
        out.deposits.push_back(outcrop);
    }
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

#include "Terrain.h"
#include "TerrainMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>

/*
    The heights. The plateau is the ground everything is measured from; the floor is deep enough
    that the walls read as many storeys (a fine cell, about a house, is 2 units across), an island
    stands about a third of the way down, and a balcony a winch's drop below the rim - well above the
    mist. Every one a whole number of the walls' strata (WALL_BAND_HEIGHT, 6) below the plateau, so
    the strata of a balcony's wall line up with those of the full wall beside it.
*/
//Names, and each kind's usual height - a plot's own is Terrain::Height.
const TerrainLevel terrain_levels[TERRAIN_NUM_LEVELS] = {
    {"plateau",   0.0f},
    {"island",  -24.0f},
    {"floor",   -72.0f},
    {"balcony", -12.0f},
    {"bare",      0.0f},
};

namespace {

/*
    WHICH SIDE OF A SET OF LINES A POINT IS ON, by a ray from it to the north (-z): an odd number of
    crossings is inside. The lines need not be closed - a rim ending on the south, west or east edge
    works as it is, because no rift reaches the north edge, so the ray always leaves the map through
    plateau. That replaced closing the rim polygon past the south edge, which needed to know which
    edge a rim's ends were on, and could not take two rims.

    A hundred thousand points against lines of hundreds of segments: testing every segment took
    43 ms, so the segments are bucketed into bands of x and a point tests only its own band's. Each
    segment's test is the plain one, and the answer is a parity, which does not depend on the order
    the crossings are counted in. A band holds every segment whose x range touches it; mapping x to
    a band subtracts and divides, both of which keep order, so a segment that straddles a point's x
    is always in that point's band.
*/
class CrossingTable{
public:
    void Add(const std::vector<vec2>& line){
        for (size_t i = 0; i + 1 < line.size(); i++){
            a.push_back(line[i]);
            b.push_back(line[i + 1]);
        }
    }

    void Build(){
        size_t n = a.size();
        if (n == 0){
            return;
        }
        x_min = x_max = a[0].x;
        for (size_t i = 0; i < n; i++){
            x_min = std::min(x_min,std::min(a[i].x,b[i].x));
            x_max = std::max(x_max,std::max(a[i].x,b[i].x));
        }
        int bands = std::max(1,(int)n / 2);
        band_size = (x_max - x_min) / (float)bands;
        if (band_size <= 0.0f){
            return;
        }
        first.assign(bands + 1,0);
        auto band_of = [&](float x){ return std::max(0,std::min(bands - 1,(int)((x - x_min) / band_size))); };
        //Counted, then filled, so the segments sit in one array in band order.
        for (int pass = 0; pass < 2; pass++){
            std::vector<int> at;
            if (pass == 1){
                for (int k = 0; k < bands; k++){
                    first[k + 1] += first[k];
                }
                segments.resize(first[bands]);
                at.assign(first.begin(),first.end() - 1);
            }
            for (size_t i = 0; i < n; i++){
                int b0 = band_of(std::min(a[i].x,b[i].x));
                int b1 = band_of(std::max(a[i].x,b[i].x));
                for (int k = b0; k <= b1; k++){
                    if (pass == 0){
                        first[k + 1]++;
                    }else{
                        segments[at[k]++] = (int)i;
                    }
                }
            }
        }
    }

    bool Inside(const vec2& pt) const{
        if (first.empty() || pt.x < x_min || pt.x > x_max){
            return false;   //no segment straddles it
        }
        int bands = (int)first.size() - 1;
        int k = std::max(0,std::min(bands - 1,(int)((pt.x - x_min) / band_size)));
        bool f_in = false;
        for (int e = first[k]; e < first[k + 1]; e++){
            const vec2& p = a[segments[e]];
            const vec2& q = b[segments[e]];
            if ((p.x > pt.x) != (q.x > pt.x)){
                float z = p.y + (pt.x - p.x) * (q.y - p.y) / (q.x - p.x);
                if (z < pt.y){
                    f_in = !f_in;
                }
            }
        }
        return f_in;
    }

private:
    std::vector<vec2> a;
    std::vector<vec2> b;
    float x_min = 0.0f;
    float x_max = 0.0f;
    float band_size = 0.0f;
    std::vector<int> first;     //band k's segments are segments[first[k] .. first[k + 1])
    std::vector<int> segments;
};

}

//The smoothed lines the grid pinned its chains to - the chains lie exactly on these, so the levels
//split exactly along them.
std::vector<GridLine> TerrainFeatureLines(const Grid& g){
    if ((int)g.lines.size() <= g.feature_line_base){
        return std::vector<GridLine>();
    }
    return std::vector<GridLine>(g.lines.begin() + g.feature_line_base,g.lines.end());
}

uint8_t TerrainLevelOfKind(int kind){
    if (kind == GRID_FEATURE_RIM){
        return TERRAIN_PLATEAU;
    }
    return kind == GRID_FEATURE_BALCONY ? TERRAIN_BALCONY : TERRAIN_ISLAND;
}

void Terrain::Build(const Grid& g, const std::vector<GridLine>& features){
    auto t0 = std::chrono::steady_clock::now();
    size_t n = g.fine.pos.size();
    //The rivers first: their wet plots and their falls are what the heights keep clear of.
    BuildRivers(g,features);
    BuildHeights(g,features);
    BuildKinds(g);
    //The biomes (TerrainBiomeAt), from the layout's mountain, pockets and south regions. A plot at the
    //plateau's height is plateau for them, bare or not.
    biome.assign(n,TERRAIN_BIOME_TEMPERATE);
    for (int i = 0; i < TERRAIN_NUM_BIOMES; i++){
        biome_count[i] = 0;
    }
    for (size_t v = 0; v < n; v++){
        int kind = (steps[v] == 0) ? TERRAIN_PLATEAU : level[v];
        biome[v] = (uint8_t)TerrainBiomeAt(g.layout,g.fine.pos[v],kind);
        biome_count[biome[v]]++;
    }
    //After the rivers: the relief fades out along them.
    BuildRelief(g);
    ground.assign(n,0.0f);
    for (size_t v = 0; v < n; v++){
        ground[v] = GroundHeight(g.fine.pos[v],Height((int)v));
    }
    //The swamp's pools: ground under the water, or barely out of it, is wet like a river's bank -
    //nothing is built or grown there, and no walker crosses it (until there are causeways).
    swamp_pool_count = 0;
    for (size_t v = 0; v < n; v++){
        if (biome[v] == TERRAIN_BIOME_SWAMP && ground[v] < TERRAIN_WATER_Y + SWAMP_SHORE){
            if (!wet[v]){
                wet[v] = 1;
                wet_count++;
            }
            swamp_pool_count++;
        }
    }
    build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

/*
    THE HEIGHTS, in steps (Terrain.h). Every number below is a look, tuned against the Blender model
    the user chose (art_source/chasm/chasm_terraces.blend, apps/chasm/tools/blender_chasm_terraces.py):
    a plot's noise is a function of its position and the seed alone, so the heights are the same in
    every build.
*/
#define HEIGHT_COLUMN_STEPS     2       //a column's top: -12, standing tall out of the chasm
#define HEIGHT_ISLAND_STEPS     4       //a shard's or a ledge's: -24
#define HEIGHT_BALCONY_STEPS    2       //-12
#define HEIGHT_FRINGE_MOST      13.0f   //how far out from a wall its broken columns reach, at most
#define HEIGHT_FRINGE_STEPS     6       //the deepest a broken column goes: -36, just out of the deck
#define HEIGHT_FALL_CLEAR       9.0f    //no broken column this near a fall's lip: the water drops clear
#define HEIGHT_CRACK_DEEPEST    5       //steps, where a crack leaves the rift; 1 at its tip

namespace {

uint32_t PlotHash(int32_t x, int32_t z, uint32_t salt){
    uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)z * 0xD8163841u ^ salt * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

//Smooth value noise in 0..1 at `wavelength`, the corners hashed with `salt`.
float Noise01(const vec2& p, float wavelength, uint32_t salt){
    float fx = std::floor(p.x / wavelength);
    float fz = std::floor(p.y / wavelength);
    float tx = p.x / wavelength - fx;
    float tz = p.y / wavelength - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    int32_t ix = (int32_t)fx;
    int32_t iz = (int32_t)fz;
    auto corner = [salt](int32_t cx, int32_t cz){ return (float)(PlotHash(cx,cz,salt) & 0xFFFF) / 65535.0f; };
    float a = corner(ix,iz) + (corner(ix + 1,iz) - corner(ix,iz)) * tx;
    float b = corner(ix,iz + 1) + (corner(ix + 1,iz + 1) - corner(ix,iz + 1)) * tx;
    return a + (b - a) * tz;
}

float Smooth01(float e0, float e1, float x){
    float t = std::max(0.0f,std::min(1.0f,(x - e0) / (e1 - e0)));
    return t * t * (3.0f - 2.0f * t);
}

}

void Terrain::BuildHeights(const Grid& g, const std::vector<GridLine>& features){
    size_t n = g.fine.pos.size();
    const uint32_t salt = g.settings.seed * 0x9E3779B1u;
    level.assign(n,TERRAIN_PLATEAU);
    steps.assign(n,0);

    //--- From the layout's lines: the rims bound the chasm; islands and balconies stand in it --------
    CrossingTable rims;
    CrossingTable islands;
    CrossingTable columns;
    CrossingTable balconies;
    for (size_t f = 0; f < features.size(); f++){
        int kind = g.LineKind(g.feature_line_base + (int)f);
        if (kind == GRID_FEATURE_RIM){
            rims.Add(features[f].points);
        }else if (kind == GRID_FEATURE_BALCONY){
            balconies.Add(g.layout.features[f].region.points);
        }else if (kind == GRID_FEATURE_COLUMN){
            columns.Add(features[f].points);
        }else{
            islands.Add(features[f].points);
        }
    }
    rims.Build();
    islands.Build();
    columns.Build();
    balconies.Build();
    //What each plot stands as before the passes: 1 a balcony, 2 an island or column, 0 neither.
    std::vector<uint8_t> stand(n,0);
    for (size_t v = 0; v < n; v++){
        /*
            A hair inside the map. A vertex on the east edge sits at exactly the x a rim ending
            there ends at, and the straddle test is half-open: with nothing beyond x_max it never
            counts that rim's end, and the whole east mouth of a rift came out plateau - a wall
            straight across it. (The west edge passes by the same asymmetry.)
        */
        vec2 p = g.fine.pos[v];
        p.x = std::max(g.bounds_min.x + 1e-3f,std::min(g.bounds_max.x - 1e-3f,p.x));
        if (!rims.Inside(p)){
            continue;
        }
        if (islands.Inside(p)){
            steps[v] = HEIGHT_ISLAND_STEPS;
            stand[v] = 2;
        }else if (columns.Inside(p)){
            steps[v] = HEIGHT_COLUMN_STEPS;
            stand[v] = 2;
        }else if (balconies.Inside(p)){
            steps[v] = HEIGHT_BALCONY_STEPS;
            stand[v] = 1;
        }else{
            steps[v] = (uint8_t)FringeSteps(g,p,salt);
        }
    }

    std::vector<std::vector<int>> nbr(n);
    for (const std::pair<int,int>& e : g.fine.edges){
        nbr[e.first].push_back(e.second);
        nbr[e.second].push_back(e.first);
    }
    auto noise = [&](int v, float wavelength, uint32_t k){ return Noise01(g.fine.pos[v],wavelength,salt + k); };
    auto near_fall = [&](int v, float reach){
        for (const TerrainFall& f : falls){
            if ((g.fine.pos[v] - f.lip).length() < reach){
                return true;
            }
        }
        return false;
    };
    //The mountain and a margin off its foot: crumbling there could box a plot of plateau in between
    //the mountain, which no one crosses, and the drop - land only a zeppelin could reach.
    auto near_mountain = [&](int v){ return g.fine.pos[v].y < ChasmMountainFootAt(g.layout,g.fine.pos[v].x) + 12.0f; };

    //--- Islands and balconies: a crumbling edge, and a broken skirt round them two plots deep ------
    {
        std::vector<uint8_t> next = steps;
        for (size_t v = 0; v < n; v++){
            if (stand[v] == 0){
                continue;
            }
            for (int w : nbr[v]){
                if (steps[w] > steps[v] && noise((int)v,2.4f,60) > 0.68f){
                    next[v] = steps[v] + 1;
                }
            }
        }
        steps.swap(next);
        //Ring 0 grows from what stands, ring 1 from ring 0 only - from anything lower than the floor
        //it would put a ledge along every sheer wall.
        std::vector<uint8_t> from(n,0);
        for (size_t v = 0; v < n; v++){
            from[v] = stand[v] != 0;
        }
        for (int ring = 0; ring < 2; ring++){
            next = steps;
            std::vector<uint8_t> grown(n,0);
            for (size_t v = 0; v < n; v++){
                if (steps[v] != TERRAIN_FLOOR_STEPS || near_fall((int)v,HEIGHT_FALL_CLEAR)){
                    continue;
                }
                int over = TERRAIN_FLOOR_STEPS;     //the highest neighbour it could be a skirt of
                for (int w : nbr[v]){
                    if (from[w]){
                        over = std::min(over,(int)steps[w]);
                    }
                }
                if (over >= TERRAIN_FLOOR_STEPS || (ring == 1 && noise((int)v,2.2f,71) < 0.55f)){
                    continue;
                }
                int s = over + 1 + (noise((int)v,2.2f,70) > 0.5f ? 1 : 0);
                if (s <= HEIGHT_FRINGE_STEPS){
                    next[v] = (uint8_t)s;
                    grown[v] = 1;
                }
            }
            steps.swap(next);
            from.swap(grown);
        }
    }

    //--- The rim crumbling ----------------------------------------------------------------------
    /*
        Plateau plots over a real drop break a step or two down, in clumps of a few; then, more
        rarely, the plots behind those. Only ever next to a drop - never a pit in the meadow - and
        never over a balcony, whose winch needs the rim above it whole, nor at a fall's lip.
    */
    for (int ring = 0; ring < 2; ring++){
        std::vector<uint8_t> next = steps;
        for (size_t v = 0; v < n; v++){
            if (steps[v] != 0 || wet[v] || g.fine.f_boundary[v] || near_mountain((int)v) || near_fall((int)v,HEIGHT_FALL_CLEAR)){
                continue;
            }
            bool f_drop = false;
            for (int w : nbr[v]){
                f_drop = f_drop || (steps[w] >= (ring == 0 ? 2 : 1) && stand[w] != 1);
            }
            if (!f_drop){
                continue;
            }
            float d = noise((int)v,2.8f,50 + ring);
            if (ring == 0 && d > 0.82f){
                next[v] = 2;
            }else if (d > (ring == 0 ? 0.60f : 0.70f)){
                next[v] = 1;
            }
        }
        steps.swap(next);
    }

    //--- Cracks: deepest where they leave the rift, shallowing to a step at the tip ------------------
    for (const ChasmLayout::Crack& c : g.layout.cracks){
        float total = 0.0f;
        vec2 lo = c.points[0];
        vec2 hi = c.points[0];
        for (size_t k = 0; k + 1 < c.points.size(); k++){
            total += (c.points[k + 1] - c.points[k]).length();
        }
        for (const vec2& p : c.points){
            lo = vec2(std::min(lo.x,p.x),std::min(lo.y,p.y));
            hi = vec2(std::max(hi.x,p.x),std::max(hi.y,p.y));
        }
        for (size_t v = 0; v < n; v++){
            const vec2& p = g.fine.pos[v];
            if (steps[v] > 2 || stand[v] != 0){
                continue;   //the chasm, already deep, or a balcony or island
            }
            if (p.x < lo.x - 4.0f || p.x > hi.x + 4.0f || p.y < lo.y - 4.0f || p.y > hi.y + 4.0f ||
                wet[v] || g.fine.f_boundary[v]){
                continue;
            }
            float best = 1e30f;
            float along = 0.0f;
            float s = 0.0f;
            for (size_t k = 0; k + 1 < c.points.size(); k++){
                vec2 a = c.points[k];
                vec2 ab = c.points[k + 1] - a;
                float len = ab.length();
                float t = len > 0.0f ? std::max(0.0f,std::min(1.0f,(p - a).dot(ab) / (len * len))) : 0.0f;
                float d = (p - (a + ab * t)).length();
                if (d < best){
                    best = d;
                    along = s + t * len;
                }
                s += len;
            }
            float f = along / std::max(total,1.0f);
            //Never under a plot's width, or the crack falls apart into separate pits.
            float half = std::max(1.6f,c.half_width * (1.0f - 0.5f * f));
            if (best < half){
                int want = (int)std::lround((float)HEIGHT_CRACK_DEEPEST - (float)(HEIGHT_CRACK_DEEPEST - 1) * f);
                steps[v] = (uint8_t)std::max((int)steps[v],want);
            }
        }
    }

    //--- Saddles and pits, until neither changes anything --------------------------------------------
    /*
        SADDLES: two high corners diagonal across a quad and two low ones would stand four walls on one
        line through its centre. The higher of the low pair is raised to the lower of the high, which
        joins the high pair - as marching squares joined them.

        PITS: everything below the rim hangs together ("one chasm" in the checks). A crack's thin end
        can catch plots that meet only corner to corner, which the grid's edges do not join - and
        each would be a pit with walls all round. Every piece below the plateau's height but the
        biggest - the chasm - is filled back to the plateau.

        Each can make work for the other, so they take turns.
    */
    saddles_joined = 0;
    pits_filled = 0;
    for (int round = 0; round < 8; round++){
        int raised = 0;
        for (int pass = 0; pass < 30; pass++){
            int this_pass = 0;
            for (const GridQuad& q : g.fine.quads){
                const int* c = q.v;
                for (int k = 0; k < 2; k++){
                    int a = c[k];
                    int b = c[k + 2];
                    int x = c[k + 1];
                    int y = c[(k + 3) % 4];
                    //Heights as tops: a smaller step is higher.
                    int hi_low = std::max((int)steps[a],(int)steps[b]);    //the lower of the high pair
                    int lo_high = std::min((int)steps[x],(int)steps[y]);   //the higher of the low pair
                    if (hi_low < lo_high){
                        int raise = (steps[x] <= steps[y]) ? x : y;
                        steps[raise] = (uint8_t)hi_low;
                        this_pass++;
                    }
                }
            }
            raised += this_pass;
            if (this_pass == 0){
                break;
            }
        }
        saddles_joined += raised;

        std::vector<int> piece(n,-1);
        std::vector<int> piece_size;
        std::vector<int> stack;
        for (size_t v = 0; v < n; v++){
            if (steps[v] == 0 || piece[v] >= 0){
                continue;
            }
            int id = (int)piece_size.size();
            int size = 0;
            piece[v] = id;
            stack.push_back((int)v);
            while (!stack.empty()){
                int a = stack.back();
                stack.pop_back();
                size++;
                for (int b : nbr[a]){
                    if (steps[b] > 0 && piece[b] < 0){
                        piece[b] = id;
                        stack.push_back(b);
                    }
                }
            }
            piece_size.push_back(size);
        }
        int chasm = (int)(std::max_element(piece_size.begin(),piece_size.end()) - piece_size.begin());
        int filled = 0;
        for (size_t v = 0; v < n; v++){
            if (piece[v] >= 0 && piece[v] != chasm){
                steps[v] = 0;
                filled++;
            }
        }
        pits_filled += filled;
        if (raised == 0 && filled == 0){
            break;
        }
    }
}

/*
    How many steps down a chasm plot is that no island or balcony claims: the floor, or a broken
    column stepping down from the wall - where the wall has them. How far out they reach wanders along
    the rim, to nothing in places, where the wall drops sheer. Within a reach the columns step down
    with the distance, in blocks a few plots across (noise, not a plot-by-plot hash), with gaps so
    they stand apart.
*/
int Terrain::FringeSteps(const Grid& g, const vec2& p, uint32_t salt) const{
    for (const TerrainFall& f : falls){
        if ((p - f.lip).length() < HEIGHT_FALL_CLEAR){
            return TERRAIN_FLOOR_STEPS;
        }
    }
    float depth = std::max(0.0f,-ChasmRiftDistance(g.layout,p));
    float reach = HEIGHT_FRINGE_MOST * Smooth01(0.40f,0.75f,Noise01(p,24.0f,salt + 1));
    if (depth >= reach || Noise01(p,5.0f,salt + 2) > 0.76f){
        return TERRAIN_FLOOR_STEPS;
    }
    float t = depth / reach;
    float jitter = (float)(PlotHash((int32_t)std::floor(p.x * 7.0f),(int32_t)std::floor(p.y * 7.0f),salt + 4) & 0xFFFF) / 65535.0f;
    float s = 1.0f + 3.5f * t + (Noise01(p,6.0f,salt + 3) - 0.5f) * 2.5f + (jitter - 0.5f) * 0.4f;
    return std::max(1,std::min(HEIGHT_FRINGE_STEPS,(int)std::lround(s)));
}

/*
    THE KINDS from the heights (Terrain.h): flat patches by flooding along grid edges at one height,
    then floor, bare, plateau, and balcony or island by whether a winch can reach the patch - from the
    plateau, or from a balcony already reached.
*/
void Terrain::BuildKinds(const Grid& g){
    size_t n = g.fine.pos.size();
    std::vector<std::vector<int>> nbr(n);
    for (const std::pair<int,int>& e : g.fine.edges){
        nbr[e.first].push_back(e.second);
        nbr[e.second].push_back(e.first);
    }
    patch.assign(n,-1);
    patch_size.clear();
    std::vector<int> stack;
    for (size_t v = 0; v < n; v++){
        if (patch[v] >= 0){
            continue;
        }
        int id = (int)patch_size.size();
        int size = 0;
        patch[v] = id;
        stack.push_back((int)v);
        while (!stack.empty()){
            int a = stack.back();
            stack.pop_back();
            size++;
            for (int b : nbr[a]){
                if (patch[b] < 0 && steps[b] == steps[a]){
                    patch[b] = id;
                    stack.push_back(b);
                }
            }
        }
        patch_size.push_back(size);
    }
    std::vector<uint8_t> kind(patch_size.size(),TERRAIN_ISLAND);
    std::vector<uint8_t> patch_steps(patch_size.size(),0);
    for (size_t v = 0; v < n; v++){
        patch_steps[patch[v]] = steps[v];
    }
    std::vector<int> queue;
    for (size_t k = 0; k < patch_size.size(); k++){
        if (-TERRAIN_STEP * (float)patch_steps[k] < TERRAIN_DECK_Y){
            kind[k] = TERRAIN_FLOOR;
        }else if (patch_size[k] < TERRAIN_MIN_PATCH){
            kind[k] = TERRAIN_BARE;
        }else if (patch_steps[k] == 0){
            kind[k] = TERRAIN_PLATEAU;
            queue.push_back((int)k);
        }
    }
    //Which patches touch which: across every edge between two heights.
    std::vector<std::vector<int>> touching(patch_size.size());
    for (const std::pair<int,int>& e : g.fine.edges){
        int a = patch[e.first];
        int b = patch[e.second];
        if (a != b){
            touching[a].push_back(b);
            touching[b].push_back(a);
        }
    }
    for (size_t at = 0; at < queue.size(); at++){
        for (int k : touching[queue[at]]){
            if (kind[k] == TERRAIN_ISLAND){
                kind[k] = TERRAIN_BALCONY;
                queue.push_back(k);
            }
        }
    }
    for (int i = 0; i < TERRAIN_NUM_LEVELS; i++){
        level_count[i] = 0;
    }
    for (size_t v = 0; v < n; v++){
        level[v] = kind[patch[v]];
        level_count[level[v]]++;
    }
}

//--- The relief (biomes_plan.md step 2) -----------------------------------------------------------

namespace {

//Smooth value noise in -1..1 on a lattice of `wavelength`, from the seed. Position alone decides it.
float ReliefNoise(const vec2& p, float wavelength, uint32_t seed){
    auto hash = [seed](int x, int z){
        uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)z * 0xD8163841u ^ seed * 0xCB1AB31Fu;
        h ^= h >> 13;
        h *= 0x5BD1E995u;
        h ^= h >> 15;
        return (float)(h & 0xFFFFu) / 32767.5f - 1.0f;
    };
    float fx = p.x / wavelength;
    float fz = p.y / wavelength;
    float ix = std::floor(fx);
    float iz = std::floor(fz);
    float tx = fx - ix;
    float tz = fz - iz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    int x = (int)ix;
    int z = (int)iz;
    float a = hash(x,z) + (hash(x + 1,z) - hash(x,z)) * tx;
    float b = hash(x,z + 1) + (hash(x + 1,z + 1) - hash(x,z + 1)) * tx;
    return a + (b - a) * tz;
}

//A ridge: 1 along the noise's zero line, falling to 0 away from it - sharp crests, broad valleys.
float Ridge(const vec2& p, float wavelength, uint32_t seed){
    float r = 1.0f - std::fabs(ReliefNoise(p,wavelength,seed));
    return r * r;
}

float SmoothStep(float e0, float e1, float x){
    float t = std::max(0.0f,std::min(1.0f,(x - e0) / (e1 - e0)));
    return t * t * (3.0f - 2.0f * t);
}

}

/*
    THE RELIEF, made once per world on a 2-unit raster. Three parts, all of position alone:

      - HILLS, soft, in regions of the plateau: two octaves of noise lifted to be all above the
        level, times a regional mask, so some country rolls and some lies flat for towns and fields.
      - THE MOUNTAIN: rising from the foot over RELIEF_MOUNTAIN_RISE into peaks - a base rise and two
        octaves of ridges - so its front is foothills and its heart crags. Added to the hills from
        nothing at the foot, so the hills run on up into it rather than meeting an edge.
      - FLAT ALONG RIVERS: everything fades to nothing toward a river's wet margin, so the water's
        flat surface (TERRAIN_WATER_Y) always has flat ground round it, and a river through the
        mountain runs in a valley it has cut.

    Seeded from the world's seed, through its own salts, so hills differ between maps.
*/
void Terrain::BuildRelief(const Grid& g){
    const uint32_t seed = g.settings.seed;
    relief_seed = seed;
    relief_origin = g.bounds_min;
    vec2 size = g.bounds_max - g.bounds_min;
    relief_w = (int)std::ceil(size.x / relief_cell) + 2;
    relief_h = (int)std::ceil(size.y / relief_cell) + 2;
    relief.assign((size_t)relief_w * relief_h,0.0f);
    relief_max = 0.0f;
    for (int j = 0; j < relief_h; j++){
        for (int i = 0; i < relief_w; i++){
            vec2 p = relief_origin + vec2(i * relief_cell,j * relief_cell);
            //Hills: 0..1 noise, a region mask, an amplitude.
            float shape = ReliefNoise(p,RELIEF_HILL_WAVELENGTH,seed ^ 0x4111u) * 0.75f +
                          ReliefNoise(p,RELIEF_HILL_WAVELENGTH * 0.4f,seed ^ 0x4112u) * 0.25f;
            float mask = SmoothStep(-0.15f,0.45f,ReliefNoise(p,RELIEF_HILL_REGION,seed ^ 0x4113u));
            float hills = (shape * 0.5f + 0.5f) * mask * RELIEF_HILL_HEIGHT;
            //The mountain: how far in from its foot, and the peaks there.
            float in = ChasmMountainFootAt(g.layout,p.x) - p.y;
            float rise = SmoothStep(0.0f,RELIEF_MOUNTAIN_RISE,in);
            float crags = Ridge(p,RELIEF_PEAK_WAVELENGTH,seed ^ 0x4114u) * 0.7f +
                          Ridge(p,RELIEF_PEAK_WAVELENGTH * 0.35f,seed ^ 0x4115u) * 0.3f;
            //On top of the hills, from nothing at the foot: the hills run on up into it, no seam.
            float mountain = rise * (RELIEF_MOUNTAIN_BASE + crags * RELIEF_PEAK_HEIGHT);
            float h = hills + mountain;
            /*
                The swamp: the hills sink away into it, to a floor about the water's level, broken by
                hummocks - so pools lie wherever the ground dips under the water, and the shore is
                wherever the two cross, as along a river.
            */
            float swamp = ChasmSwampMask(g.layout,p);
            if (swamp > 0.0f){
                float hummock = ReliefNoise(p,SWAMP_HUMMOCK_SIZE,seed ^ 0x4116u) * 0.7f +
                                ReliefNoise(p,SWAMP_HUMMOCK_SIZE * 0.4f,seed ^ 0x4117u) * 0.3f;
                h = h + (SWAMP_FLOOR + hummock * SWAMP_HUMMOCK - h) * swamp;
            }
            //The desert (step 5): the hills give way to dunes - ridges, sharp at the crest.
            float desert = ChasmDesertMask(g.layout,p);
            if (desert > 0.0f){
                float dune = Ridge(p,DUNE_WAVELENGTH,seed ^ 0x4118u) * 0.7f +
                             Ridge(p,DUNE_WAVELENGTH * 0.4f,seed ^ 0x4119u) * 0.3f;
                h = h + (DUNE_BASE + dune * DUNE_HEIGHT - h) * desert;
            }
            /*
                A pocket: its meadow flat at its floor, its valley ramping from that floor down to the
                hills at its mouth - so it can be walked up - and the crags rising steeply round both
                over RELIEF_POCKET_EDGE, which is what makes it read as a hollow in the mountain.
            */
            for (const ChasmLayout::Pocket& k : g.layout.pockets){
                float along = 0.0f;
                float d = ChasmPocketDistance(k,p,&along);
                float keep = SmoothStep(0.0f,RELIEF_POCKET_EDGE,d);
                if (keep < 1.0f){
                    float target = k.floor + (hills - k.floor) * along;
                    h = target + (h - target) * keep;
                }
            }
            //Flat along rivers.
            float river = SmoothStep(TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN,
                                     TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN + RELIEF_RIVER_FADE,EdgeDistance(p));
            h *= river;
            relief[(size_t)j * relief_w + i] = h;
            relief_max = std::max(relief_max,h);
        }
    }
}

float Terrain::Relief(const vec2& p) const{
    if (relief.empty()){
        return 0.0f;
    }
    float fx = (p.x - relief_origin.x) / relief_cell;
    float fz = (p.y - relief_origin.y) / relief_cell;
    fx = std::max(0.0f,std::min((float)(relief_w - 1) - 0.001f,fx));
    fz = std::max(0.0f,std::min((float)(relief_h - 1) - 0.001f,fz));
    int ix = (int)fx;
    int iz = (int)fz;
    float tx = fx - ix;
    float tz = fz - iz;
    const float* row0 = &relief[(size_t)iz * relief_w + ix];
    const float* row1 = row0 + relief_w;
    float a = row0[0] + (row0[1] - row0[0]) * tx;
    float b = row1[0] + (row1[1] - row1[0]) * tx;
    return a + (b - a) * tz;
}

float Terrain::GroundHeight(const vec2& p, float level_height) const{
    float y = TerrainGroundHeight(p,level_height);
    if (level_height == terrain_levels[TERRAIN_PLATEAU].height){
        y += Relief(p);
    }
    return y;
}

int TerrainBiomeAt(const ChasmLayout& lay, const vec2& p, int level, float dither){
    int b = TERRAIN_BIOME_TEMPERATE;
    if (p.y < ChasmMountainFootAt(lay,p.x)){
        b = TERRAIN_BIOME_MOUNTAIN;
    }
    if (level != TERRAIN_PLATEAU){
        return b;
    }
    //The south's regions where their masks pass one half; never over the mountain.
    if (b == TERRAIN_BIOME_TEMPERATE){
        if (ChasmSwampMask(lay,p) + dither > 0.5f){
            b = TERRAIN_BIOME_SWAMP;
        }else if (ChasmDesertMask(lay,p) + dither > 0.5f){
            b = TERRAIN_BIOME_DESERT;
        }
    }
    //A pocket's meadow and valley are open ground, in the mountain or just out of it.
    for (const ChasmLayout::Pocket& k : lay.pockets){
        if (ChasmPocketDistance(k,p) < 0.0f){
            b = TERRAIN_BIOME_POCKET;
        }
    }
    return b;
}

float Terrain::SnowLine(const vec2& p) const{
    return SNOW_LINE + SNOW_LINE_SWING * ReliefNoise(p,SNOW_LINE_WAVELENGTH,relief_seed ^ 0x5110u);
}

const char* TerrainBiomeName(int biome){
    switch (biome){
        case TERRAIN_BIOME_TEMPERATE:   return "temperate";
        case TERRAIN_BIOME_MOUNTAIN:    return "mountain";
        case TERRAIN_BIOME_POCKET:      return "pocket";
        case TERRAIN_BIOME_SWAMP:       return "swamp";
        case TERRAIN_BIOME_DESERT:      return "desert";
        default:                        return "?";
    }
}

//--- Rivers --------------------------------------------------------------------------------------

//A river's width along its course (BuildRivers).
#define RIVER_WANDER        0.30f   //how far the width wanders either way, a share of the usual
#define RIVER_WANDER_LENGTH 45.0f   //world units of course over which it does
#define RIVER_EASE          25.0f   //back to the usual width over this much toward the source and the fall
#define RIVER_LAKE_RUN      160.0f  //a lake per this much course before the fall (two at most)
#define RIVER_LAKE_FROM     0.35f   //lakes lie between these shares of the way to the fall
#define RIVER_LAKE_TO       0.80f
//Water widens only this far short of a rim or the map's edge: past its bank and wet margin, room
//for a cliff-top path, so a lake never reaches a cliff.
#define RIVER_LAKE_CLEAR    (TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN + 7.0f)

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
    //The layout's lines, in world coordinates already (ChasmLayout.cpp chose their courses), rounded.
    for (const GridRiverLine& def : g.layout.rivers){
        TerrainRiver r;
        r.points = Smooth(def.points,3);
        r.width = def.width;
        r.length = 0.0f;
        for (size_t i = 0; i + 1 < r.points.size(); i++){
            r.length += (r.points[i + 1] - r.points[i]).length();
        }
        rivers.push_back(r);
    }

    //--- Falls: each river's first crossing of any rim -------------------------------------------
    for (int ri = 0; ri < (int)rivers.size(); ri++){
        const TerrainRiver& r = rivers[ri];
        float along = 0.0f;
        bool f_found = false;
        for (size_t i = 0; i + 1 < r.points.size() && !f_found; i++){
            vec2 a = r.points[i];
            vec2 b = r.points[i + 1];
            //The nearest crossing along this segment, whichever rim it is on.
            float best_t = 2.0f;
            vec2 tangent;
            for (size_t fi = 0; fi < features.size(); fi++){
                if (g.LineKind(g.feature_line_base + (int)fi) != GRID_FEATURE_RIM){
                    continue;
                }
                const std::vector<vec2>& rim = features[fi].points;
                for (size_t j = 0; j + 1 < rim.size(); j++){
                    float t;
                    if (SegmentsCross(a,b,rim[j],rim[j + 1],t) && t < best_t){
                        best_t = t;
                        tangent = rim[j + 1] - rim[j];
                    }
                }
            }
            if (best_t <= 1.0f){
                TerrainFall f;
                f.river = ri;
                f.lip = a + (b - a) * best_t;
                f.width = r.width;
                f.along = along + (b - a).length() * best_t;
                tangent.normalize();
                f.out = vec2(-tangent.y,tangent.x);
                if (f.out.dot(b - a) < 0.0f){
                    f.out = -f.out;
                }
                f.across = vec2(-f.out.y,f.out.x);
                falls.push_back(f);
                f_found = true;
            }
            along += (b - a).length();
        }
    }

    /*
        --- Each river's width along it (2026-10-06) ---------------------------------------------
        The layout gives a river one width. Along the course it now WANDERS - a slow noise, a third
        either way - and the longer rivers swell into a LAKE or two: a smooth bulge several times the
        river's width over a stretch, the river running in at one end and out at the other. All of
        it eases back to the usual width toward the source and over the fall (whose sheet is drawn
        at that width), and it only WIDENS where there is room - kept RIVER_LAKE_CLEAR clear of
        every rim and of the map's edge, so no lake reaches a cliff. The edge raster below, and
        so the channel, the banks, the wet margin and the water's surface, all follow it.
    */
    for (int ri = 0; ri < (int)rivers.size(); ri++){
        TerrainRiver& r = rivers[ri];
        size_t n = r.points.size();
        std::vector<float> s(n,0.0f);
        for (size_t i = 1; i < n; i++){
            s[i] = s[i - 1] + (r.points[i] - r.points[i - 1]).length();
        }
        float fall_at = r.length;
        for (const TerrainFall& f : falls){
            if (f.river == ri){
                fall_at = f.along;
            }
        }
        const uint32_t seed = g.settings.seed * 0x9E3779B1u ^ (uint32_t)(ri + 1) * 0x85EBCA77u;
        auto unit = [seed](uint32_t salt){
            uint32_t h = seed ^ (salt * 0xC2B2AE3Du);
            h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
            return (float)(h & 0xFFFFFF) / (float)0x1000000;
        };
        const float base = r.width * 0.5f;
        //The lakes: one per RIVER_LAKE_RUN of course before the fall (two at most, and sometimes one
        //fewer), spread over its middle, where the source and the fall are far off.
        struct Lake{ float at, half, reach; };
        std::vector<Lake> lakes;
        int want = std::min(2,(int)(fall_at / RIVER_LAKE_RUN));
        if (want > 0 && unit(1) < 0.25f){
            want--;
        }
        for (int k = 0; k < want; k++){
            Lake l;
            l.at = fall_at * (RIVER_LAKE_FROM + (RIVER_LAKE_TO - RIVER_LAKE_FROM) * ((float)k + 0.2f + 0.6f * unit(2 + k)) / (float)want);
            l.half = base * (2.6f + 1.4f * unit(10 + k));
            l.reach = l.half * (1.6f + 0.8f * unit(20 + k));
            lakes.push_back(l);
        }
        //How far a point is from the nearest rim and from the map's edge - the room it has to widen.
        auto room = [&](const vec2& p){
            float d = std::min(std::min(p.x - g.bounds_min.x,g.bounds_max.x - p.x),
                               std::min(p.y - g.bounds_min.y,g.bounds_max.y - p.y));
            for (size_t fi = 0; fi < features.size(); fi++){
                if (g.LineKind(g.feature_line_base + (int)fi) != GRID_FEATURE_RIM){
                    continue;
                }
                const std::vector<vec2>& rim = features[fi].points;
                for (size_t j = 0; j + 1 < rim.size(); j++){
                    float t;
                    d = std::min(d,SegmentDistance(p,rim[j],rim[j + 1],t));
                }
            }
            return d - RIVER_LAKE_CLEAR;
        };
        r.half.assign(n,base);
        r.lakes = 0;
        for (size_t i = 0; i < n; i++){
            float h = base * (1.0f + RIVER_WANDER * ReliefNoise(vec2(s[i],(float)ri * 53.0f),RIVER_WANDER_LENGTH,seed));
            for (const Lake& l : lakes){
                float d = std::fabs(s[i] - l.at) / l.reach;
                if (d < 1.0f){
                    float bump = std::cos(d * 1.5707963f);
                    h = std::max(h,base + (l.half - base) * bump * bump);
                }
            }
            float ease = SmoothStep(0.0f,RIVER_EASE,s[i]) * SmoothStep(0.0f,RIVER_EASE,fall_at - s[i]);
            h = base + (h - base) * ease;
            if (h > base){
                h = std::max(base,std::min(h,room(r.points[i])));
            }
            r.half[i] = h;
        }
        for (const Lake& l : lakes){
            //Counted where it came to something: a lake squeezed back by a rim is no lake.
            float widest = 0.0f;
            for (size_t i = 0; i < n; i++){
                if (std::fabs(s[i] - l.at) < l.reach){
                    widest = std::max(widest,r.half[i]);
                }
            }
            r.lakes += (widest > base * 1.8f) ? 1 : 0;
        }
    }

    //--- The edge raster: distance past the nearest water edge -------------------------------------
    //Out as far as the relief's fade along a river (BuildRelief), past the wet margin it was made for.
    const float reach = TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN + RELIEF_RIVER_FADE + 2.0f;
    edge_cell = 1.0f;
    edge_origin = lo;
    edge_w = (int)std::ceil(size.x / edge_cell) + 2;
    edge_h = (int)std::ceil(size.y / edge_cell) + 2;
    edge.assign((size_t)edge_w * edge_h,1e3f);
    for (const TerrainRiver& r : rivers){
        for (size_t i = 0; i + 1 < r.points.size(); i++){
            vec2 a = r.points[i];
            vec2 b = r.points[i + 1];
            float pad = std::max(r.half[i],r.half[i + 1]) + reach;
            int x0 = std::max(0,(int)std::floor((std::min(a.x,b.x) - pad - edge_origin.x) / edge_cell));
            int x1 = std::min(edge_w - 1,(int)std::ceil((std::max(a.x,b.x) + pad - edge_origin.x) / edge_cell));
            int z0 = std::max(0,(int)std::floor((std::min(a.y,b.y) - pad - edge_origin.y) / edge_cell));
            int z1 = std::min(edge_h - 1,(int)std::ceil((std::max(a.y,b.y) + pad - edge_origin.y) / edge_cell));
            for (int z = z0; z <= z1; z++){
                for (int x = x0; x <= x1; x++){
                    vec2 p = edge_origin + vec2(x * edge_cell,z * edge_cell);
                    float t;
                    float d = SegmentDistance(p,a,b,t);
                    float e = d - r.HalfAt(i,t);
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
            float half = r.HalfAt(i,t);
            if (d < best && d - half < reach){
                best = d;
                river = ri;
                along = run + len * t;
                vec2 ab = b - a;
                float side = ab.x * (p.y - a.y) - ab.y * (p.x - a.x);
                across = ((side < 0.0f) ? -d : d) / half;
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
        bool f_rim = false;
        bool f_island = false;
        bool f_balcony = false;
        for (size_t f = 0; f < features.size(); f++){
            int kind = g.LineKind(g.feature_line_base + (int)f);
            f_rim = f_rim || kind == GRID_FEATURE_RIM;
            f_island = f_island || GridFeatureIsIsland(kind);
            f_balcony = f_balcony || kind == GRID_FEATURE_BALCONY;
        }
        bool f_ok = t.level_count[TERRAIN_PLATEAU] > 0 &&
                    (!f_rim || t.level_count[TERRAIN_FLOOR] > 0) &&
                    (!f_island || t.level_count[TERRAIN_ISLAND] > 0) &&
                    (!f_balcony || t.level_count[TERRAIN_BALCONY] > 0);
        snprintf(buf,sizeof(buf),"plateau %i, balcony %i, island %i, bare %i, floor %i vertices; %i flat patches, "
                 "%i plots raised for saddles, %i pits filled (%.1f ms)",
                 t.level_count[TERRAIN_PLATEAU],t.level_count[TERRAIN_BALCONY],t.level_count[TERRAIN_ISLAND],
                 t.level_count[TERRAIN_BARE],t.level_count[TERRAIN_FLOOR],(int)t.patch_size.size(),t.saddles_joined,t.pits_filled,
                 t.build_ms);
        add_result("levels",f_ok,buf);
    }

    /*
        --- One chasm: everything below the rim is one piece (user, 2026-10-06) ----------------
        Every plot under the plateau's height, joined across any edge between two of them, whatever
        their heights: the chasm with its broken columns, its islands, its cracks and its crumbled
        rim. A crack starts inside the rift and crumbling only happens beside a drop, so a second
        piece is a second chasm, or a pit.
    */
    {
        size_t n = g.fine.pos.size();
        std::vector<int> parent(n);
        for (size_t v = 0; v < n; v++){
            parent[v] = (int)v;
        }
        std::function<int(int)> find = [&](int v){
            while (parent[v] != v){
                parent[v] = parent[parent[v]];
                v = parent[v];
            }
            return v;
        };
        for (const auto& e : g.fine.edges){
            if (t.steps[e.first] > 0 && t.steps[e.second] > 0){
                int ra = find(e.first);
                int rb = find(e.second);
                if (ra != rb){
                    parent[std::max(ra,rb)] = std::min(ra,rb);
                }
            }
        }
        int pieces = 0;
        for (size_t v = 0; v < n; v++){
            if (t.steps[v] > 0 && find((int)v) == (int)v){
                pieces++;
                if (pieces > 1){
                    add_issue("one chasm",(int)v,g.fine.pos[v],true);
                }
            }
        }
        snprintf(buf,sizeof(buf),"below the rim in %i piece%s; %i crack%s",pieces,pieces == 1 ? "" : "s",
                 (int)g.layout.cracks.size(),g.layout.cracks.size() == 1 ? "" : "s");
        add_result("one chasm",pieces == 1,buf);
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
        --- Mouths: a rift that reaches the map's edge is open there -----------------------------
        Every outline vertex between an open rim's two ends is below the plateau - the floor, or a
        broken column standing in the chasm (Terrain::BuildHeights). A wall stood across every east
        mouth until the inside test was moved off the edge.
    */
    {
        int bad = 0;
        int mouths = 0;
        for (size_t f = 0; f < features.size(); f++){
            const std::vector<vec2>& pts = features[f].points;
            if (g.LineKind(g.feature_line_base + (int)f) != GRID_FEATURE_RIM || pts.size() < 2 ||
                (pts.front().x == pts.back().x && pts.front().y == pts.back().y)){
                continue;
            }
            vec2 a = pts.front();
            vec2 b = pts.back();
            //Both ends on one edge: the same x on a side edge, the same z on the north or south.
            bool f_side = a.x == b.x && (a.x == g.bounds_min.x || a.x == g.bounds_max.x);
            bool f_end = a.y == b.y && (a.y == g.bounds_min.y || a.y == g.bounds_max.y);
            if (!f_side && !f_end){
                continue;
            }
            mouths++;
            for (size_t v = 0; v < g.fine.pos.size(); v++){
                const vec2& p = g.fine.pos[v];
                bool f_on = f_side ? (p.x == a.x && p.y > std::min(a.y,b.y) && p.y < std::max(a.y,b.y))
                                   : (p.y == a.y && p.x > std::min(a.x,b.x) && p.x < std::max(a.x,b.x));
                if (f_on && g.fine.f_boundary[v] && t.steps[v] == 0){
                    bad++;
                    add_issue("mouth",(int)v,p,true);
                }
            }
        }
        snprintf(buf,sizeof(buf),"%i mouths on the map's edge, %i outline vertices across them on the plateau",mouths,bad);
        add_result("mouths",bad == 0,buf);
    }

    /*
        --- Relief: a figure, not a rule - how high it goes, and how much of the open plateau it makes
        too steep to build a house on. A vertex counts as steep where the ground rises more than a
        house's limit (0.7, Zones.cpp) along any fine edge from it: about a plot's worth of rise.
    */
    {
        int open = 0;
        int steep = 0;
        std::vector<float> rise(g.fine.pos.size(),0.0f);
        for (const auto& e : g.fine.edges){
            if (t.level[e.first] != t.level[e.second]){
                continue;
            }
            float d = std::fabs(t.ground[e.first] - t.ground[e.second]);
            rise[e.first] = std::max(rise[e.first],d);
            rise[e.second] = std::max(rise[e.second],d);
        }
        for (size_t v = 0; v < g.fine.pos.size(); v++){
            if (t.level[v] != TERRAIN_PLATEAU || t.Mountain((int)v) || t.wet[v]){
                continue;
            }
            open++;
            steep += (rise[v] > 0.7f) ? 1 : 0;
        }
        int swamp = t.biome_count[TERRAIN_BIOME_SWAMP];
        snprintf(buf,sizeof(buf),"highest %.1f above the plateau; %.1f%% of the open plateau too steep for a house; "
                 "swamp %i vertices (%s side), %.0f%% of it pools",t.relief_max,100.0f * steep / std::max(1,open),swamp,
                 g.layout.swamp.side < 0 ? "west" : "east",100.0f * t.swamp_pool_count / std::max(1,swamp));
        add_result("relief",true,buf);
    }

    /*
        --- Sealed: the mountain closes the chasm off, so the two sides cannot reach each other ------
        The one rule a biome enforces (biomes_plan.md). The ground is flooded as a walker would cross
        it with nothing painted - along fine edges, never down a cliff (a level change), never onto
        the mountain - but ACROSS rivers, which bridges will cross. Then no region may hold plateau on
        the south edge on both sides of the main rift's mouth. A failure marks the region's vertex
        nearest the main tip, which is where the seal should have been.
    */
    {
        size_t n = g.fine.pos.size();
        std::vector<int> parent(n);
        for (size_t v = 0; v < n; v++){
            parent[v] = (int)v;
        }
        std::function<int(int)> find = [&](int v){
            while (parent[v] != v){
                parent[v] = parent[parent[v]];
                v = parent[v];
            }
            return v;
        };
        for (const auto& e : g.fine.edges){
            int a = e.first;
            int b = e.second;
            if (t.level[a] != t.level[b] || t.Mountain(a) || t.Mountain(b)){
                continue;
            }
            int ra = find(a);
            int rb = find(b);
            if (ra != rb){
                parent[std::max(ra,rb)] = std::min(ra,rb);
            }
        }
        //Per region: whether it touches the south edge west of the mouth, and east of it.
        std::vector<uint8_t> sides(n,0);
        float south = g.bounds_max.y;
        float mouth = g.layout.main_mouth_x;
        for (size_t v = 0; v < n; v++){
            const vec2& p = g.fine.pos[v];
            if (!g.fine.f_boundary[v] || std::fabs(p.y - south) > 1e-3f || t.level[v] != TERRAIN_PLATEAU ||
                t.Mountain((int)v)){
                continue;
            }
            sides[find((int)v)] |= (p.x < mouth) ? 1 : 2;
        }
        int joined = -1;
        int west = 0, east = 0;
        for (size_t v = 0; v < n; v++){
            int r = find((int)v);
            if (sides[r] == 3){
                joined = r;
            }
            if (t.level[v] == TERRAIN_PLATEAU && !t.Mountain((int)v)){
                west += (sides[r] == 1);
                east += (sides[r] == 2);
            }
        }
        if (joined >= 0){
            int nearest = -1;
            float best = 1e30f;
            for (size_t v = 0; v < n; v++){
                if (find((int)v) == joined){
                    float d = (g.fine.pos[v] - g.layout.main_tip).length();
                    if (d < best){
                        best = d;
                        nearest = (int)v;
                    }
                }
            }
            add_issue("sealed",nearest,g.fine.pos[nearest],true);
            snprintf(buf,sizeof(buf),"the two sides of the chasm are joined - nearest the main tip at (%.0f, %.0f)",
                     g.fine.pos[nearest].x,g.fine.pos[nearest].y);
        }else{
            snprintf(buf,sizeof(buf),"west side %i, east side %i plateau vertices reachable from the south "
                     "edge; mountain %i vertices (%.0f%%)",west,east,t.biome_count[TERRAIN_BIOME_MOUNTAIN],
                     100.0f * t.biome_count[TERRAIN_BIOME_MOUNTAIN] / std::max<size_t>(1,n));
        }
        add_result("sealed",joined < 0 && west > 0 && east > 0,buf);

        /*
            --- Sides: each side of the chasm is ONE region (Grid.h, "THE SIDES") ---------------------
            Through the same flood: every bit of open plateau must be in a region that touches the
            south edge, and each side's must be one region - so nothing can be reached only by
            zeppelin. The generator draws its rifts to this (Builder::SidesWhole) on a coarser raster;
            this is the proof on the grid. A cut-off region is marked at its first vertex.
        */
        {
            std::vector<int> region_size(n,0);
            for (size_t v = 0; v < n; v++){
                if (t.level[v] == TERRAIN_PLATEAU && !t.Mountain((int)v)){
                    region_size[find((int)v)]++;
                }
            }
            int pieces[3] = {0,0,0};    //cut off, west, east
            int cut_vertices = 0;
            int largest_cut = 0;
            for (size_t r = 0; r < n; r++){
                if (region_size[r] == 0 || find((int)r) != (int)r){
                    continue;
                }
                int side = (sides[r] == 1) ? 1 : (sides[r] == 2 ? 2 : 0);
                if (sides[r] == 3){
                    continue;   //`sealed` reports it
                }
                pieces[side]++;
                if (side == 0){
                    cut_vertices += region_size[r];
                    largest_cut = std::max(largest_cut,region_size[r]);
                    add_issue("sides",(int)r,g.fine.pos[r],true);
                }
            }
            //Rivers run on the home side only: each fall's plateau behind it is the east's.
            int far_falls = 0;
            for (const TerrainFall& f : t.falls){
                vec2 back = f.lip - f.out * 4.0f;
                int best = -1;
                float bd = 1e30f;
                for (size_t v = 0; v < n; v++){
                    if (t.level[v] != TERRAIN_PLATEAU){
                        continue;
                    }
                    float d = (g.fine.pos[v] - back).length();
                    if (d < bd){
                        bd = d;
                        best = (int)v;
                    }
                }
                if (best < 0 || sides[find(best)] != 2){
                    far_falls++;
                    add_issue("sides",f.river,f.lip,true);
                }
            }
            snprintf(buf,sizeof(buf),"west %i region%s, east %i region%s; %i cut off (%i vertices, the largest %i); "
                     "%i of %i falls not on the home side",
                     pieces[1],pieces[1] == 1 ? "" : "s",pieces[2],pieces[2] == 1 ? "" : "s",pieces[0],
                     cut_vertices,largest_cut,far_falls,(int)t.falls.size());
            add_result("sides",pieces[0] == 0 && pieces[1] == 1 && pieces[2] == 1 && far_falls == 0,buf);
        }

        /*
            --- Balconies: at least one at home, each a level of its own, none cut off from its rim --
            A balcony is reached from the rim above it (by winch, later), so each must have ground of
            its own and plateau of its side right above it; the home side must have one at all.
        */
        {
            int count[2] = {0,0};
            int empty = 0;
            std::string detail;
            for (size_t f = 0; f < g.layout.features.size(); f++){
                const GridFeature& feature = g.layout.features[f];
                if (feature.kind != GRID_FEATURE_BALCONY){
                    continue;
                }
                count[feature.side > 0]++;
                int ground = 0;
                CrossingTable region;
                region.Add(feature.region.points);
                region.Build();
                for (size_t v = 0; v < n; v++){
                    ground += (t.level[v] == TERRAIN_BALCONY && region.Inside(g.fine.pos[v]));
                }
                if (ground < 20){
                    empty++;
                    add_issue("balconies",(int)f,feature.line.points[feature.line.points.size() / 2],true);
                }
                snprintf(buf,sizeof(buf),"%s %i vertices; ",feature.side > 0 ? "east" : "west",ground);
                detail += buf;
            }
            snprintf(buf,sizeof(buf),"east %i, west %i; %i with too little ground: ",count[1],count[0],empty);
            add_result("balconies",count[1] > 0 && empty == 0,std::string(buf) + detail);
        }

        /*
            --- Pockets: each one open, mostly buildable, and reachable from exactly one side --------
            Through the same flood as `sealed`: the region a pocket's meadow is in must touch the south
            edge's plateau on one side of the main mouth only - not none (shut in), not both (a way
            round the seal).
        */
        const ChasmLayout& lay = g.layout;
        /*
            Walking in needs more than the seal's flood: a walker cannot climb past a grade of 0.75
            (Walkers.h, WALKER_STEEPEST), so a pocket whose valley were too steep would pass `sealed`
            and still be shut. A second flood with that limit says whether its own side gets in.
        */
        std::vector<int> walk(n);
        for (size_t v = 0; v < n; v++){
            walk[v] = (int)v;
        }
        std::function<int(int)> find_walk = [&](int v){
            while (walk[v] != v){
                walk[v] = walk[walk[v]];
                v = walk[v];
            }
            return v;
        };
        for (const auto& e : g.fine.edges){
            int a = e.first;
            int b = e.second;
            if (t.level[a] != t.level[b] || t.Mountain(a) || t.Mountain(b)){
                continue;
            }
            float run = (g.fine.pos[a] - g.fine.pos[b]).length();
            if (std::fabs(t.ground[a] - t.ground[b]) > 0.75f * run){
                continue;
            }
            int ra = find_walk(a);
            int rb = find_walk(b);
            if (ra != rb){
                walk[std::max(ra,rb)] = std::min(ra,rb);
            }
        }
        std::vector<uint8_t> walk_sides(n,0);
        for (size_t v = 0; v < n; v++){
            const vec2& p = g.fine.pos[v];
            if (g.fine.f_boundary[v] && std::fabs(p.y - south) <= 1e-3f && t.level[v] == TERRAIN_PLATEAU &&
                !t.Mountain((int)v)){
                walk_sides[find_walk((int)v)] |= (p.x < mouth) ? 1 : 2;
            }
        }
        int bad = 0;
        std::string detail;
        for (size_t pi = 0; pi < lay.pockets.size(); pi++){
            const ChasmLayout::Pocket& k = lay.pockets[pi];
            int centre_v = -1;
            float best = 1e30f;
            int cells = 0;
            for (size_t v = 0; v < n; v++){
                if (t.biome[v] != TERRAIN_BIOME_POCKET || ChasmPocketDistance(k,g.fine.pos[v]) >= 0.0f){
                    continue;
                }
                cells++;
                float d = (g.fine.pos[v] - k.centre).length();
                if (d < best){
                    best = d;
                    centre_v = (int)v;
                }
            }
            int reach = (centre_v >= 0) ? sides[find(centre_v)] : 0;
            int walked = (centre_v >= 0) ? walk_sides[find_walk(centre_v)] : 0;
            bool f_ok = centre_v >= 0 && (reach == 1 || reach == 2) && walked == reach;
            char one[128];
            snprintf(one,sizeof(one),"%s%s pocket r %.0f: %i vertices, reached from %s",detail.empty() ? "" : "; ",
                     k.side < 0 ? "west" : "east",k.radius,cells,
                     reach == 1 ? "the west" : reach == 2 ? "the east" : reach == 3 ? "BOTH sides" : "NOWHERE");
            if (walked != reach){
                detail += one;
                snprintf(one,sizeof(one)," but WALKED from %s",walked == 0 ? "nowhere - too steep" : walked == 3 ? "both" : "the other side");
            }
            detail += one;
            if (!f_ok){
                bad++;
                add_issue("pocket",centre_v,k.centre,true);
            }
        }
        snprintf(buf,sizeof(buf),"%i of %i wanted (tries refused: room %i, rim %i, river %i, apart %i, valley %i): ",
                 (int)lay.pockets.size(),lay.pockets_wanted,lay.pocket_rejects[0],lay.pocket_rejects[1],
                 lay.pocket_rejects[2],lay.pocket_rejects[3],lay.pocket_rejects[4]);
        add_result("pockets",bad == 0,std::string(buf) + (detail.empty() ? "none placed" : detail));
    }
}
#endif

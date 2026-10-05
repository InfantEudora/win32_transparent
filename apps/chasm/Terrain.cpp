#include "Terrain.h"
#include "TerrainMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>

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
    return kind == GRID_FEATURE_RIM ? TERRAIN_PLATEAU : TERRAIN_SHARD;
}

void Terrain::Build(const Grid& g, const std::vector<GridLine>& features){
    auto t0 = std::chrono::steady_clock::now();
    size_t n = g.fine.pos.size();
    level.assign(n,TERRAIN_PLATEAU);
    for (int i = 0; i < TERRAIN_NUM_LEVELS; i++){
        level_count[i] = 0;
    }
    //Rims bound the floor; every other feature is closed and stands in it at shard level.
    CrossingTable rims;
    CrossingTable shards;
    for (size_t f = 0; f < features.size(); f++){
        if (g.LineKind(g.feature_line_base + (int)f) == GRID_FEATURE_RIM){
            rims.Add(features[f].points);
        }else{
            shards.Add(features[f].points);
        }
    }
    rims.Build();
    shards.Build();
    for (size_t v = 0; v < n; v++){
        /*
            A hair inside the map. A vertex on the east edge sits at exactly the x a rim ending
            there ends at, and the straddle test is half-open: with nothing beyond x_max it never
            counts that rim's end, and the whole east mouth of a rift came out plateau - a wall
            straight across it. (The west edge passes by the same asymmetry.)
        */
        vec2 p = g.fine.pos[v];
        p.x = std::max(g.bounds_min.x + 1e-3f,std::min(g.bounds_max.x - 1e-3f,p.x));
        int pin = g.fine.pin[v];
        uint8_t l = TERRAIN_PLATEAU;
        //Fixed vertices are the map's corners and the rims' ends - plateau, both, and a rim's end
        //sits exactly on its line, where the ray can go either way. A vertex on a line takes the
        //line's high side.
        if (pin == GRID_PIN_FIXED){
            l = TERRAIN_PLATEAU;
        }else if (pin >= g.feature_line_base){
            l = TerrainLevelOfKind(g.LineKind(pin));
        }else if (rims.Inside(p)){
            l = shards.Inside(p) ? TERRAIN_SHARD : TERRAIN_FLOOR;
        }
        level[v] = l;
        level_count[l]++;
    }
    //The biomes (TerrainBiomeAt), from the layout's mountain, pockets and south regions.
    biome.assign(n,TERRAIN_BIOME_TEMPERATE);
    for (int i = 0; i < TERRAIN_NUM_BIOMES; i++){
        biome_count[i] = 0;
    }
    for (size_t v = 0; v < n; v++){
        biome[v] = (uint8_t)TerrainBiomeAt(g.layout,g.fine.pos[v],level[v]);
        biome_count[biome[v]]++;
    }
    BuildRivers(g,features);
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

    //--- The edge raster: distance past the nearest water edge -------------------------------------
    //Out as far as the relief's fade along a river (BuildRelief), past the wet margin it was made for.
    const float reach = TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN + RELIEF_RIVER_FADE + 2.0f;
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
        bool f_rim = false;
        bool f_closed = false;
        for (size_t f = 0; f < features.size(); f++){
            bool f_is_rim = g.LineKind(g.feature_line_base + (int)f) == GRID_FEATURE_RIM;
            f_rim = f_rim || f_is_rim;
            f_closed = f_closed || !f_is_rim;
        }
        bool f_ok = t.level_count[TERRAIN_PLATEAU] > 0 &&
                    (!f_rim || t.level_count[TERRAIN_FLOOR] > 0) &&
                    (!f_closed || t.level_count[TERRAIN_SHARD] > 0);
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
        --- Mouths: a rift that reaches the map's edge is open there -----------------------------
        Every outline vertex between an open rim's two ends is floor. Steps cannot see a wall
        across a mouth - it is a plain two-level cliff - and one stood across every east mouth
        until the inside test was moved off the edge.
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
                if (f_on && g.fine.f_boundary[v] && t.level[v] != TERRAIN_FLOOR){
                    bad++;
                    add_issue("mouth",(int)v,p,true);
                }
            }
        }
        snprintf(buf,sizeof(buf),"%i mouths on the map's edge, %i outline vertices across them not floor",mouths,bad);
        add_result("mouths",bad == 0,buf);
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
            int kind = g.fine.pin[v] >= 0 ? g.LineKind(g.fine.pin[v]) : -1;
            if (kind < 0){
                continue;
            }
            pinned++;
            int want = TerrainLevelOfKind(kind);
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

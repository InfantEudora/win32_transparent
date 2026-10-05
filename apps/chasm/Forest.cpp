#include "Forest.h"

#include <algorithm>
#include <chrono>
#include <cmath>

const char* prop_asset_names[PROP_KIND_COUNT] = {
    "tree_pine_a","tree_pine_b","tree_pine_c","tree_pine_d",
    "tree_oak_a","tree_oak_b","tree_oak_c",
    "rock_a","rock_b","rock_c","rock_cluster_a",
    "stump_a","log_a","bush_a","bush_b",
    "grass_a","grass_b","grass_c","flowers_a","flowers_b",
    "fern_a","shrub_a","mushrooms_a","twig_a",
};

#define FOREST_SCALE        70.0f   //world units over which forest patches come and go
#define FOREST_DETAIL       22.0f   //the patches' ragged edges
#define FOREST_CORE         0.18f   //noise above this: dense forest
#define FOREST_EDGE         0.02f   //between this and the core: the thinning edge band
#define PROP_JITTER         0.60f   //how far a prop strays from its plot's vertex, world units
#define COVER_JITTER        0.90f   //ground cover spreads further, so it does not ring the trees
/*
    Trees at 0.7 of their modelled size: at the game's zoom A Little Age's trees stand about a house
    tall, and ours as modelled came out about twice that (the props window measured it).
*/
#define TREE_SCALE          0.70f

namespace {

//splitmix64 - a well-mixed hash of the seed, the plot and a salt, the only source of chance here.
uint64_t Mix(uint64_t x){
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

//0..1 from (seed, plot, salt).
float Chance(uint32_t seed, int plot, int salt){
    uint64_t h = Mix(((uint64_t)seed << 32) ^ (uint64_t)(uint32_t)plot ^ ((uint64_t)salt << 56));
    return (float)(h >> 40) / (float)(1ull << 24);
}

//Smooth value noise in -1..1, seeded - the same construction as the terrain's.
float Noise(float x, float z, uint32_t seed){
    float fx = std::floor(x), fz = std::floor(z);
    int32_t ix = (int32_t)fx, iz = (int32_t)fz;
    float tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    auto corner = [seed](int32_t cx, int32_t cz){
        uint64_t h = Mix(((uint64_t)seed << 40) ^ ((uint64_t)(uint32_t)cx << 20) ^ (uint64_t)(uint32_t)cz);
        return (float)(h >> 40) / (float)(1ull << 23) - 1.0f;
    };
    float a = corner(ix,iz) + (corner(ix + 1,iz) - corner(ix,iz)) * tx;
    float b = corner(ix,iz + 1) + (corner(ix + 1,iz + 1) - corner(ix,iz + 1)) * tx;
    return a + (b - a) * tz;
}

//Every corner of every cell round the plot on the plot's level - see ZoneCanHouse for the same rule.
bool PlotIsFlat(const Grid& g, const GridPicker& p, const Terrain& t, int plot){
    if (g.fine.f_boundary[plot] || t.wet[plot]){
        return false;   //the edge, or a river's bank (Terrain.h, RIVERS - wet is wider than a plot)
    }
    int level = t.level[plot];
    for (int i = 0; i < p.PlotQuadCount(plot); i++){
        const GridQuad& q = g.fine.quads[p.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            if (t.level[q.v[k]] != level){
                return false;
            }
        }
    }
    return true;
}

//One of `n` kinds starting at `first`, by weight.
int Pick(float r, int first, const float* weights, int n){
    float total = 0.0f;
    for (int i = 0; i < n; i++){
        total += weights[i];
    }
    float x = r * total;
    for (int i = 0; i < n; i++){
        if (x < weights[i]){
            return first + i;
        }
        x -= weights[i];
    }
    return first + n - 1;
}

}

void BuildForest(const Grid& g, const GridPicker& picker, const Terrain& t, const TerrainMeshData& mesh,
                 ForestData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = ForestData();
    out.chunk_props.assign(mesh.chunks.size(),std::vector<int>());
    const uint32_t seed = g.settings.seed;
    static const float pine_w[4] = {3.0f,2.0f,2.0f,3.0f};
    static const float oak_w[3] = {3.0f,2.0f,2.0f};
    static const float rock_w[4] = {1.0f,1.0f,2.0f,2.0f};

    int v = 0;
    //One prop at `at` on plot v's level: onto the relief, into the chunk its ground is in.
    auto Place = [&](int kind, vec2 at, float yaw, float scale){
        PropInstance p;
        p.kind = (uint8_t)kind;
        float level = terrain_levels[t.level[v]].height;
        p.pos = vec3(at.x,t.GroundHeight(at,level),at.y);
        p.yaw = yaw;
        p.scale = scale;
        GridPick under = picker.Pick(at);
        //The plot it STANDS IN, which is not always v's: a prop scattered across its plot can land
        //in the neighbour's, and a house painted there has to clear it.
        p.plot = under.f_hit ? under.plot : v;
        p.coarse = under.f_hit ? under.coarse_quad : -1;
        int chunk = under.f_hit ? TerrainChunkOfQuad(g,mesh,under.fine_quad) : 0;
        out.chunk_props[chunk].push_back((int)out.props.size());
        out.props.push_back(p);
        out.count[kind]++;
    };
    for (v = 0; v < (int)g.fine.pos.size(); v++){
        if (!PlotIsFlat(g,picker,t,v)){
            continue;
        }
        const vec2& base = g.fine.pos[v];
        /*
            The north mountain (biomes_plan.md) grows nothing yet: a scatter of rocks on the snow, no
            trees and no ground cover. Its own trees - the snow pine is modelled - come with its relief.
        */
        if (t.Mountain(v)){
            if (Chance(seed,v,1) < 0.035f){
                float a = Chance(seed,v,4) * 6.2831853f;
                float r = std::sqrt(Chance(seed,v,5)) * PROP_JITTER;
                Place(Pick(Chance(seed,v,3),PROP_ROCK_A,rock_w,4),base + vec2(std::cos(a),std::sin(a)) * r,
                      Chance(seed,v,6) * 6.2831853f,0.9f + Chance(seed,v,7) * 0.6f);
            }
            continue;
        }
        //A pocket (step 3) is there for what it holds: until resources exist, a show of stone.
        if (t.biome[v] == TERRAIN_BIOME_POCKET && Chance(seed,v,40) < 0.06f){
            float a = Chance(seed,v,41) * 6.2831853f;
            float r = std::sqrt(Chance(seed,v,42)) * PROP_JITTER;
            Place(PROP_ROCK_CLUSTER,base + vec2(std::cos(a),std::sin(a)) * r,Chance(seed,v,43) * 6.2831853f,
                  1.0f + Chance(seed,v,44) * 0.5f);
            continue;
        }
        float n = Noise(base.x / FOREST_SCALE,base.y / FOREST_SCALE,seed) * 0.75f +
                  Noise(base.x / FOREST_DETAIL,base.y / FOREST_DETAIL,seed + 7) * 0.25f;
        //The chasm floor is a darker, sparser place; the shard top bare stone and scrub.
        if (t.level[v] == TERRAIN_FLOOR){
            n -= 0.12f;
        }else if (t.level[v] == TERRAIN_SHARD){
            n -= 0.30f;
        }
        float roll = Chance(seed,v,1);
        int kind = -1;
        if (n > FOREST_CORE){
            if (roll < 0.88f){
                kind = Pick(Chance(seed,v,2),PROP_PINE_A,pine_w,4);
            }else if (roll < 0.91f){
                kind = (Chance(seed,v,2) < 0.5f) ? PROP_STUMP : PROP_LOG;
            }
        }else if (n > FOREST_EDGE){
            //The edge band thins toward the open land.
            float density = (n - FOREST_EDGE) / (FOREST_CORE - FOREST_EDGE);
            if (roll < 0.15f + 0.45f * density){
                float which = Chance(seed,v,2);
                kind = (which < 0.45f) ? Pick(Chance(seed,v,3),PROP_PINE_A,pine_w,4)
                     : (which < 0.80f) ? Pick(Chance(seed,v,3),PROP_OAK_A,oak_w,3)
                     : ((which < 0.92f) ? PROP_BUSH_A : PROP_BUSH_B);
            }
        }else{
            if (roll < 0.010f){
                kind = Pick(Chance(seed,v,3),PROP_OAK_A,oak_w,3);
            }else if (roll < 0.020f){
                kind = (Chance(seed,v,3) < 0.5f) ? PROP_BUSH_A : PROP_BUSH_B;
            }else if (roll < 0.028f){
                kind = Pick(Chance(seed,v,3),PROP_ROCK_A,rock_w,4);
            }
        }
        //Ground cover: up to two pieces a plot, its own rolls, so it neither depends on nor moves the
        //tree. Ferns, twigs and mushrooms under the trees; grass and flowers in the open.
        for (int c = 0; c < 2; c++){
            int salt = 10 + c * 8;
            bool f_forest = (n > FOREST_EDGE);
            if (Chance(seed,v,salt) >= (f_forest ? 0.55f : 0.40f)){
                continue;
            }
            static const float open_w[9] = {6.0f,5.0f,6.0f,1.5f,1.5f,0.5f,1.0f,0.1f,0.3f};
            static const float wood_w[9] = {3.0f,2.0f,3.0f,0.3f,0.3f,3.0f,1.5f,0.8f,1.5f};
            int cover = Pick(Chance(seed,v,salt + 1),PROP_COVER_FIRST,f_forest ? wood_w : open_w,9);
            float ca = Chance(seed,v,salt + 2) * 6.2831853f;
            float cr = std::sqrt(Chance(seed,v,salt + 3)) * COVER_JITTER;
            vec2 at = base + vec2(std::cos(ca),std::sin(ca)) * cr;
            Place(cover,at,Chance(seed,v,salt + 4) * 6.2831853f,0.85f + Chance(seed,v,salt + 5) * 0.30f);
        }
        if (kind < 0){
            continue;
        }
        bool f_tree = (kind <= PROP_OAK_C);
        float a = Chance(seed,v,4) * 6.2831853f;
        float r = std::sqrt(Chance(seed,v,5)) * PROP_JITTER;
        Place(kind,base + vec2(std::cos(a),std::sin(a)) * r,Chance(seed,v,6) * 6.2831853f,
              (0.85f + Chance(seed,v,7) * 0.30f) * (f_tree ? TREE_SCALE : 1.0f));
        /*
            A forest core's second tree, on the far side of the plot. One tree a plot, standing near
            its vertex, lines the trees up along the grid - visible as rows once they are scaled
            down to a house's height - and leaves the forest thin. Two, spread across the plot,
            make the shapeless clump the reference has.
        */
        if (n > FOREST_CORE && Chance(seed,v,30) < 0.75f){
            float a2 = a + 3.1415927f + (Chance(seed,v,31) - 0.5f) * 1.6f;
            float r2 = PROP_JITTER + Chance(seed,v,32) * (COVER_JITTER - PROP_JITTER);
            Place(Pick(Chance(seed,v,33),PROP_PINE_A,pine_w,4),base + vec2(std::cos(a2),std::sin(a2)) * r2,
                  Chance(seed,v,34) * 6.2831853f,(0.85f + Chance(seed,v,35) * 0.30f) * TREE_SCALE);
        }
    }
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

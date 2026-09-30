#include "Boulders.h"

#include <math.h>
#include <stdint.h>

static uint32_t Mix(uint32_t x){
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

//0..1 from a corner and a purpose - the Foliage kind of hash, so no shared random stream is touched.
static float HashUnit(uint32_t a, uint32_t b, uint32_t c){
    //Each input folded in through a full-avalanche mixer (lowbias32) in turn. A single multiply-xor
    //of all three first was tried and was not enough: small signed lattice indices came out within
    //a few percent of each other, and the ridge line was a plateau.
    uint32_t h = Mix(a + 0x9E3779B9u);
    h = Mix(h ^ b);
    h = Mix(h ^ c);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
}

static bool InsideAny(const std::vector<StageBlock>& blocks, float x, float y){
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& b = blocks[i];
        if (b.f_alive && x > b.Left() && x < b.Right() && y > b.Bottom() && y < b.Top()){
            return true;
        }
    }
    return false;
}

static bool IsSurface(const StageBlock& b){
    return b.f_alive && (b.kind == BLOCK_SOLID || b.kind == BLOCK_LEDGE);
}

static bool IsWall(const StageBlock& b){
    //Nor a crumble stone: a rock heaped against something about to fall away reads wrong too.
    return b.f_alive && b.kind != BLOCK_PLATFORM && b.kind != BLOCK_BREAKABLE && b.kind != BLOCK_CRUMBLE;
}

void FindBoulderCorners(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                        std::vector<BoulderCorner>& out){
    out.clear();
    const float eps = 0.02f;
    for (size_t a = 0; a < blocks.size(); a++){
        const StageBlock& A = blocks[a];
        if (!IsSurface(A)){
            continue;
        }
        const float top = A.Top();
        for (size_t w = 0; w < blocks.size(); w++){
            const StageBlock& W = blocks[w];
            if (w == a || !IsWall(W) || W.Bottom() > top + 0.05f || W.Top() < top + params.min_wall){
                continue;
            }
            //Its left face opens onto ground to the left of it, its right face to the right.
            for (int f = 0; f < 2; f++){
                float side = (f == 0) ? -1.0f : 1.0f;
                float xf = (f == 0) ? W.Left() : W.Right();
                float lo = (side < 0.0f) ? xf - params.room : xf;
                float hi = (side < 0.0f) ? xf : xf + params.room;
                if (lo < A.Left() - eps || hi > A.Right() + eps){
                    continue;
                }
                //Open ground, not the floor under something standing there.
                if (InsideAny(blocks,xf + side * params.room * 0.5f,top + 0.3f) ||
                    InsideAny(blocks,xf + side * 0.2f,top + 0.3f)){
                    continue;
                }
                bool f_seen = false;
                for (size_t k = 0; k < out.size(); k++){
                    if (out[k].top == (int)a && out[k].side == side && fabsf(out[k].x - xf) < 0.2f){
                        f_seen = true;
                        break;
                    }
                }
                if (!f_seen){
                    BoulderCorner c;
                    c.x = xf;
                    c.side = side;
                    c.top = (int)a;
                    out.push_back(c);
                }
            }
        }
    }
}

BoulderBiome BoulderBiomeFor(int biome){
    BoulderBiome r;
    if (biome == BIOME_CAVE){
        r.cluster_at_least = 1.0f;
        r.rubble = 0.6f;
        r.rubble_big = 0.12f;
    }
    return r;
}

//The rules at a spot: its biome's, scaled toward the jungle's (none) by how far into a fade it is.
static BoulderBiome RulesAt(const std::vector<StageBiome>* biomes, float x, float y){
    float w = 1.0f;
    BoulderBiome r = BoulderBiomeFor(BiomeAt(biomes,x,y,&w));
    r.cluster_at_least *= w;
    r.rubble *= w;
    return r;
}

//Clear of every rock already placed, in (x,z), by `slack` of the two radii.
static bool ClearOfRocks(const std::vector<Boulder>& out, const BoulderParams& params, float x, float z,
                         float r, float slack){
    for (const Boulder& o : out){
        float ro = params.radius[o.kind] * o.scale;
        float dx = x - o.x, dz = z - o.z;
        if (sqrtf(dx * dx + dz * dz) < (r + ro) * slack){
            return false;
        }
    }
    return true;
}

void ScatterBoulders(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                     std::vector<Boulder>& out, const std::vector<StageBiome>* biomes){
    out.clear();
    std::vector<BoulderCorner> corners;
    FindBoulderCorners(blocks,params,corners);

    for (size_t ci = 0; ci < corners.size(); ci++){
        const BoulderCorner& c = corners[ci];
        const StageBlock& A = blocks[c.top];
        //Seeded by WHERE the corner is, not by its place in the list, so an edit elsewhere in the
        //level does not reshuffle every cluster.
        const uint32_t seed = (uint32_t)(int32_t)floorf(c.x * 10.0f) * 2u + (c.side > 0.0f ? 1u : 0u);
        const uint32_t level = (uint32_t)(int32_t)floorf(A.Top() * 10.0f);
        //A biome may want more of them; the jungle's floor of 0 changes nothing.
        const float chance = fmaxf(params.cluster_chance,RulesAt(biomes,c.x,A.Top()).cluster_at_least);
        if (HashUnit(seed,level,0u) > chance){
            continue;
        }

        //--- The big one, into the corner and to the back ------------------------------------------
        /*
            Which of the big shapes, from its own hash purpose (4) so choosing it leaves every
            other draw for this corner where it was. Only among the kinds that have a size: one
            whose mesh did not load has radius 0, and would otherwise be picked and draw nothing.
        */
        int big_kinds[BOULDER_BIG_KINDS];
        int num_big = 0;
        for (int k = BOULDER_BIG_1; k < BOULDER_BIG_1 + BOULDER_BIG_KINDS; k++){
            if (params.radius[k] > 0.0f){
                big_kinds[num_big++] = k;
            }
        }
        if (num_big == 0){
            continue;
        }
        int pick = (int)(HashUnit(seed,level,4u) * (float)num_big);
        const int kind = big_kinds[pick < num_big ? pick : num_big - 1];

        //Its centre (1 - back_overhang) R in front of the back edge, so its front is at
        //back + (2 - back_overhang) R - which must not pass z_front_max.
        const float back = A.Back();
        const float depth_span = 2.0f - params.back_overhang;
        float s = params.big_scale_min + (params.big_scale_max - params.big_scale_min) * HashUnit(seed,level,1u);
        float R = params.radius[kind] * s;
        if (back + depth_span * R > params.z_front_max){
            R = (params.z_front_max - back) / depth_span;
            s = R / params.radius[kind];
        }
        if (s < params.big_scale_min * 0.5f){
            continue;       //this platform is too shallow behind her for a big rock
        }
        Boulder big;
        big.kind = kind;
        big.scale = s;
        //Slightly INTO the wall: the terrain's fillet fills the foot of a wall, and a rock standing
        //clear of it leaves a gap that reads as the rock floating.
        big.x = c.x + c.side * R * 0.85f;
        big.z = back + (1.0f - params.back_overhang) * R;
        big.ground = A.Top();
        big.y = A.Top() - params.sink * params.height[kind] * s;
        big.yaw = 6.2831853f * HashUnit(seed,level,2u);
        out.push_back(big);

        //--- The small ones, round its base on the open side ---------------------------------------
        size_t first_small = out.size();
        int span = params.small_max - params.small_min + 1;
        int count = params.small_min + (int)(HashUnit(seed,level,3u) * (float)(span > 0 ? span : 1));
        if (count > params.small_max){
            count = params.small_max;
        }
        for (int j = 0, tries = 0; j < count && tries < count * 16; tries++){
            uint32_t t = 16u + (uint32_t)tries * 8u;
            float ss = params.small_scale_min
                     + (params.small_scale_max - params.small_scale_min) * HashUnit(seed,level,t);
            float r = params.radius[BOULDER_SMALL_1] * ss;
            float x = big.x + c.side * (R * 0.2f + HashUnit(seed,level,t + 1u) * (R + params.small_reach));
            //Up to half of it past the back edge, like the big one - a band from the back edge to
            //z_front_max holds nothing much bigger than a pebble otherwise.
            float zlo = A.Back() + params.back_inset;
            float zhi = params.z_front_max - r;
            if (zhi < zlo){
                continue;       //this one is too big to fit; the next may not be
            }
            float z = zlo + (zhi - zlo) * HashUnit(seed,level,t + 2u);
            //On this top, on the open side of the face, clear of the big one and of each other.
            if (x - r < A.Left() || x + r > A.Right() || c.side * (x - c.x) < r * 0.5f){
                continue;
            }
            float dxb = x - big.x, dzb = z - big.z;
            if (sqrtf(dxb * dxb + dzb * dzb) < (R + r) * 0.8f){
                continue;
            }
            bool f_clear = true;
            for (size_t k = first_small; k < out.size(); k++){
                float dx = x - out[k].x, dz = z - out[k].z;
                float rk = params.radius[BOULDER_SMALL_1] * out[k].scale;
                if (sqrtf(dx * dx + dz * dz) < (r + rk) * 0.9f){
                    f_clear = false;
                    break;
                }
            }
            if (!f_clear){
                continue;
            }
            Boulder b;
            b.kind = BOULDER_SMALL_1;
            b.scale = ss;
            b.x = x;
            b.z = z;
            b.ground = A.Top();
            b.y = A.Top() - params.sink * params.height[BOULDER_SMALL_1] * ss;
            b.yaw = 6.2831853f * HashUnit(seed,level,t + 3u);
            b.tilt = (params.small_tilt_deg * 3.14159265f / 180.0f) * HashUnit(seed,level,t + 4u);
            b.tilt_axis_yaw = 6.2831853f * HashUnit(seed,level,t + 5u);
            out.push_back(b);
            j++;
        }
    }

    /*
        RUBBLE, where a biome asks for it: small rocks strewn along the open tops, now and then a
        big one, as if come down from the roof. After the clusters, and clear of them. A candidate
        every half unit, seeded by WHERE it is, like a corner, so an edit elsewhere moves none of
        it; and only where a biome's rubble is above zero is anything drawn at all - so the
        jungle's rocks are exactly what they were.
    */
    if (!biomes || biomes->empty()){
        return;
    }
    const float rubble_step = 0.5f;
    for (size_t a = 0; a < blocks.size(); a++){
        const StageBlock& A = blocks[a];
        if (!IsSurface(A)){
            continue;
        }
        const float top = A.Top();
        const uint32_t level = (uint32_t)(int32_t)floorf(top * 10.0f);
        for (float x = A.Left() + rubble_step * 0.5f; x < A.Right(); x += rubble_step){
            const BoulderBiome rules = RulesAt(biomes,x,top);
            if (rules.rubble <= 0.0f){
                continue;
            }
            const uint32_t seed = (uint32_t)(int32_t)floorf(x * 10.0f) * 2u + 1000003u;
            if (HashUnit(seed,level,40u) >= rules.rubble * rubble_step){
                continue;
            }
            //Open ground: nothing standing on the top here.
            if (InsideAny(blocks,x,top + 0.3f)){
                continue;
            }
            const bool f_big = HashUnit(seed,level,41u) < rules.rubble_big;
            int kind = BOULDER_SMALL_1;
            float sc;
            if (f_big){
                kind = (HashUnit(seed,level,42u) < 0.5f) ? BOULDER_BIG_1 : BOULDER_BIG_2;
                if (params.radius[kind] <= 0.0f){
                    kind = BOULDER_BIG_1;
                }
                sc = params.big_scale_min + (params.big_scale_max - params.big_scale_min) * HashUnit(seed,level,43u) * 0.6f;
            }else{
                sc = params.small_scale_min + (params.small_scale_max - params.small_scale_min) * HashUnit(seed,level,43u);
            }
            if (params.radius[kind] <= 0.0f){
                continue;
            }
            float r = params.radius[kind] * sc;
            //Off the half-unit lattice, so a row of them does not read as one.
            const float bx = x + rubble_step * 0.4f * (2.0f * HashUnit(seed,level,45u) - 1.0f);
            //Behind her walking line, like every rock; a big one to the back, as a cluster's is.
            float zlo = A.Back() + params.back_inset;
            float zhi = params.z_front_max - r;
            if (f_big){
                float zb = A.Back() + (1.0f - params.back_overhang) * r;
                if (zb + r > params.z_front_max){
                    continue;
                }
                zlo = zhi = zb;
            }
            if (zhi < zlo || bx - r < A.Left() || bx + r > A.Right()){
                continue;
            }
            float z = zlo + (zhi - zlo) * HashUnit(seed,level,44u);
            if (!ClearOfRocks(out,params,bx,z,r,0.9f)){
                continue;
            }
            Boulder b;
            b.kind = kind;
            b.scale = sc;
            b.x = bx;
            b.z = z;
            b.ground = top;
            b.y = top - params.sink * params.height[kind] * sc;
            b.yaw = 6.2831853f * HashUnit(seed,level,46u);
            if (!f_big){
                b.tilt = (params.small_tilt_deg * 3.14159265f / 180.0f) * HashUnit(seed,level,47u);
                b.tilt_axis_yaw = 6.2831853f * HashUnit(seed,level,48u);
            }
            out.push_back(b);
        }
    }
}

#include "Backdrop.h"
#include "Water.h"

#include <math.h>
#include <stdint.h>

static uint32_t Mix(uint32_t x){
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

//0..1 from a place and a purpose - the Foliage kind of hash, so no shared random stream is touched.
static float HashUnit(uint32_t a, uint32_t b, uint32_t c = 0){
    //Each input folded in through a full-avalanche mixer (lowbias32) in turn. A single multiply-xor
    //of all three first was tried and was not enough: small signed lattice indices came out within
    //a few percent of each other, and the ridge line was a plateau.
    uint32_t h = Mix(a + 0x9E3779B9u);
    h = Mix(h ^ b);
    h = Mix(h ^ c);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
}

//Value noise along x, 0..1: a hashed height at every wavelength, smoothstepped between. Smooth, so
//the ridge line rolls; seeded by the ground block, so two bays do not share a skyline.
static float RidgeNoise(float x, float wavelength, uint32_t seed){
    float u = x / (wavelength > 0.1f ? wavelength : 0.1f);
    float k = floorf(u);
    float f = u - k;
    f = f * f * (3.0f - 2.0f * f);
    float a = HashUnit(seed,(uint32_t)(int32_t)k,11u);
    float b = HashUnit(seed,(uint32_t)(int32_t)(k + 1.0f),11u);
    return a + (b - a) * f;
}

//Nothing under it anywhere in the level, and nothing it stands on: the bottom of the world.
static bool IsGround(const StageBlock& b, const std::vector<StageBlock>& all){
    for (size_t k = 0; k < all.size(); k++){
        const StageBlock& o = all[k];
        if (&o == &b || !o.f_alive || o.Right() <= b.Left() || o.Left() >= b.Right()){
            continue;
        }
        if (o.Top() <= b.Bottom() + 0.05f){
            return false;       //something is under it, or holding it up
        }
    }
    return true;
}

static StageBlock Column(float left, float right, float bottom, float top, float front, float half_depth){
    StageBlock b;
    b.kind = BLOCK_SOLID;
    b.x = (left + right) * 0.5f;
    b.hw = (right - left) * 0.5f;
    b.y = (top + bottom) * 0.5f;
    b.hh = (top - bottom) * 0.5f;
    b.depth = half_depth;
    b.z = front - half_depth;
    return b;
}

void BuildBackdropBlocks(const std::vector<StageBlock>& blocks, float x_min, float x_max,
                         float y_min, float y_max, const BackdropParams& params,
                         std::vector<StageBlock>& out, std::vector<BackdropTree>* trees,
                         std::vector<int>* grounds, const std::vector<StageWater>* waters){
    out.clear();
    if (trees){
        trees->clear();
    }
    if (grounds){
        grounds->clear();
    }
    std::vector<StageBlock> ridges;
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& g = blocks[i];
        if (g.kind != BLOCK_SOLID || !g.f_alive || g.f_invisible ||
            g.x < x_min || g.x >= x_max || g.y < y_min || g.y >= y_max || !IsGround(g,blocks)){
            continue;
        }
        if (grounds){
            grounds->push_back((int)i);
        }
        const uint32_t seed = (uint32_t)i;
        const float left = g.Left() - params.overhang_x;
        const float right = g.Right() + params.overhang_x;
        const float bottom = g.Bottom() - params.drop_below;

        //--- The wall ------------------------------------------------------------------------------
        int n = (int)ceilf((right - left) / (params.segment > 0.1f ? params.segment : 0.1f));
        if (n < 1){
            n = 1;
        }
        const float width = (right - left) / (float)n;
        const float wall_front = g.Back() - params.wall_gap;
        std::vector<float> tops(n);
        for (int k = 0; k < n; k++){
            float cx = left + width * ((float)k + 0.5f);
            float t = RidgeNoise(cx,params.wall_wavelength,seed);
            float top = g.Top() + params.wall_low + (params.wall_high - params.wall_low) * t
                      + params.wall_jitter * (2.0f * HashUnit(seed,(uint32_t)k,1u) - 1.0f);
            if (top < g.Top() + 0.2f){
                top = g.Top() + 0.2f;
            }
            tops[k] = top;
        }

        /*
            The waterfalls standing on this ground, cut in AFTER the ridge line is drawn and
            before anything is built from it: every hash above is still drawn for every column,
            so with no water the bank is bit for bit what it was, and with one only the columns
            the water touches move.
        */
        std::vector<WaterLayout> falls;
        std::vector<float> lips;
        if (waters){
            for (const StageWater& w : *waters){
                if (w.x >= g.Left() && w.x < g.Right()){
                    WaterLayout l;
                    LayoutWater(w,g,params,WaterParams(),l);
                    falls.push_back(l);
                    lips.push_back(w.lip_y);
                }
            }
        }
        std::vector<float> fronts(n,wall_front);
        std::vector<char> notched(n,0);
        for (size_t f = 0; f < falls.size(); f++){
            for (int k = 0; k < n; k++){
                float core_l = left + width * (float)k;
                if (core_l + width > falls[f].notch_l && core_l < falls[f].notch_r){
                    tops[k] = lips[f];
                    fronts[k] = falls[f].recess_front;
                    notched[k] = 1;
                }
            }
        }
        //And the cleft's two sides, up over the lip - wherever the ridge line happened to be low.
        for (size_t f = 0; f < falls.size(); f++){
            for (int k = 0; k < n; k++){
                bool f_beside = !notched[k] && ((k > 0 && notched[k - 1]) || (k < n - 1 && notched[k + 1]));
                if (f_beside && tops[k] < lips[f] + params.notch_rise){
                    tops[k] = lips[f] + params.notch_rise;
                }
            }
        }
        for (int k = 0; k < n; k++){
            float l = left + width * (float)k - params.overlap;
            float r = l + width + 2.0f * params.overlap;
            out.push_back(Column(l,r,bottom,tops[k],fronts[k],params.wall_half_depth));
        }

        //--- The trees, on the wall's high points -------------------------------------------------
        if (trees){
            float last_x = -1e30f;
            for (int k = 0; k < n; k++){
                float top = tops[k];
                //Near the top of its neighbourhood: a neighbour much higher raises this column's
                //top by its fillet, which would bury the foot of a pine standing in it.
                bool f_high = (k == 0 || top >= tops[k - 1] - params.tree_max_step) &&
                              (k == n - 1 || top >= tops[k + 1] - params.tree_max_step);
                float cx = left + width * ((float)k + 0.5f);
                //Not in a notch: the water runs over its top.
                if (notched[k] || !f_high || top < g.Top() + params.tree_min_rise || cx - last_x < params.tree_spacing ||
                    HashUnit(seed,(uint32_t)k,2u) > params.tree_chance){
                    continue;
                }
                BackdropTree t;
                //Inside the middle of the column, where its top is pinned flat and no neighbour's
                //fillet has raised it.
                t.x = left + width * ((float)k + 0.5f) + width * 0.3f * (2.0f * HashUnit(seed,(uint32_t)k,3u) - 1.0f);
                t.y = top;
                t.z = wall_front - params.wall_half_depth
                    + params.wall_half_depth * 0.6f * (2.0f * HashUnit(seed,(uint32_t)k,4u) - 1.0f);
                t.yaw = 6.2831853f * HashUnit(seed,(uint32_t)k,5u);
                t.scale = params.tree_scale_min
                        + (params.tree_scale_max - params.tree_scale_min) * HashUnit(seed,(uint32_t)k,6u);
                trees->push_back(t);
                last_x = cx;
            }
        }

        //--- The ridges, standing forward of it -----------------------------------------------------
        int candidates = (int)floorf((right - left) / (params.ridge_spacing > 0.1f ? params.ridge_spacing : 0.1f));
        for (int k = 0; k < candidates; k++){
            if (HashUnit(seed,(uint32_t)k,7u) > params.ridge_chance){
                continue;
            }
            float cx = left + params.ridge_spacing * ((float)k + 0.5f)
                     + params.ridge_spacing * 0.3f * (2.0f * HashUnit(seed,(uint32_t)k,8u) - 1.0f);
            float hw = params.ridge_hw_min + (params.ridge_hw_max - params.ridge_hw_min) * HashUnit(seed,(uint32_t)k,9u);
            //None in a waterfall's pool, and the ones along its stream stand back from it.
            bool f_in_pool = false;
            float front = g.Back() - params.front_gap;
            for (const WaterLayout& l : falls){
                f_in_pool = f_in_pool || (cx + hw > l.clear_l && cx - hw < l.clear_r);
                if (cx + hw > l.channel_l && cx - hw < l.channel_r && front > l.channel_back){
                    front = l.channel_back;
                }
            }
            if (f_in_pool){
                continue;
            }
            //Under the wall where it stands, so it reads as part of it rather than a pillar before it.
            int col = (int)floorf((cx - left) / width);
            col = (col < 0) ? 0 : ((col >= n) ? n - 1 : col);
            float drop = params.ridge_drop_min
                       + (params.ridge_drop_max - params.ridge_drop_min) * HashUnit(seed,(uint32_t)k,10u);
            float top = tops[col] - drop;
            if (top < g.Top() + 0.5f){
                top = g.Top() + 0.5f;
            }
            //From its own front back INTO the wall, so the two are one piece of rock.
            float half_depth = (front - (wall_front - 0.5f)) * 0.5f;
            ridges.push_back(Column(cx - hw,cx + hw,bottom,top,front,half_depth));
        }
    }
    out.insert(out.end(),ridges.begin(),ridges.end());
}

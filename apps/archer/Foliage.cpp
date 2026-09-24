#include <math.h>
#include <stdint.h>

#include "Foliage.h"
#include "PlaceHash.h"

/*
    The garden. See Foliage.h for the three steps and why each is shaped the way it is.

    Like Stage.cpp, nothing in here includes an engine header - `make rules` links it into
    stage_test.exe with no core, and it has to stay that way to stay testable like that.
*/

static const float FOLIAGE_PI  = 3.14159265358979f;
static const float FOLIAGE_EPS = 0.001f;

//--- Randomness -----------------------------------------------------------------------------------
//Hash01 is in PlaceHash.h, shared with the vines - see there for why it is a hash, not a stream.

//The channels a spot draws from, so no two decisions share a number.
enum{ CH_ACCEPT = 0, CH_KIND, CH_Z, CH_SCALE, CH_YAW };

//--- Occlusion ------------------------------------------------------------------------------------
/*
    Distance along a ray to a box, or -1 for a miss within max_t. The slab test, in 2D. A ray that
    starts inside the box hits it at 0, which is what a spot tucked under an overhang should see.
*/
static float RayBox(float ox, float oy, float dx, float dy, float max_t, const StageBlock& b){
    float t0 = 0.0f;
    float t1 = max_t;
    const float o[2]  = { ox, oy };
    const float d[2]  = { dx, dy };
    const float lo[2] = { b.Left(),  b.Bottom() };
    const float hi[2] = { b.Right(), b.Top() };
    for (int a = 0; a < 2; a++){
        if (fabsf(d[a]) < 1e-8f){
            if (o[a] < lo[a] || o[a] > hi[a]){
                return -1.0f;       //parallel to this slab and outside it
            }
            continue;
        }
        float inv = 1.0f / d[a];
        float ta = (lo[a] - o[a]) * inv;
        float tb = (hi[a] - o[a]) * inv;
        if (ta > tb){ float t = ta; ta = tb; tb = t; }
        if (ta > t0){ t0 = ta; }
        if (tb < t1){ t1 = tb; }
        if (t0 > t1){
            return -1.0f;
        }
    }
    return t0;
}

float FoliageOcclusion(const std::vector<StageBlock>& blocks, float x, float y,
                       const FoliageParams& params){
    int n = (params.ao_rays > 0) ? params.ao_rays : 1;
    float reach = (params.ao_radius > FOLIAGE_EPS) ? params.ao_radius : FOLIAGE_EPS;
    //Just above the surface, so the block being stood on is not in the way of its own rays.
    float oy = y + 0.02f;
    float sum = 0.0f;
    for (int i = 0; i < n; i++){
        //Evenly across the upper half-circle, never exactly horizontal: a ray along the top of the
        //surface would graze a neighbour at the same height and call that shade.
        float a = FOLIAGE_PI * ((float)i + 0.5f) / (float)n;
        float dx = cosf(a);
        float dy = sinf(a);
        float nearest = -1.0f;
        for (size_t k = 0; k < blocks.size(); k++){
            if (!blocks[k].f_alive){
                continue;
            }
            float t = RayBox(x,oy,dx,dy,reach,blocks[k]);
            if (t >= 0.0f && (nearest < 0.0f || t < nearest)){
                nearest = t;
            }
        }
        if (nearest >= 0.0f){
            /*
                Counted by how close the hit is, so the score falls off smoothly away from a wall
                rather than stepping from 1 to 0 at ao_radius.

                QUADRATIC, NOT LINEAR. With 1 - t/r a wall one unit away scored 0.17 against 0.37
                at its foot - the slant rays that do reach it arrive at t = d/cos(a), already most
                of the way out - so the "corner" was a sliver a plant could barely fit in, and the
                rules test measured it as 1.3x the open floor. 1 - (t/r)^2 holds near 1 for close
                hits and only fades toward the edge of the reach, which is the shape a shadow in
                a corner actually has.
            */
            float f = nearest / reach;
            sum += 1.0f - f * f;
        }
    }
    return sum / (float)n;
}

//--- Placement ------------------------------------------------------------------------------------

/*
    Is there room for a plant of this radius and height standing at x on block `self`?

    No if any OTHER live block sits across the spot - its bottom below the plant's head, its top
    above the surface - within the plant's clearance. That one test covers all three cases: a box
    standing on this one (the stack), a wall rising from it (clearance keeps the plant off its
    face), and a ceiling too low to grow under. A neighbour at exactly the same height is not in
    the way, because its top is not above the surface.
*/
static bool HasRoom(const std::vector<StageBlock>& blocks, size_t self, float x,
                    float radius, float height, float clearance){
    const StageBlock& b = blocks[self];
    float surface = b.Top();
    for (size_t k = 0; k < blocks.size(); k++){
        if (k == self || !blocks[k].f_alive){
            continue;
        }
        const StageBlock& o = blocks[k];
        if (o.Top() <= surface + FOLIAGE_EPS){
            continue;       //at or below the surface: not in the way
        }
        if (o.Bottom() >= surface + height){
            continue;       //high enough to grow under
        }
        if (x > o.Left() - clearance && x < o.Right() + clearance){
            return false;
        }
    }
    return true;
}

static int ChooseKind(float shade, float u){
    //Ferns in the shade, flowers in the open, the low fern evenly everywhere to fill in between.
    float w[FOLIAGE_KIND_COUNT];
    w[FOLIAGE_FERN]     = 0.15f + 1.2f * shade;
    w[FOLIAGE_FERN_LOW] = 0.45f;
    w[FOLIAGE_FLOWER]   = 0.05f + 0.9f * (1.0f - shade) * (1.0f - shade);
    float total = w[0] + w[1] + w[2];
    float pick = u * total;
    for (int k = 0; k < FOLIAGE_KIND_COUNT; k++){
        if (pick < w[k]){
            return k;
        }
        pick -= w[k];
    }
    return FOLIAGE_KIND_COUNT - 1;
}

void ScatterFoliage(const std::vector<StageBlock>& blocks, const std::vector<bool>& grows,
                    const FoliageParams& params, std::vector<FoliagePlant>& out){
    out.clear();
    float step = (params.step > 0.005f) ? params.step : 0.005f;
    int tries = (params.tries_per_step > 0) ? params.tries_per_step : 1;

    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& b = blocks[i];
        if (i >= grows.size() || !grows[i] || !b.f_alive){
            continue;
        }
        if (b.kind != BLOCK_SOLID && b.kind != BLOCK_LEDGE){
            continue;       //no plants on a one-way platform or on a wall about to be kicked in
        }
        float y = b.Top();
        float x0 = b.Left() + params.edge_inset;
        float x1 = b.Right() - params.edge_inset;
        for (float x = x0; x <= x1; x += step){
            /*
                "Fully in a corner" is HALF the sky blocked, not all of it - a wall on one side
                takes the rays on that side and leaves the rest open. So the raw score is doubled
                before it is shaped, and a slot with walls on both sides saturates rather than
                counting for more than a corner does.
            */
            float shade = FoliageOcclusion(blocks,x,y,params) * 2.0f;
            if (shade > 1.0f){ shade = 1.0f; }
            if (params.ao_gamma > 0.0f && shade > 0.0f){
                shade = powf(shade,params.ao_gamma);
            }
            float density = params.density_open + (params.density_corner - params.density_open) * shade;
            //Plants per unit length, spread over the steps and the tries within a step.
            float p = density * step / (float)tries;

            for (int t = 0; t < tries; t++){
                if (Hash01(x,y,t,CH_ACCEPT) >= p){
                    continue;
                }
                FoliagePlant plant;
                plant.kind = ChooseKind(shade,Hash01(x,y,t,CH_KIND));
                plant.x = x;
                plant.y = y;
                float uz = powf(Hash01(x,y,t,CH_Z),params.z_bias);
                plant.z = params.z_back + (params.z_front - params.z_back) * uz;
                plant.scale = 1.0f + params.scale_jitter * (2.0f * Hash01(x,y,t,CH_SCALE) - 1.0f);
                plant.yaw = 2.0f * FOLIAGE_PI * Hash01(x,y,t,CH_YAW);
                plant.occlusion = shade;

                float r = params.radius[plant.kind] * plant.scale;
                float h = params.height[plant.kind] * plant.scale;
                if (!HasRoom(blocks,i,x,r,h,r * params.wall_clearance)){
                    continue;
                }
                //Spacing against everything placed so far, on this block or its neighbours. A few
                //hundred plants, so the plain loop is cheaper than anything cleverer would be.
                bool f_clear = true;
                for (size_t q = 0; q < out.size() && f_clear; q++){
                    const FoliagePlant& o = out[q];
                    if (fabsf(o.y - y) > 0.5f){
                        continue;   //a different storey
                    }
                    float ro = params.radius[o.kind] * o.scale;
                    float need = params.spacing * ((r > ro) ? r : ro);
                    float dx = o.x - x;
                    float dz = o.z - plant.z;
                    if (dx * dx + dz * dz < need * need){
                        f_clear = false;
                    }
                }
                if (f_clear){
                    out.push_back(plant);
                }
            }
        }
    }
}

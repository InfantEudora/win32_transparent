#include <math.h>
#include <stdint.h>

#include "TerrainField.h"

/*
    See TerrainField.h. Nothing here includes an engine header - that is the point of the file.
*/

//--- the field ----------------------------------------------------------------------------------

/*
    Signed distance to one block: negative inside, matching core/MarchingCubes.h's convention.

    THE HALF EXTENTS ARE (hw, hh - r, depth) AND NOT (hw - r, hh - r, depth - r), which is the
    whole of the top-pinning rule in one line. Adding r back at the end then puts the flat top at
    b.y + (hh - r) + r == Top() exactly, and the bottom at Bottom() exactly, while x and z grow by
    r. So the rounding rolls over the edge OUTSIDE the collider's footprint and the entire width
    the archer can stand on is at the height the archer's sweep thinks it is.

    r is clamped to 0.9 * hh rather than used as given, so that a block shallower than 2r still
    gets an exact top instead of one lifted by the leftover.

    Written for any box - centre (cx, cy, cz), half extents (hw, hh, hd) - because the cap is the
    same shape at a different size: see SdCap.
*/
static float SdRoundBox(const vec3& p, float cx, float cy, float cz, float hw, float hh, float hd,
                        float r){
    if (r > hh * 0.9f){
        r = hh * 0.9f;
    }
    if (r < 0.0f){
        r = 0.0f;
    }
    float dx = fabsf(p.x - cx) - hw;
    float dy = fabsf(p.y - cy) - (hh - r);
    float dz = fabsf(p.z - cz) - hd;

    float ox = (dx > 0.0f) ? dx : 0.0f;
    float oy = (dy > 0.0f) ? dy : 0.0f;
    float oz = (dz > 0.0f) ? dz : 0.0f;
    float outside = sqrtf(ox*ox + oy*oy + oz*oz);

    float m = (dx > dy) ? dx : dy;
    if (dz > m){ m = dz; }
    float inside = (m < 0.0f) ? m : 0.0f;

    return outside + inside - r;
}

//The body: the block's own box, its front and back set in so the cap overhangs them.
static float SdBody(const vec3& p, const StageBlock& b, const TerrainParams& params){
    float hd = b.HalfDepth() - params.body_inset_z;
    if (hd < 0.1f){
        hd = 0.1f;
    }
    return SdRoundBox(p,b.x,b.y,b.z,b.hw,b.hh,hd,params.round_r);
}

/*
    The grass cap: a slab whose TOP is the block's top and whose thickness is cap_thickness plus
    the drip at this (x, z). Top-pinned by the same outward rounding as the body, so a drip only
    ever lowers the underside.

    SCALED DOWN ON A BLOCK UNDER 1.5 TALL, in proportion. The tiles' grass is about a fifth of
    their height, and at full thickness the 0.8-tall stones came out more than half grass - green
    lumps with a little earth under them. The clamp after it is the backstop for anything thinner.
*/
static float SdCap(const vec3& p, const StageBlock& b, const TerrainParams& params, float drip){
    float scale = (2.0f * b.hh) / 1.5f;
    if (scale > 1.0f){ scale = 1.0f; }
    float half = 0.5f * (params.cap_thickness + drip) * scale;
    if (half > 0.85f * b.hh){
        half = 0.85f * b.hh;
    }
    return SdRoundBox(p,b.x,b.Top() - half,b.z,b.hw + params.cap_lip_x,half,
                      b.HalfDepth() + params.cap_lip_z,params.cap_round);
}

/*
    The belly under a floating block: an ellipsoid centred on the collider's bottom face. Its
    height is held below the top by root_k plus a margin, so its smooth blend into the body can
    never reach up and lift the top face.

    The distance is the usual ellipsoid approximation (k0 * (k0 - 1) / k1) - not exact, but
    continuous and well-behaved near the surface, which is all marching cubes needs.
*/
static float SdRoot(const vec3& p, const StageBlock& b, const TerrainParams& params){
    float ry = params.root_scale * b.hw;
    if (ry > params.root_max){ ry = params.root_max; }
    float cap = 2.0f * b.hh - params.root_k - 0.05f;
    if (ry > cap){ ry = cap; }
    if (ry < 0.05f){
        return 1e30f;
    }
    float rx = 0.8f * b.hw;
    float rz = 0.8f * b.HalfDepth();
    vec3 q(p.x - b.x,p.y - b.Bottom(),p.z - b.z);
    float ax = q.x / rx, ay = q.y / ry, az = q.z / rz;
    float k0 = sqrtf(ax * ax + ay * ay + az * az);
    float bx = q.x / (rx * rx), by = q.y / (ry * ry), bz = q.z / (rz * rz);
    float k1 = sqrtf(bx * bx + by * by + bz * bz);
    if (k1 < 1e-9f){
        return -ry;
    }
    return k0 * (k0 - 1.0f) / k1;
}

/*
    Polynomial smooth minimum - the standard one.

    smin(a,b) <= min(a,b) always, so a smooth union only ever makes the solid BIGGER. That is the
    property the top-pinning argument leans on: this term cannot pull a top face down, only push
    an inside corner up. k of 0 degenerates to a hard min rather than dividing by zero, which is a
    legitimate setting - it is what "welded boxes, no blending" means.
*/
static float SmoothMin(float a, float b, float k){
    if (k <= 1e-6f){
        return (a < b) ? a : b;
    }
    float h = 0.5f + 0.5f * (b - a) / k;
    if (h < 0.0f){ h = 0.0f; }
    if (h > 1.0f){ h = 1.0f; }
    return (b * (1.0f - h) + a * h) - k * h * (1.0f - h);
}

//--- the ramps (docs/terrain_plan.md section 12) -------------------------------------------------

/*
    Exact signed distance to a convex polygon in the play plane - the standard winding form. Exact
    rather than the max of the edges' half-planes, because that max is wrong outside a corner, and
    the rounding added to it afterwards would then come out as a bevel instead of an arc.
*/
static float SdPolygon2(const float* vx, const float* vy, int n, float px, float py){
    float d = (px - vx[0]) * (px - vx[0]) + (py - vy[0]) * (py - vy[0]);
    float s = 1.0f;
    for (int i = 0, j = n - 1;i < n;j = i, i++){
        float ex = vx[j] - vx[i], ey = vy[j] - vy[i];
        float wx = px - vx[i], wy = py - vy[i];
        float t = (wx * ex + wy * ey) / (ex * ex + ey * ey);
        if (t < 0.0f){ t = 0.0f; }
        if (t > 1.0f){ t = 1.0f; }
        float bx = wx - ex * t, by = wy - ey * t;
        float dd = bx * bx + by * by;
        if (dd < d){ d = dd; }
        bool c1 = py >= vy[i], c2 = py < vy[j], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)){
            s = -s;
        }
    }
    return s * sqrtf(d);
}

/*
    One ramp's shape, worked out once per sample: which end is high, the slope's unit vectors, and
    the drop in y that moving its line by round_r along its normal comes to.
*/
struct RampShape{
    float len, ux, uy;          //along the ramp, a to b
    float nx, ny;               //its upward normal
    float sinq, cosq;           //of its angle from level
    bool  f_rising;             //the high end is b, on the right
};

static RampShape ShapeOf(const StageRamp& r){
    RampShape s;
    float dx = r.b.x - r.a.x, dy = r.b.y - r.a.y;
    s.len = sqrtf(dx * dx + dy * dy);
    s.ux = dx / s.len;
    s.uy = dy / s.len;
    s.nx = -s.uy;
    s.ny = s.ux;
    s.cosq = s.ux;
    s.sinq = fabsf(s.uy);
    s.f_rising = dy > 0.0f;
    return s;
}

float TerrainRampHalfDepth(){
    //Ramps have no depth of their own: a default block's, which is what seals them.
    return STAGE_BLOCK_HALF_DEPTH;
}

float TerrainRampBottom(const StageRamp& r, const TerrainParams& params){
    RampShape s = ShapeOf(r);
    float lo = (r.a.y < r.b.y) ? r.a.y : r.b.y;
    return lo - (params.ramp_bury + params.round_r + params.round_r / s.cosq);
}

/*
    The cut at the high end: the half-space on the ramp's own side of the vertical plane through
    that end. Both pieces run on past the end, so the outward rounding there cannot sag the last
    stretch of slope, and this takes the overrun off again - inside the block the ramp leans on.
    A flat clip at the high end's height instead would put the ramp's top in the block's top, and
    the smooth union lifts two coinciding tops by k/4 (see TerrainFieldAt).
*/
static float RampHighCut(const vec3& p, const StageRamp& r, const RampShape& s){
    return s.f_rising ? (p.x - r.b.x) : (r.a.x - p.x);
}

/*
    The body: the solid UNDER the line, through the slab - a trapezoid with the ramp for a top, its
    sides vertical at the ends and its bottom buried below the low end, extruded and rounded.

    TOP-PINNED THE SAME WAY AS SdRoundBox, said for a tilted face: the top edge is moved down by
    round_r along its normal and round_r is added back by the rounding, so the plane of the slope is
    exactly the rules' line. The sides are not moved in, so the rounding rolls outward past the
    ends - and the high end's side is pushed out by r sin q more, which is what the rounding there
    would otherwise eat off the top, before RampHighCut takes it away.

    `grow_z` stands its front and back out further, and `cut_out` moves the cut that far on past the
    high end: the same shape, bigger, for cutting the blocks' caps out from under a ramp in
    TerrainFieldAt.
*/
static float SdRampBody(const vec3& p, const StageRamp& r, const RampShape& s, const TerrainParams& params,
                        float grow_z = 0.0f, float cut_out = 0.0f){
    float rr = params.round_r;
    float lift = rr / s.cosq;           //round_r along the normal, as a drop straight down
    float over = rr * s.sinq + cut_out;
    float xl = r.a.x - (s.f_rising ? 0.0f : over);
    float xr = r.b.x + (s.f_rising ? over : 0.0f);
    float m = r.Slope();
    float bottom = TerrainRampBottom(r,params) + rr;
    float vx[4] = { xl, xr, xr, xl };
    float vy[4] = { r.a.y + m * (xl - r.a.x) - lift, r.a.y + m * (xr - r.a.x) - lift, bottom, bottom };
    float d2 = SdPolygon2(vx,vy,4,p.x,p.y);

    float hd = TerrainRampHalfDepth() - params.body_inset_z + grow_z;
    if (hd < 0.1f){
        hd = 0.1f;
    }
    float dz = fabsf(p.z) - hd;
    float ox = (d2 > 0.0f) ? d2 : 0.0f;
    float oz = (dz > 0.0f) ? dz : 0.0f;
    float m2 = (d2 > dz) ? d2 : dz;
    float d = sqrtf(ox * ox + oz * oz) + ((m2 < 0.0f) ? m2 : 0.0f) - rr;
    float cut = RampHighCut(p,r,s) - cut_out;
    return (cut > d) ? cut : d;
}

/*
    The cap: a slab of grass along the slope, its top on the line - SdCap's slab turned to the
    ramp's angle and pinned in the ramp's own normal, as SdCap's is in y. Run on past the LOW end by
    the lip, where it goes into the floor's grass, and not past the high end, where its rounding is
    beyond the cut anyway.
*/
static float SdRampCap(const vec3& p, const StageRamp& r, const RampShape& s, const TerrainParams& params,
                       float drip){
    float half = 0.5f * (params.cap_thickness + drip);
    float qx = p.x - r.a.x, qy = p.y - r.a.y;
    float along = qx * s.ux + qy * s.uy;
    float up = qx * s.nx + qy * s.ny;
    //Along the ramp from its a end: [lo_s, hi_s], the lip only at the low end.
    float lo_s = s.f_rising ? -params.cap_lip_x : 0.0f;
    float hi_s = s.f_rising ? s.len : s.len + params.cap_lip_x;
    vec3 local(along,up,p.z);
    float d = SdRoundBox(local,(lo_s + hi_s) * 0.5f,-half,0.0f,(hi_s - lo_s) * 0.5f,half,
                         TerrainRampHalfDepth() + params.cap_lip_z,params.cap_round);
    float cut = RampHighCut(p,r,s);
    return (cut > d) ? cut : d;
}

//Integer hash to a float in [-1,1]. Cheap, deterministic, and good enough to be lost under a
//lighting pass - this is displacement on dirt, not a texture anyone will look at closely.
static float Hash3(int x, int y, int z){
    uint32_t h = (uint32_t)(x * 374761393) + (uint32_t)(y * 668265263) + (uint32_t)(z * 2147483647);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return ((float)(h & 0xFFFFFF) / (float)0x7FFFFF) - 1.0f;
}

static float Smootherstep(float t){
    return t * t * (3.0f - 2.0f * t);
}

//Trilinearly interpolated value noise on the integer lattice.
static float ValueNoise(const vec3& p){
    float fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    float tx = Smootherstep(p.x - fx);
    float ty = Smootherstep(p.y - fy);
    float tz = Smootherstep(p.z - fz);

    float c00 = Hash3(ix,iy,iz)       * (1-tx) + Hash3(ix+1,iy,iz)       * tx;
    float c10 = Hash3(ix,iy+1,iz)     * (1-tx) + Hash3(ix+1,iy+1,iz)     * tx;
    float c01 = Hash3(ix,iy,iz+1)     * (1-tx) + Hash3(ix+1,iy,iz+1)     * tx;
    float c11 = Hash3(ix,iy+1,iz+1)   * (1-tx) + Hash3(ix+1,iy+1,iz+1)   * tx;

    float c0 = c00 * (1-ty) + c10 * ty;
    float c1 = c01 * (1-ty) + c11 * ty;
    return c0 * (1-tz) + c1 * tz;
}

/*
    How much of the noise applies here: 0 on and above every block's top face, ramping to 1 at
    noise_fade below it.

    NOISE IS THE ONE TERM THAT CAN PUSH A SURFACE DOWN THROUGH A COLLIDER. Rounding is arranged
    not to (see SdRoundBox) and smooth union cannot (see SmoothMin), so without this the whole
    top-pinning argument is worth nothing - a landing surface would be as bumpy as the amplitude,
    in both directions.

    Taken as a MINIMUM over the blocks, so being under any one block's top is enough to be
    attenuated. Outside a block's footprint that block does not attenuate at all, which is what
    lets a cliff face keep its full displacement right up to the lip.

    --- IT RAMPS OUT HORIZONTALLY TOO, AND THAT IS NOT COSMETIC ---------------------------------
    The obvious spelling of "outside the footprint, this block does not attenuate" is to skip the
    block entirely, and that was what this did. It is wrong in a way that is invisible in the
    arithmetic and extremely visible on screen: attenuation then JUMPS from 0 to 1 across the
    plane x == b.hw + round_r, so the field jumps by a whole noise_amp there, so the gradient at
    that plane is enormous and points sideways - and core/MarchingCubes.cpp reads its normals off
    that gradient. The result was a hard black band down the side of every raised block, which
    reads as a shadow bug or a broken material and is neither.

    So the horizontal edge gets the same smoothstep ramp the vertical one has. A field sampled for
    its gradient has to be continuous everywhere, not just where it is convenient.

    THE SAME RAMP IN z, from the block's depth outward. Within the depth is where the plants stand
    (and, at z 0, where she does), so the top is left exact there; beyond it is the cap's lip,
    which nothing stands on, and noise there is what turns a ruled edge into a grass edge.
*/
static float NoiseAttenuation(const vec3& p, const std::vector<const StageBlock*>& blocks,
                              const std::vector<const StageRamp*>& ramps, const TerrainParams& params){
    float fade = params.noise_fade;
    if (fade <= 1e-6f){
        return 1.0f;
    }
    //How far this block's own surface actually reaches past its collider sideways: the body's
    //rounding or the cap's lip, whichever is further - see SdBody and SdCap.
    float reach_x = params.round_r;
    if (params.cap_lip_x + params.cap_round > reach_x){
        reach_x = params.cap_lip_x + params.cap_round;
    }
    float atten = 1.0f;
    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        //How far below this block's top: 0 at the face, 1 a full fade under it.
        float below = (b.Top() - p.y) / fade;
        if (below < 0.0f){ below = 0.0f; }
        if (below > 1.0f){ below = 1.0f; }
        //How far outside its footprint: 0 within, 1 a full fade clear of it.
        float outside = (fabsf(p.x - b.x) - (b.hw + reach_x)) / fade;
        if (outside < 0.0f){ outside = 0.0f; }
        if (outside > 1.0f){ outside = 1.0f; }
        //And past its depth, into the lip.
        float out_z = (fabsf(p.z - b.z) - b.HalfDepth()) / fade;
        if (out_z < 0.0f){ out_z = 0.0f; }
        if (out_z > 1.0f){ out_z = 1.0f; }

        //Full noise if well below the top, or well clear of the block sideways or in depth.
        float a = Smootherstep(below);
        float b_out = Smootherstep(outside);
        if (b_out > a){
            a = b_out;
        }
        float z_out = Smootherstep(out_z);
        if (z_out > a){
            a = z_out;
        }
        if (a < atten){
            atten = a;
        }
    }
    //A ramp the same way, its line standing in for the top: noise-free on and just under the
    //slope within the slab's depth, full noise past it in z, where the grass edge should wobble.
    for (size_t i = 0;i < ramps.size();i++){
        const StageRamp& r = *ramps[i];
        float x = p.x;
        if (x < r.a.x){ x = r.a.x; }
        if (x > r.b.x){ x = r.b.x; }
        float below = (r.SurfaceY(x) - p.y) / fade;
        if (below < 0.0f){ below = 0.0f; }
        if (below > 1.0f){ below = 1.0f; }
        float past = (p.x < r.a.x) ? (r.a.x - p.x) : ((p.x > r.b.x) ? (p.x - r.b.x) : 0.0f);
        float outside = (past - reach_x) / fade;
        if (outside < 0.0f){ outside = 0.0f; }
        if (outside > 1.0f){ outside = 1.0f; }
        float out_z = (fabsf(p.z) - TerrainRampHalfDepth()) / fade;
        if (out_z < 0.0f){ out_z = 0.0f; }
        if (out_z > 1.0f){ out_z = 1.0f; }
        float a = Smootherstep(below);
        float b_out = Smootherstep(outside);
        if (b_out > a){
            a = b_out;
        }
        float z_out = Smootherstep(out_z);
        if (z_out > a){
            a = z_out;
        }
        if (a < atten){
            atten = a;
        }
    }
    return atten;
}

//The drip at (x, z): 0 across most of a cap, rising to drip_amp in rounded tongues.
static float Drip(const vec3& p, const TerrainParams& params){
    if (params.drip_amp <= 1e-6f){
        return 0.0f;
    }
    //A fixed y slice of the same value noise, so the tongues are columns rather than blobs.
    float n = 0.5f + 0.5f * ValueNoise(vec3(p.x * params.drip_freq,17.3f,p.z * params.drip_freq));
    float t = (n - 0.45f) / 0.35f;
    if (t < 0.0f){ t = 0.0f; }
    if (t > 1.0f){ t = 1.0f; }
    return params.drip_amp * Smootherstep(t);
}

/*
    The whole field at a point, and - for the material pass - the nearest cap and the nearest body
    on their own. See "THE SHAPE, THROUGH THE SLAB" in Terrain.h for why a block's cap and body are
    a hard min and blocks are a smooth one.
*/
void TerrainRampSet::Gather(const std::vector<StageRamp>& level, const TerrainRegion& region,
                            const TerrainParams& params){
    own.clear();
    cuts.clear();
    for (const StageRamp& r : level){
        if (region.Contains(r)){
            own.push_back(&r);
        }
        /*
            By its run alone, not by height: a region's y range is about block MIDDLES, and a tall
            block in it can stand well up into a ramp above the split (the shelf under the pitch). A
            cut too many costs a sample's worth of time and removes only grass already inside a
            wedge, which is drawn solid whichever bay draws it.
        */
        if (r.b.x >= region.x_min && r.a.x < region.x_max){
            cuts.push_back(&r);
        }
    }
}

float TerrainFieldAt(const vec3& p, const std::vector<const StageBlock*>& blocks,
                   const std::vector<bool>& floating, const TerrainRampSet& ramp_set,
                   const TerrainParams& params, float* out_cap, float* out_body){
    const std::vector<const StageRamp*>& ramps = ramp_set.own;
    float drip = Drip(p,params);
    /*
        THE BLOCKS' GRASS DOES NOT RUN ON UNDER A RAMP. A floor's top goes on beneath a wedge, and so
        does its cap - whose lip and drips stand out past the wedge's earth face as a green ledge
        under the slope. So every block's cap loses whatever is inside a wedge, the wedge stood out
        in depth by the cap's own reach so the lip goes too. Its top is the ramp's line, so nothing
        above the slope is touched; a region with no ramps skips it, and adds up as it always did.
    */
    float under_ramp = 1e30f;
    if (!ramp_set.cuts.empty()){
        /*
            Deeper again by the noise's reach, and a margin: the noise pushes the earth face out
            past the cut's front, and grass is whatever is nearer a cap than a body - a cut whose
            boundary sat within reach of that face handed it back the grass, in green blobs.
        */
        float grow = params.body_inset_z + params.cap_lip_z + params.cap_round +
                     params.noise_amp + params.coarse_amp + 0.3f;
        /*
            And on past the high end by a cap's reach in x, where a block under the ramp - the shelf
            under the pitch - would otherwise poke the last of its lip out beside the cut. Only below
            the block at the high end's own grass and drips, which are the top of the ramp's ground.
        */
        float reach = params.cap_lip_x + params.cap_round + 0.05f;
        float keep = params.cap_thickness + params.drip_amp + params.cap_round;
        for (const StageRamp* r : ramp_set.cuts){
            RampShape s = ShapeOf(*r);
            float w = SdRampBody(p,*r,s,params,grow);
            float hi = s.f_rising ? r->b.y : r->a.y;
            float past = SdRampBody(p,*r,s,params,grow,reach);
            float under_top = p.y - (hi - keep);
            if (under_top > past){ past = under_top; }
            if (past < w){ w = past; }
            if (w < under_ramp){ under_ramp = w; }
        }
    }
    float d = 1e30f;
    float cap_min = 1e30f, body_min = 1e30f;
    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        float body = SdBody(p,b,params);
        if (floating[i]){
            body = SmoothMin(body,SdRoot(p,b,params),params.root_k);
        }
        float cap = SdCap(p,b,params,drip);
        if (-under_ramp > cap){
            cap = -under_ramp;
        }
        float db = (cap < body) ? cap : body;
        d = (i == 0) ? db : SmoothMin(d,db,params.smooth_k);
        /*
            And inside a cut no block's cap owns grass at all - only the ramp's own does. Its
            distance there is to grass that is gone, and the earth the noise pushes out of the
            wedge's face, being further from its body than that, was coming out green.
        */
        if (cap < cap_min && under_ramp >= 0.0f){ cap_min = cap; }
        if (body < body_min){ body_min = body; }
    }
    //The ramps after every block, so a region without any adds up exactly as it did before them.
    for (size_t i = 0;i < ramps.size();i++){
        const StageRamp& r = *ramps[i];
        RampShape s = ShapeOf(r);
        float body = SdRampBody(p,r,s,params);
        float cap = SdRampCap(p,r,s,params,drip);
        float dr = (cap < body) ? cap : body;
        d = (blocks.empty() && i == 0) ? dr : SmoothMin(d,dr,params.smooth_k);
        if (cap < cap_min){ cap_min = cap; }
        if (body < body_min){ body_min = body; }
    }
    if (params.noise_amp > 1e-6f || params.coarse_amp > 1e-6f){
        float atten = NoiseAttenuation(p,blocks,ramps,params);
        if (atten > 0.0f){
            vec3 np(p.x * params.noise_freq,p.y * params.noise_freq,p.z * params.noise_freq);
            d += ValueNoise(np) * params.noise_amp * atten;
            //The coarse octave, off near the walk line - see coarse_z0.
            float span = params.coarse_z1 - params.coarse_z0;
            float w = (span > 1e-6f) ? (fabsf(p.z) - params.coarse_z0) / span : 1.0f;
            if (w < 0.0f){ w = 0.0f; }
            if (w > 1.0f){ w = 1.0f; }
            w = Smootherstep(w);
            if (w > 0.0f){
                //Offset from the fine octave's lattice, so the two do not share their zeros.
                vec3 cp(p.x * params.coarse_freq + 31.7f,p.y * params.coarse_freq + 5.1f,
                        p.z * params.coarse_freq + 11.9f);
                d += ValueNoise(cp) * params.coarse_amp * atten * w;
            }
        }
    }
    if (out_cap){ *out_cap = cap_min; }
    if (out_body){ *out_body = body_min; }
    return d;
}

/*
    Which of `blocks` float: nothing sits under them, but something - anywhere in the level, not
    only in this bay - is below them. That is the stones and the island and not the floor, which
    has nothing under it, nor a wall standing on it. `all` rather than the bay's own, because the
    upper bay's stones float over the ground bay's floor.
*/
std::vector<bool> TerrainFindFloating(const std::vector<const StageBlock*>& blocks,
                                      const std::vector<StageBlock>& all){
    std::vector<bool> floating(blocks.size(),false);
    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        bool f_supported = false, f_over = false;
        for (size_t k = 0;k < all.size();k++){
            const StageBlock& o = all[k];
            if (&o == &b || !o.f_alive || o.Right() <= b.Left() || o.Left() >= b.Right()){
                continue;
            }
            if (fabsf(o.Top() - b.Bottom()) < 0.05f){
                f_supported = true;
            }
            if (o.Top() < b.Bottom() - 0.05f){
                f_over = true;
            }
        }
        floating[i] = f_over && !f_supported;
    }
    return floating;
}


//--- a region's surface, to sample -------------------------------------------------------------

void TerrainSurface::Build(const std::vector<StageBlock>& all, const TerrainRegion& r, const TerrainParams& p,
                           const std::vector<StageRamp>* level_ramps){
    region = r;
    params = p;
    own.clear();
    for (const StageBlock& b : all){
        if (region.Contains(b)){
            own.push_back(b);
        }
    }
    blocks.clear();
    for (const StageBlock& b : own){
        blocks.push_back(&b);
    }
    floating = TerrainFindFloating(blocks,all);
    own_ramps.clear();
    if (level_ramps){
        own_ramps = *level_ramps;
    }
    ramps.Gather(own_ramps,region,params);
}

float TerrainSurface::Distance(const vec3& p) const{
    if (blocks.empty() && ramps.own.empty()){
        return 1e30f;
    }
    return TerrainFieldAt(p,blocks,floating,ramps,params);
}

vec3 TerrainSurface::Normal(const vec3& p) const{
    const float h = 0.01f;
    vec3 g(Distance(vec3(p.x + h,p.y,p.z)) - Distance(vec3(p.x - h,p.y,p.z)),
           Distance(vec3(p.x,p.y + h,p.z)) - Distance(vec3(p.x,p.y - h,p.z)),
           Distance(vec3(p.x,p.y,p.z + h)) - Distance(vec3(p.x,p.y,p.z - h)));
    float l = g.length();
    return (l > 1e-9f) ? g / l : vec3(0.0f,0.0f,0.0f);
}

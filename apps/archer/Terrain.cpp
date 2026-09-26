#include "Terrain.h"
#include "MarchingCubes.h"
#include "Debug.h"

#include <math.h>

static Debugger* debug = new Debugger("Terrain",DEBUG_INFO);

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
                              const TerrainParams& params){
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
static float Field(const vec3& p, const std::vector<const StageBlock*>& blocks,
                   const std::vector<bool>& floating, const TerrainParams& params,
                   float* out_cap = NULL, float* out_body = NULL){
    float drip = Drip(p,params);
    float d = 1e30f;
    float cap_min = 1e30f, body_min = 1e30f;
    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        float body = SdBody(p,b,params);
        if (floating[i]){
            body = SmoothMin(body,SdRoot(p,b,params),params.root_k);
        }
        float cap = SdCap(p,b,params,drip);
        float db = (cap < body) ? cap : body;
        d = (i == 0) ? db : SmoothMin(d,db,params.smooth_k);
        if (cap < cap_min){ cap_min = cap; }
        if (body < body_min){ body_min = body; }
    }
    if (params.noise_amp > 1e-6f || params.coarse_amp > 1e-6f){
        float atten = NoiseAttenuation(p,blocks,params);
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
static std::vector<bool> FindFloating(const std::vector<const StageBlock*>& blocks,
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

//--- measuring the top-pinning ------------------------------------------------------------------

/*
    Is this point buried inside some OTHER block? If it is, the top face beneath it is not a
    surface at all and measuring it would measure the thing standing on it.

    Without this the probe at a floor's centre would report the wall standing on that floor as an
    enormous error, and the one number that is supposed to mean "the archer floats" would be
    meaningless on every level with anything stacked on anything.
*/
static bool IsBuried(const vec3& p, const std::vector<const StageBlock*>& blocks,
                     const StageBlock* skip, float round_r){
    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        if (&b == skip){
            continue;
        }
        /*
            HORIZONTALLY THE FOOTPRINT IS GROWN BY round_r, VERTICALLY IT IS NOT.

            That asymmetry is exactly the asymmetry SdRoundBox builds in: the rounding is pushed
            outward in x and z and is absent in y, so a block's VISUAL extent is r wider than its
            collider and exactly as tall. A probe inside that margin is standing under a
            neighbour's overhanging turf, and the surface it finds is that neighbour's side - which
            was being reported as this block's top rising by the full height of the wall next to
            it, a number that says nothing about anything.
        */
        if (p.x > b.Left() - round_r && p.x < b.Right() + round_r &&
            p.y > b.Bottom() && p.y < b.Top()){
            return true;
        }
    }
    return false;
}

/*
    Walks across every exposed top face and reports how far the iso-surface strayed from it.

    A measurement rather than a look, because the whole error is a fraction of a unit: 0.2 of a
    unit of float is invisible in a screenshot and unmistakable under the feet, and the plan this
    was built from asks for it to be checked rather than eyeballed for exactly that reason.
*/
static void MeasureTops(const std::vector<const StageBlock*>& blocks, const std::vector<bool>& floating,
                        const TerrainParams& params, TerrainStats& stats){
    //The overhang IsBuried has to allow for: the body's rounding or the cap's lip, the further.
    float reach_x = params.round_r;
    if (params.cap_lip_x + params.cap_round > reach_x){
        reach_x = params.cap_lip_x + params.cap_round;
    }
    const int   samples_across = 9;
    const float probe_down     = 0.60f;     //how far BELOW Top() to keep looking before giving up
    const float probe_step     = 0.005f;    //a fortieth of a 0.2 rounding; well under what matters

    /*
        THE PROBE STARTS ABOVE THE WHOLE BAY, not a fixed distance above the face being measured.

        It used to start at Top() + 0.6, and that quietly turned the measurement into a lie at any
        large smooth_k: a fillet running up the side of a tall neighbour lifts the ground well
        above the face it started from, the probe then begins INSIDE the solid, finds no sign
        change at all, and the "no crossing" sentinel reports it as the deepest possible dip. Three
        of four variants failed that way while the field was in fact correct.
    */
    float y_start = blocks[0]->Top();
    for (size_t i = 1;i < blocks.size();i++){
        if (blocks[i]->Top() > y_start){
            y_start = blocks[i]->Top();
        }
    }
    y_start += params.round_r + params.noise_amp + params.coarse_amp + params.smooth_k + 0.5f;

    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        for (int s = 0;s < samples_across;s++){
            //Inset a little from the corners: the outermost sliver of a top face is where the
            //rounding of a NEIGHBOURING block legitimately reaches, and measuring it would report
            //that neighbour's fillet as this block's error.
            float t = (samples_across == 1) ? 0.5f : ((float)s / (float)(samples_across - 1));
            float x = b.Left() + (b.Right() - b.Left()) * (0.08f + 0.84f * t);
            vec3 probe(x,b.Top() + 0.01f,0.0f);
            if (IsBuried(probe,blocks,&b,reach_x)){
                continue;   //something is standing here; this is not an exposed top face
            }

            float y_lo = b.Top() - probe_down;
            int   steps = (int)((y_start - y_lo) / probe_step) + 1;
            float prev_y = y_start;
            float prev_d = Field(vec3(x,y_start,0.0f),blocks,floating,params);
            bool  f_found = false;
            float surface_y = y_start;
            for (int k = 1;k <= steps;k++){
                float y = y_start - probe_step * (float)k;
                if (y < y_lo){ y = y_lo; }
                float d = Field(vec3(x,y,0.0f),blocks,floating,params);
                if ((prev_d > 0.0f) != (d > 0.0f)){
                    float denom = d - prev_d;
                    float u = (fabsf(denom) > 1e-12f) ? (-prev_d / denom) : 0.5f;
                    surface_y = prev_y + (y - prev_y) * u;
                    f_found = true;
                    break;
                }
                prev_y = y;
                prev_d = d;
            }
            if (!f_found){
                //Genuinely no surface anywhere above this point, all the way down to below the
                //face. That is a hole in the terrain over ground the archer can stand on, which is
                //the worst thing this measurement exists to catch.
                if (probe_down > stats.worst_dip){
                    stats.worst_dip = probe_down;
                }
                stats.num_probes++;
                continue;
            }

            float delta = surface_y - b.Top();
            if (delta < 0.0f && -delta > stats.worst_dip){
                stats.worst_dip = -delta;
            }
            if (delta > 0.0f && delta > stats.worst_rise){
                stats.worst_rise = delta;
            }
            stats.num_probes++;
        }
    }
}

//--- the build ----------------------------------------------------------------------------------

bool BuildTerrainVerts(const std::vector<StageBlock>& blocks, const TerrainRegion& region,
                       const TerrainParams& params, std::vector<vertex>& out,
                       TerrainStats* stats){
    //Only BLOCK_SOLID melts - see the header - and TerrainRegion::Contains says so.
    std::vector<const StageBlock*>mine;
    for (size_t i = 0;i < blocks.size();i++){
        if (region.Contains(blocks[i])){
            mine.push_back(&blocks[i]);
        }
    }
    if (mine.empty()){
        return false;
    }

    std::vector<bool> floating = FindFloating(mine,blocks);

    /*
        The sampled box has to hold everything the field can reach, not just the blocks.

        The rounding and the cap's lip push the surface out, both noises push it further, a root
        hangs below, and marching cubes needs a ring of samples OUTSIDE the surface to close it off
        - without that the terrain is cut flat wherever it meets the edge of the grid and the slab
        is left open at the front and back. Two cells of margin on top of every displacement is
        what makes the surface close itself rather than needing capping logic.
    */
    float reach = params.round_r;
    if (params.cap_lip_x + params.cap_round > reach){ reach = params.cap_lip_x + params.cap_round; }
    if (params.cap_lip_z + params.cap_round > reach){ reach = params.cap_lip_z + params.cap_round; }
    float margin = reach + params.noise_amp + params.coarse_amp + 2.0f * params.cell_xy;
    float margin_z = reach + params.noise_amp + params.coarse_amp + 2.0f * params.cell_z;
    float lo_x = mine[0]->Left(), hi_x = mine[0]->Right();
    float lo_y = mine[0]->Bottom(), hi_y = mine[0]->Top();
    float lo_z = mine[0]->Back(), hi_z = mine[0]->Front();
    for (size_t i = 0;i < mine.size();i++){
        const StageBlock& b = *mine[i];
        if (b.Left()   < lo_x){ lo_x = b.Left(); }
        if (b.Right()  > hi_x){ hi_x = b.Right(); }
        float bottom = b.Bottom() - (floating[i] ? params.root_max : 0.0f);
        if (bottom     < lo_y){ lo_y = bottom; }
        if (b.Top()    > hi_y){ hi_y = b.Top(); }
        if (b.Back()   < lo_z){ lo_z = b.Back(); }
        if (b.Front()  > hi_z){ hi_z = b.Front(); }
    }
    lo_x -= margin; hi_x += margin;
    lo_y -= margin; hi_y += margin;
    lo_z -= margin_z; hi_z += margin_z;

    MCGrid grid;
    grid.origin = vec3(lo_x,lo_y,lo_z);
    grid.cell   = vec3(params.cell_xy,params.cell_xy,params.cell_z);
    grid.nx = (int)ceilf((hi_x - lo_x) / params.cell_xy) + 1;
    grid.ny = (int)ceilf((hi_y - lo_y) / params.cell_xy) + 1;
    grid.nz = (int)ceilf((hi_z - lo_z) / params.cell_z)  + 1;

    std::vector<float>density(grid.SampleCount());
    for (int z = 0;z < grid.nz;z++){
        for (int y = 0;y < grid.ny;y++){
            for (int x = 0;x < grid.nx;x++){
                density[grid.Index(x,y,z)] = Field(grid.Position(x,y,z),mine,floating,params);
            }
        }
    }

    size_t first = out.size();
    if (!MarchingCubes(grid,density,out)){
        return false;
    }

    /*
        --- MATERIALS, per vertex ------------------------------------------------------------
        Slot 0 grass, 1 soil, 2 rock. The engine carries a material index per vertex natively
        (vertex.matid, read from the VBO bound as an SSBO - see core/Mesh.h), so this costs no
        shader work at all and no texture.

        It is worth knowing that default.vert declares vmatindex `flat`, so the material is
        effectively PER TRIANGLE and the grass/soil boundary is a hard jagged line rather than a
        gradient. A soft blend would need a custom shader and one of the free interpolating
        channels, and is deliberately not attempted here.

        GRASS IS WHAT THE CAP OWNS - a vertex nearer some cap than any body - which is the top,
        the lip and the drips hanging over the earth, the way the authored tiles are painted. The
        noise is left out of the comparison: it displaces both pieces alike. Earth splits by slope.
    */
    for (size_t i = first;i < out.size();i++){
        vertex& v = out[i];
        float cap = 0.0f, body = 0.0f;
        Field(v.pos,mine,floating,params,&cap,&body);
        if (cap <= body + params.cap_eps){
            v.matid = 0;
        }else if (v.normal.y < params.rock_ny){
            v.matid = 2;        //a face or an underside
        }else{
            v.matid = 1;
        }
    }

    if (stats){
        stats->num_blocks    = (int)mine.size();
        stats->num_triangles = (int)((out.size() - first) / 3);
        stats->num_samples   = grid.SampleCount();
        MeasureTops(mine,floating,params,*stats);
    }
    return true;
}

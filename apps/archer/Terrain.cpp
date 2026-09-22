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
    gets an exact top instead of one lifted by the leftover. Nothing in the test bay is that thin;
    a future level will be.
*/
static float SdBlock(const vec3& p, const StageBlock& b, float depth, float r){
    if (r > b.hh * 0.9f){
        r = b.hh * 0.9f;
    }
    if (r < 0.0f){
        r = 0.0f;
    }
    float dx = fabsf(p.x - b.x) - b.hw;
    float dy = fabsf(p.y - b.y) - (b.hh - r);
    float dz = fabsf(p.z) - depth;

    float ox = (dx > 0.0f) ? dx : 0.0f;
    float oy = (dy > 0.0f) ? dy : 0.0f;
    float oz = (dz > 0.0f) ? dz : 0.0f;
    float outside = sqrtf(ox*ox + oy*oy + oz*oz);

    float m = (dx > dy) ? dx : dy;
    if (dz > m){ m = dz; }
    float inside = (m < 0.0f) ? m : 0.0f;

    return outside + inside - r;
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
    not to (see SdBlock) and smooth union cannot (see SmoothMin), so without this the whole
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
*/
static float NoiseAttenuation(const vec3& p, const std::vector<const StageBlock*>& blocks,
                              float fade, float round_r){
    if (fade <= 1e-6f){
        return 1.0f;
    }
    float atten = 1.0f;
    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        //How far below this block's top: 0 at the face, 1 a full fade under it.
        float below = (b.Top() - p.y) / fade;
        if (below < 0.0f){ below = 0.0f; }
        if (below > 1.0f){ below = 1.0f; }
        //How far outside its footprint: 0 within, 1 a full fade clear of it. The footprint is the
        //collider's grown by the rounding, because that is how far this block's own surface
        //actually reaches - see SdBlock.
        float outside = (fabsf(p.x - b.x) - (b.hw + round_r)) / fade;
        if (outside < 0.0f){ outside = 0.0f; }
        if (outside > 1.0f){ outside = 1.0f; }

        //Full noise if EITHER well below the top or well clear of the block sideways.
        float a = Smootherstep(below);
        float b_out = Smootherstep(outside);
        if (b_out > a){
            a = b_out;
        }
        if (a < atten){
            atten = a;
        }
    }
    return atten;
}

//The whole field at a point: the smooth union of the bay's blocks, displaced by attenuated noise.
static float Field(const vec3& p, const std::vector<const StageBlock*>& blocks,
                   const TerrainParams& params){
    float d = 1e30f;
    for (size_t i = 0;i < blocks.size();i++){
        float db = SdBlock(p,*blocks[i],params.depth,params.round_r);
        d = (i == 0) ? db : SmoothMin(d,db,params.smooth_k);
    }
    if (params.noise_amp > 1e-6f){
        float atten = NoiseAttenuation(p,blocks,params.noise_fade,params.round_r);
        if (atten > 0.0f){
            vec3 np(p.x * params.noise_freq,p.y * params.noise_freq,p.z * params.noise_freq);
            d += ValueNoise(np) * params.noise_amp * atten;
        }
    }
    return d;
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

            That asymmetry is exactly the asymmetry SdBlock builds in: the rounding is pushed
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
static void MeasureTops(const std::vector<const StageBlock*>& blocks, const TerrainParams& params,
                        TerrainStats& stats){
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
    y_start += params.round_r + params.noise_amp + params.smooth_k + 0.5f;

    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = *blocks[i];
        for (int s = 0;s < samples_across;s++){
            //Inset a little from the corners: the outermost sliver of a top face is where the
            //rounding of a NEIGHBOURING block legitimately reaches, and measuring it would report
            //that neighbour's fillet as this block's error.
            float t = (samples_across == 1) ? 0.5f : ((float)s / (float)(samples_across - 1));
            float x = b.Left() + (b.Right() - b.Left()) * (0.08f + 0.84f * t);
            vec3 probe(x,b.Top() + 0.01f,0.0f);
            if (IsBuried(probe,blocks,&b,params.round_r)){
                continue;   //something is standing here; this is not an exposed top face
            }

            float y_lo = b.Top() - probe_down;
            int   steps = (int)((y_start - y_lo) / probe_step) + 1;
            float prev_y = y_start;
            float prev_d = Field(vec3(x,y_start,0.0f),blocks,params);
            bool  f_found = false;
            float surface_y = y_start;
            for (int k = 1;k <= steps;k++){
                float y = y_start - probe_step * (float)k;
                if (y < y_lo){ y = y_lo; }
                float d = Field(vec3(x,y,0.0f),blocks,params);
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

bool BuildTerrainVerts(const std::vector<StageBlock>& blocks, float x_min, float x_max,
                       const TerrainParams& params, std::vector<vertex>& out,
                       TerrainStats* stats){
    //Only BLOCK_SOLID melts - see the header. Selected by CENTRE so that a block belongs to
    //exactly one range and nothing is meshed twice into two overlapping surfaces.
    std::vector<const StageBlock*>mine;
    for (size_t i = 0;i < blocks.size();i++){
        const StageBlock& b = blocks[i];
        if (b.kind != BLOCK_SOLID || !b.f_alive){
            continue;
        }
        if (b.x < x_min || b.x >= x_max){
            continue;
        }
        mine.push_back(&b);
    }
    if (mine.empty()){
        debug->Err("BuildTerrainVerts: no solid blocks with a centre in [%.2f,%.2f)\n",x_min,x_max);
        return false;
    }

    /*
        The sampled box has to hold everything the field can reach, not just the blocks.

        The rounding pushes the surface out by round_r, the noise by noise_amp, and marching cubes
        needs a ring of samples OUTSIDE the surface to close it off - without that the terrain is
        cut flat wherever it meets the edge of the grid and the slab is left open at the front and
        back. Two cells of margin on top of the two displacements is what makes the surface close
        itself rather than needing capping logic.
    */
    float margin = params.round_r + params.noise_amp + 2.0f * params.cell_xy;
    float lo_x = mine[0]->Left(), hi_x = mine[0]->Right();
    float lo_y = mine[0]->Bottom(), hi_y = mine[0]->Top();
    for (size_t i = 1;i < mine.size();i++){
        if (mine[i]->Left()   < lo_x){ lo_x = mine[i]->Left(); }
        if (mine[i]->Right()  > hi_x){ hi_x = mine[i]->Right(); }
        if (mine[i]->Bottom() < lo_y){ lo_y = mine[i]->Bottom(); }
        if (mine[i]->Top()    > hi_y){ hi_y = mine[i]->Top(); }
    }
    lo_x -= margin; hi_x += margin;
    lo_y -= margin; hi_y += margin;
    float lo_z = -(params.depth + margin), hi_z = params.depth + margin;

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
                density[grid.Index(x,y,z)] = Field(grid.Position(x,y,z),mine,params);
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
        gradient. At this cell size that reads as a faceted style and sits well beside the app's
        flat palette; a soft blend would need a custom shader and one of the free interpolating
        channels, and is deliberately not attempted here.
    */
    for (size_t i = first;i < out.size();i++){
        vertex& v = out[i];
        if (v.normal.y < params.rock_ny){
            v.matid = 2;        //a cliff face
            continue;
        }
        v.matid = 1;            //soil, unless it turns out to be near a top
        if (v.normal.y > params.grass_ny){
            for (size_t k = 0;k < mine.size();k++){
                const StageBlock& b = *mine[k];
                if (fabsf(v.pos.x - b.x) > b.hw + params.round_r){
                    continue;
                }
                float below = b.Top() - v.pos.y;
                if (below >= -params.grass_depth && below <= params.grass_depth){
                    v.matid = 0;
                    break;
                }
            }
        }
    }

    if (stats){
        stats->num_blocks    = (int)mine.size();
        stats->num_triangles = (int)((out.size() - first) / 3);
        stats->num_samples   = grid.SampleCount();
        MeasureTops(mine,params,*stats);
    }
    return true;
}

Mesh* BuildTerrainMesh(const std::vector<StageBlock>& blocks, float x_min, float x_max,
                       const TerrainParams& params, TerrainStats* stats){
    std::vector<vertex>verts;
    if (!BuildTerrainVerts(blocks,x_min,x_max,params,verts,stats)){
        return NULL;
    }
    if (verts.empty()){
        debug->Err("BuildTerrainMesh: [%.2f,%.2f) produced no surface\n",x_min,x_max);
        return NULL;
    }
    Mesh* mesh = new Mesh();
    mesh->SetMeshData(verts.data(),(int)verts.size());
    return mesh;
}

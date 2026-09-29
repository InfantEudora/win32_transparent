#include "Terrain.h"
#include "MarchingCubes.h"
#include "Debug.h"

#include <math.h>

static Debugger* debug = new Debugger("Terrain",DEBUG_INFO);

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
            float prev_d = TerrainFieldAt(vec3(x,y_start,0.0f),blocks,floating,params);
            bool  f_found = false;
            float surface_y = y_start;
            for (int k = 1;k <= steps;k++){
                float y = y_start - probe_step * (float)k;
                if (y < y_lo){ y = y_lo; }
                float d = TerrainFieldAt(vec3(x,y,0.0f),blocks,floating,params);
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

    std::vector<bool> floating = TerrainFindFloating(mine,blocks);

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
                density[grid.Index(x,y,z)] = TerrainFieldAt(grid.Position(x,y,z),mine,floating,params);
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
        TerrainFieldAt(v.pos,mine,floating,params,&cap,&body);
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

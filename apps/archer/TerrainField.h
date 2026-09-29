#ifndef _ARCHER_TERRAIN_FIELD_H_
#define _ARCHER_TERRAIN_FIELD_H_

#include <vector>

#include "Stage.h"
#include "type_vec3.h"

/*
    The terrain's FIELD - the signed distance to the drawn ground - on its own, without the mesher.

    Terrain.cpp turns this into triangles (core/MarchingCubes), and that half needs the engine. This
    half is pure maths on the blocks, so anything else that wants to know where the DRAWN surface
    is - rather than the collider under it - can ask: a vine growing against the rock, a prop set
    down on the grass. Engine-free, so `make rules` links it (vine_plan.md section 9, step 6).
    Moved here unchanged from Terrain.cpp; see Terrain.h for the shape it describes.
*/

/*
    Everything that makes one bay look different from the next.

    All of it is tuning and all of it is meant to be argued with. The reason this is a struct
    rather than a block of #defines is the test bay: four variants have to exist AT THE SAME TIME
    for them to be comparable in one screenshot, so the field function has to be pure in its
    parameters rather than reading constants.
*/
struct TerrainParams{
    //--- the grid -------------------------------------------------------------------------------
    //Spacing in the play plane, and through the slab. cell_z used to be twice as coarse, when a
    //block was an extrusion and the field did not vary in z; now the front lip, the drips and the
    //coarse noise all live in z, and at 0.5 there were only eight samples through a slab.
    float cell_xy      = 0.25f;
    float cell_z       = 0.25f;

    //--- the body -------------------------------------------------------------------------------
    //Corner rounding on each block's body. Clamped per block to 0.9 * hh so the top stays exact
    //even on a block thinner than 2r - see the header note.
    float round_r      = 0.20f;
    //Smooth-union radius between blocks. This is the one that decides whether the level reads as
    //welded boxes or as ground, and the one that dissolves thin features if it is too big.
    float smooth_k     = 0.35f;
    //How far the body's front and back stand in from the block's depth, so the grass cap
    //overhangs them - the lip the authored tiles have. The body's rounding grows it back out by
    //round_r, so at 0.2 the earth face is exactly at the block's depth.
    float body_inset_z = 0.20f;

    //--- the grass cap --------------------------------------------------------------------------
    //A slab over each block's top, the same top: see "THE SHAPE, THROUGH THE SLAB" above.
    float cap_thickness = 0.28f;    //at its thinnest, between drips
    float cap_round    = 0.12f;
    float cap_lip_x    = 0.10f;     //overhang past the collider at the sides, before the rounding
    float cap_lip_z    = 0.18f;     //and at the front and back, where the camera sees it
    float drip_amp     = 0.35f;     //how much further a drip hangs below the cap's thinnest
    float drip_freq    = 1.10f;     //drips per world unit, near enough

    //--- the root -------------------------------------------------------------------------------
    //A floating block - nothing sits under it, but it is over something - gets a rounded belly
    //below its collider, like the tiles' tapering undersides. Visual only, and below the
    //collider's bottom, so it can never be stood on or bumped into.
    float root_scale   = 0.50f;     //depth of the belly as a fraction of the block's half-width
    float root_max     = 1.20f;
    float root_k       = 0.35f;     //how softly it blends into the body

    //--- the noise ------------------------------------------------------------------------------
    //Two octaves. The fine one is the old one; the coarse one is what makes a face read as rock
    //rather than as a rounded box, and it is kept off the middle of the slab - see coarse_z0.
    float noise_amp    = 0.15f;     //world units of displacement
    float noise_freq   = 0.60f;     //cycles per world unit
    float coarse_amp   = 0.35f;
    float coarse_freq  = 0.30f;
    /*
        The coarse octave fades in between these distances from the walk line (z 0). A side face
        she walks into is at z 0; a coarse bump there would show her standing in the rock or a
        gap between her and it, both at 0.3 of a unit. The front and back of the slab, which the
        camera looks at, take it all.
    */
    float coarse_z0    = 0.40f;
    float coarse_z1    = 1.10f;
    //How far below a block's top the noise reaches full strength. Zero AT the top, because noise
    //is the one term that can push a surface down through a collider, and a landing surface is
    //exactly where that must not happen.
    float noise_fade   = 0.60f;

    //--- the materials --------------------------------------------------------------------------
    //Slot 0 is grass, 1 is soil, 2 is rock - matching the order ApplicationArcher::BuildMaterials
    //assigns them in. Grass is WHATEVER THE CAP OWNS: a vertex nearer a cap than any body. The
    //rest is earth, told apart by slope.
    float cap_eps      = 0.03f;     //a tie on the top face, where both are at zero, goes to grass
    float rock_ny      = 0.25f;     //normal.y below this is a cliff face
};


/*
    Which blocks one piece of terrain is built from: every live BLOCK_SOLID whose CENTRE is in
    [x_min,x_max) x [y_min,y_max).

    By position rather than by a flag on StageBlock, which is what keeps Stage.h free of this whole
    subject - and what makes dragging a box from one region into another in the editor the whole
    of moving it between bays. Half-open, so regions that share an edge never share a block and
    nothing is meshed twice into two overlapping surfaces.
*/
struct TerrainRegion{
    float x_min = 0.0f, x_max = 0.0f;
    float y_min = 0.0f, y_max = 0.0f;

    bool Contains(const StageBlock& b) const{
        //Not an invisible one: that block already has a look, the model it sits under.
        return b.kind == BLOCK_SOLID && b.f_alive && !b.f_invisible &&
               b.x >= x_min && b.x < x_max && b.y >= y_min && b.y < y_max;
    }
};


/*
    The field at p, for `blocks` (the region's own) with `floating` from TerrainFindFloating.
    Negative inside. The nearest cap's and body's own distances too, for the material pass.
*/
float TerrainFieldAt(const vec3& p, const std::vector<const StageBlock*>& blocks,
                     const std::vector<bool>& floating, const TerrainParams& params,
                     float* out_cap = NULL, float* out_body = NULL);

//Which of `blocks` float: nothing under them, but something anywhere in `all` below them.
std::vector<bool> TerrainFindFloating(const std::vector<const StageBlock*>& blocks,
                                      const std::vector<StageBlock>& all);

/*
    One region's drawn surface, ready to sample: the blocks it melts (copied, so the Stage's vector
    can change under it), which of them float, and the params it is meshed with. Build it from the
    same region and params the mesh was built from and Distance is that mesh's field, exactly.

    Not copyable: it points into its own copy of the blocks.
*/
class TerrainSurface{
public:
    TerrainSurface(){}
    TerrainSurface(const TerrainSurface&) = delete;
    TerrainSurface& operator=(const TerrainSurface&) = delete;

    void  Build(const std::vector<StageBlock>& all, const TerrainRegion& region, const TerrainParams& params);
    bool  IsEmpty() const { return own.empty(); }
    //The signed distance to the drawn surface at p, and its outward normal there (numerically).
    float Distance(const vec3& p) const;
    vec3  Normal(const vec3& p) const;
    const TerrainRegion& Region() const { return region; }

private:
    std::vector<StageBlock>        own;
    std::vector<const StageBlock*> blocks;
    std::vector<bool>              floating;
    TerrainParams params;
    TerrainRegion region;
};

#endif

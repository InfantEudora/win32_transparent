#ifndef _ARCHER_TERRAIN_H_
#define _ARCHER_TERRAIN_H_

#include "Mesh.h"
#include "type_vertex.h"
#include "Stage.h"

#include <vector>

/*
    The archer's level, as terrain rather than as boxes.

    This is the half of the job that knows what a StageBlock is; core/MarchingCubes.h is the half
    that knows how to turn a field into triangles and nothing else. See apps/archer/terrain_plan.md
    for the whole argument - what follows is only what a reader of the code needs.

    --- THE BLOCKOUT IS STILL THE COLLISION ------------------------------------------------------
    Nothing in here produces a collider, and nothing in here is consulted by Stage. The archer is
    still swept against the same axis-aligned StageBlocks by Stage::MoveAndCollide, exactly as
    before; this file builds something to LOOK at, out of the very same boxes. That is what makes
    the arrangement safe: the visual is a pure function of the collision, so the two cannot drift
    apart the way a hand-sculpted mesh and a hand-placed collider always eventually do.

    --- ONLY BLOCK_SOLID MELTS -------------------------------------------------------------------
    LEDGE, PLATFORM and BREAKABLE keep their own colour-coded boxes. ApplicationArcher::BuildMaterials
    states the rule this protects - colour means a rule - and a level where you cannot see at a
    glance which surface you can grab and which you can drop through has lost something a prototype
    needs more than it needs to look good. A BREAKABLE staying a box also means the kick slice
    never has to remesh anything mid-game.

    --- WHAT "THE TOP IS PINNED" MEANS, EXACTLY --------------------------------------------------
    A block becomes a rounded box whose ROUNDING LIVES OUTSIDE ITS OWN FOOTPRINT: half extents
    (hw, hh - r, depth) with r added back by the SDF, so the flat top lands at exactly Top() across
    the whole width of the collider and the curve rolls over beyond it.

    That ordering is the entire trick, and the obvious spelling gets it backwards. The textbook
    rounded box - (hw - r, hh - r, depth - r) then -r - keeps the footprint and rounds INWARD, so
    the last r of every ledge sags below the collider and the archer stands in the air exactly
    where a jump is tightest. Rounding outward instead costs r of terrain overhanging the edge,
    which reads as turf hanging over a lip and is the error you want to have.

    Smooth union only ever ADDS material (smin <= min), so it cannot pull a top face down either.
    It does push one UP in an inside corner, and the archer's feet then sink a little into the
    fillet at the foot of a wall - which looks like grass and is why the measurement below reports
    a rise and a dip as two different numbers rather than one absolute error.
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
    //Spacing in the play plane, and through the slab. cell_z is deliberately coarser: the field
    //barely varies in z, so a finer z spends vertices tessellating the flat front and back faces
    //of a slab the camera is looking straight at.
    float cell_xy      = 0.25f;
    float cell_z       = 0.50f;
    //Half the slab's thickness. Matches BLOCK_DEPTH/2 in ApplicationArcher.h; the terrain has to
    //be as deep as the boxes it replaces or the level gets visibly thinner where it melts.
    float depth        = 1.50f;

    //--- the shape ------------------------------------------------------------------------------
    //Corner rounding on each block. Clamped per block to 0.9 * hh so the top stays exact even on
    //a block thinner than 2r - see the header note.
    float round_r      = 0.20f;
    //Smooth-union radius between blocks. This is the one that decides whether the level reads as
    //welded boxes or as ground, and the one that dissolves thin features if it is too big.
    float smooth_k     = 0.35f;

    //--- the noise ------------------------------------------------------------------------------
    float noise_amp    = 0.15f;     //world units of displacement
    float noise_freq   = 0.60f;     //cycles per world unit
    //How far below a block's top the noise reaches full strength. Zero AT the top, because noise
    //is the one term that can push a surface down through a collider, and a landing surface is
    //exactly where that must not happen.
    float noise_fade   = 0.60f;

    //--- the materials --------------------------------------------------------------------------
    //Slot 0 is grass, 1 is soil, 2 is rock - matching the order ApplicationArcher::BuildMaterials
    //assigns them in. Four slots is all an Object has (NUM_MATERIAL_SLOTS), which is exactly
    //enough and is why this is a classification rather than a texture.
    float grass_ny     = 0.70f;     //normal.y above this is a top surface...
    float grass_depth  = 0.40f;     //...and within this of a block top, it is grass
    float rock_ny      = 0.25f;     //normal.y below this is a cliff face
};

/*
    What the build measured, so that a caller can check it rather than look at it.

    worst_dip IS THE ONE THAT MATTERS. It is how far the surface sank BELOW a collider's exposed
    top face, which is the archer standing in mid-air, and it should be zero. worst_rise is the
    fillet in an inside corner pushing the ground up, which is the archer's boots in the grass and
    is fine at any sane value.

    Two numbers rather than one absolute error, because they are two different phenomena with two
    different causes and only one of them is a bug.
*/
struct TerrainStats{
    int    num_blocks = 0;
    int    num_triangles = 0;
    size_t num_samples = 0;
    float  worst_dip = 0.0f;        //surface below a top face; must stay at zero
    float  worst_rise = 0.0f;       //surface above one; cosmetic
    int    num_probes = 0;          //exposed top-face points actually measured
};

/*
    Builds the surface for every BLOCK_SOLID block whose CENTRE falls in [x_min,x_max).

    Selection is by x range rather than by a flag on StageBlock, which is what keeps Stage.h free
    of this whole subject. A block spanning several ranges would be built into each of them, so
    the level is laid out to avoid that - see the per-bay floor note in Stage::BuildLevel.

    matid is written per vertex: 0 grass, 1 soil, 2 rock. `out` is appended to, not cleared.
    Returns false and logs if the range contains no solid blocks at all.
*/
bool BuildTerrainVerts(const std::vector<StageBlock>& blocks, float x_min, float x_max,
                       const TerrainParams& params, std::vector<vertex>& out,
                       TerrainStats* stats = NULL);

/*
    The same, wrapped in a Mesh the caller owns. RENDER THREAD ONLY - it ends in
    Mesh::SetMeshData, which talks to GL immediately. Returns NULL and logs on failure.
*/
Mesh* BuildTerrainMesh(const std::vector<StageBlock>& blocks, float x_min, float x_max,
                       const TerrainParams& params, TerrainStats* stats = NULL);

#endif

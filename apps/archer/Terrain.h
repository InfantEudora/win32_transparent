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

    --- THE SHAPE, THROUGH THE SLAB ---------------------------------------------------------------
    A block used to be one rounded box extruded to one depth for the whole level. Now each block is
    THREE PIECES, at its own depth and z (StageBlock::z, HalfDepth()):
      - the BODY: the rounded box above, its front and back set in by body_inset_z;
      - the CAP: a slab of grass over the top, THE SAME TOP - pinned the same way - that
        overhangs the body by cap_lip_z at the front and back and cap_lip_x at the sides, and
        hangs down in drips where a low-frequency noise says so. Only its underside moves, so the
        drips cannot reach the top;
      - the ROOT, for a floating block only: a rounded belly under the collider's bottom.
    A block's own cap and body are joined with a HARD min, because a smooth union of two surfaces
    that coincide on the top face would lift the whole top by k/4. Blocks are then smooth-unioned
    with each other exactly as before, so the fillet at the foot of a wall is still there.

    The top is exact where she and the plants are: across the collider in x, and within the
    block's depth in z. Beyond the depth, in the lip, the noise is let back in, so the grass edge
    wobbles rather than ruling a straight line along the front.
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
    Builds the surface for every block `region` contains.

    matid is written per vertex: 0 grass, 1 soil, 2 rock. `out` is appended to, not cleared.
    Returns false if the region contains no blocks at all - quietly, because after an edit that
    is a legitimate state rather than a failure.
*/
bool BuildTerrainVerts(const std::vector<StageBlock>& blocks, const TerrainRegion& region,
                       const TerrainParams& params, std::vector<vertex>& out,
                       TerrainStats* stats = NULL);

#endif

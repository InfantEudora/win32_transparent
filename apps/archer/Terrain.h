#ifndef _ARCHER_TERRAIN_H_
#define _ARCHER_TERRAIN_H_

#include "Mesh.h"
#include "type_vertex.h"
#include "Stage.h"
#include "TerrainField.h"

#include <vector>

/*
    The archer's level, as terrain rather than as boxes.

    This is the half of the job that knows what a StageBlock is; core/MarchingCubes.h is the half
    that knows how to turn a field into triangles and nothing else. See apps/archer/docs/terrain_plan.md
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
    //The same two along the ramps' lines (docs/terrain_plan.md section 12): a dip is her sliding
    //in the air, a rise her feet in the grass - at a ramp's foot, where the union fills the corner.
    float  ramp_worst_dip = 0.0f;
    float  ramp_worst_rise = 0.0f;
    int    num_ramp_probes = 0;
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
//And with the level's ramps: those `region` contains melt in as wedges (docs/terrain_plan.md
//section 12). The one above is this with none.
bool BuildTerrainVerts(const std::vector<StageBlock>& blocks, const std::vector<StageRamp>& ramps,
                       const TerrainRegion& region, const TerrainParams& params,
                       std::vector<vertex>& out, TerrainStats* stats = NULL);

#endif

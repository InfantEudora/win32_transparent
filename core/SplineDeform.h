#ifndef _SPLINE_DEFORM_H_
#define _SPLINE_DEFORM_H_

#include <vector>
#include "Spline.h"
#include "type_vertex.h"

/*
    Tiles an authored mesh along a Spline, bending each copy into the curve - Blender's Array plus
    Curve modifier, done in the engine. A vine's trunk and a rope's twisted middle are both this.

    THE TILE CONVENTION. The tile runs along its own +Z (glTF +Z is Blender -Y, the forward every
    prop in this repo is authored to), with its cross-section around the Z axis: local X maps to the
    frame's `side`, local Y to its `normal`, Z to distance along the curve. The tile's length is
    measured off its vertices (z max - z min), never typed in, and the copies are stretched a little
    so a whole number of them fits the range exactly. For the seams to close, the vertices on the
    tile's z-min face must sit where the z-max face's do - that is an authoring rule, and nothing
    here can repair a tile that breaks it.

    The input is a TRIANGLE LIST, as Mesh::GetVertices hands it out; so is the output, in world
    space, ready for Mesh::SetMeshData. matid and uv are copied from the tile, so a tile's materials
    and texture carry over unchanged.
*/

struct SplineDeformParams{
    //Scales the tile before anything else - the same number an Object would be scaled by.
    float scale = 1.0f;
    //The distance range along the curve to fill. end < 0 means the curve's full length.
    float start = 0.0f;
    float end   = -1.0f;
    //A turn of the tile about the curve (radians), all along it, and a twist added per unit of
    //distance - the second breaks up a tile that would otherwise visibly repeat.
    float roll  = 0.0f;
    float twist = 0.0f;
    /*
        Taper: the cross-section is scaled toward `*_scale` over the last `*_length` of each end,
        on a smoothstep so the thinning has no crease where it begins. A length of 0 is no taper.
    */
    float taper_start_length = 0.0f;
    float taper_start_scale  = 1.0f;
    float taper_end_length   = 0.0f;
    float taper_end_scale    = 1.0f;
    /*
        Recompute each triangle's normal from its deformed corners instead of carrying the tile's
        normals round the bend. For faceted, flat-shaded art this is the exact answer. Leave it off
        for a smooth-shaded tile: it throws the authored normals away and facets the surface.

        Off, the tile's own normals are carried - not merely rotated, but corrected for what the
        deform does to the surface under them: the stretch-to-fit (a non-uniform scale), the taper
        (a cone tilts its normals toward the thin end) and the twist (a shear). What is NOT
        corrected is the bend's own squash on the inside of a curve, which is small at a trunk's
        radius and which Blender's Curve modifier leaves out too.
    */
    bool  f_flat_normals = false;
    /*
        Smooth tiles: average the normals where one copy's z-max ring meets the next copy's z-min
        ring. A tile smoothed in Blender on its own has end-ring normals that lean toward its own
        middle - measured on archer.glb's vine_trunk, the two ends disagree by 11 degrees median,
        16 at worst - so every join would show as a soft crease. Blender hides it only when an
        Array modifier merges the copies first. Vertices are paired by position on the tile's two
        end rings; a pair more than 60 degrees apart is taken as an authored hard edge and left.
        Nothing is done at the curve's two ends, or where the rings do not line up (an overlay).
    */
    bool  f_weld_seams = false;
    /*
        The period, when it is not the tile's own extent. `tile_length` > 0 replaces the measured
        z max - z min, and `tile_start` replaces z min as the tile's local zero.

        For an OVERLAY - a wrap wound round a trunk, authored against the trunk tile - which must
        be laid in step with the trunk, copy for copy: its strands cross the tile boundary on a
        slant, so its bounds overhang the period at both ends, and measuring it would space the
        copies wider than the trunk's and open a gap at every join. Pass the trunk's own numbers
        (SplineDeformMeasure) and both lay down the same copies at the same distances. Both in the
        tile's own units, before `scale`.
    */
    float tile_start  = 0.0f;
    float tile_length = 0.0f;
    /*
        GROWTH: only the sweep up to `grown` (a distance along the curve, like start and end) is
        laid down. < 0, or at or past the range's end, is all of it. See apps/archer/vine_plan.md
        section 9.

        Not a moving `end`, which would re-stretch every copy to fit and change the count as it
        went - the whole trunk sliding and popping as it grows. The copies are laid out for the
        FULL range, exactly as the finished sweep has them: a copy wholly past the front is left
        out, and a vertex of the copy the front is inside is pulled back onto the front. Over the
        last grow_tip_length behind the front the section closes to grow_tip_scale, so the front
        is a closed point rather than a sawn ring - shaped as a cone at the point, meeting the trunk
        with no crease. That closing fades out as the front nears the range's end, so a finished
        sweep arrives at the ordinary end taper rather than jumping onto it.

        A copy the front has just reached is born collapsed onto the point, so nothing appears out
        of nowhere either, and everything more than a tip's length behind the front is the finished
        sweep's own geometry, bit for bit.
    */
    float grown           = -1.0f;
    float grow_tip_length = 0.35f;
    float grow_tip_scale  = 0.0f;
};

//A tile's extent along its Z: where it starts and how long it is, in its own units. False for a
//tile with no length.
bool SplineDeformMeasure(const std::vector<vertex>& tile, float& zmin, float& length);

//The cross-section scale at distance s under these params (1 away from the ends), the growing
//tip's closing included.
float SplineDeformTaper(const SplineDeformParams& params, float s, float range_start, float range_end);

/*
    Appends the deformed copies to `out` (it is NOT cleared, so several sweeps can share one mesh)
    and returns how many copies were laid down - 0 for an empty tile, a flat one, a curve that was
    not built, or a sweep not grown yet. Growing, it is the copies laid SO FAR, each whole, the
    last one partly collapsed onto the front.
*/
int DeformAlongSpline(const Spline& spline, const std::vector<vertex>& tile,
                      const SplineDeformParams& params, std::vector<vertex>& out);

#endif

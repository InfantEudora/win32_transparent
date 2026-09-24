#ifndef _ARCHER_VINE_H_
#define _ARCHER_VINE_H_

#include <vector>

#include "Spline.h"
#include "SplineDeform.h"
#include "type_quat.h"
#include "type_vertex.h"
#include "Stage.h"

/*
    The vines - a trunk bent along a curve, with leaves along it. See apps/archer/vine_plan.md;
    this is steps 1-2 of it, the static decorative kind.

    Pure geometry: a path in, vertices and leaf placements out. No engine type beyond core's maths
    and the two spline files, so `make rules` builds this against stage_test.cpp and checks it
    without a GPU, the same as Foliage. ApplicationArcher turns the output into Objects.

    --- THE PIECES --------------------------------------------------------------------------------
      * THE TRUNK is a tile deformed along the path (core/SplineDeform.h has the convention: along
        +Z, cross-section around the axis, matching end rings). The app takes it from archer.glb's
        `vine_trunk` node if there is one, and from MakeVinePlaceholderTile if not, so the whole
        path works before the asset exists.
      * THE LEAVES are placed at stations along the trunk, in clusters, turned round it by the
        golden angle so no two neighbours line up - the phyllotaxis real stems use, and the reason
        a vine's leaves never look arranged. Leaf convention: origin at the stem, blade along +Z,
        upper face +Y - the same "origin at the attach point" as every other prop here.

    DETERMINISTIC, by the same hash-of-where the ferns use (PlaceHash.h): a vine always grows the
    same leaves, and nothing here draws from the engine's shared RRandom stream.
*/

enum VineLeafKind{
    VINE_LEAF_1 = 0,
    VINE_LEAF_2,
    VINE_LEAF_KIND_COUNT
};

//One vine: the curve its trunk follows, and what makes it this vine rather than another.
struct VinePath{
    std::vector<vec3> points;       //the trunk's centre line, world space
    //Which way the trunk's top (tile +Y) faces at the first point. The frame is carried from
    //there without twisting, so a vine that starts on the ground and climbs a wall ends with its
    //top facing out of the wall.
    vec3  up = vec3(0.0f,1.0f,0.0f);
    int   seed = 0;                 //which leaves it grows; two vines on one path differ by this
    float thickness = 1.0f;         //on the cross-section, on top of VineParams::tile_scale
};

struct VineParams{
    //Tile to world. The app sets the character's scale for a tile out of archer.glb (the file is
    //authored to scale with her) and 1 for the placeholder, which is built in world units.
    float tile_scale     = 1.0f;
    //The tile's largest distance from its axis, at scale 1 - VineTileRadius measures it. Leaves
    //are seated on the surface by this, so it is measured, never typed in.
    float tile_radius    = 0.10f;
    //Radians per unit length. Breaks up a tile that would otherwise visibly repeat.
    float twist          = 0.9f;
    //Both ends thin over this distance to tip_scale, so a vine does not end in a sawn-off stump.
    float taper_length   = 0.9f;
    float tip_scale      = 0.30f;

    //--- Leaves -----------------------------------------------------------------------------------
    float leaf_spacing        = 0.42f;  //mean distance between clusters along the trunk
    float leaf_spacing_jitter = 0.45f;  //fraction of the spacing, either way
    int   cluster_min         = 1;
    int   cluster_max         = 3;
    float cluster_fan         = 0.55f;  //radians between neighbours in a cluster, round the trunk
    float leaf_scale          = 1.0f;
    float leaf_scale_jitter   = 0.25f;
    //How far a leaf rises from lying along the trunk toward standing straight out, radians. The
    //reference model's leaves stand well off it.
    float leaf_lift           = 0.85f;
    float leaf_lift_jitter    = 0.30f;
    //Where the stem sits, as a fraction of the trunk's radius - a little inside it, so no stem
    //is seen floating off the bark at a thin place.
    float leaf_seat           = 0.75f;
    //No leaf nearer an end than this: the tips taper to a point and a leaf there floats.
    float leaf_end_margin     = 0.25f;
    //Leaves shrink toward the ends by this much, along with the trunk - young growth.
    float leaf_tip_shrink     = 0.45f;
    //Stem to tip of the leaf mesh at scale 1, measured by the app. Only used to keep tips out of
    //the blocks - see ScatterVineLeaves.
    float leaf_length         = 0.30f;
};

struct VineLeaf{
    int   kind = VINE_LEAF_1;
    vec3  position;
    quat  rotation = quat(0.0f,0.0f,0.0f,1.0f);
    float scale = 1.0f;             //multiplier on the leaf mesh
    float s = 0.0f;                 //distance along the trunk it grows from, for inspection
};

//The spline for a path, built. False for a path of fewer than two points.
bool  BuildVineSpline(const VinePath& path, Spline& out);

//The trunk's radius at distance s, taper included - where a leaf's stem goes.
float VineRadiusAt(const Spline& spline, const VinePath& path, const VineParams& params, float s);

//Appends the trunk to `out`, world space, a triangle list for Mesh::SetMeshData. Returns the
//number of tiles laid down, 0 if there was nothing to lay.
int   BuildVineTrunk(const Spline& spline, const VinePath& path, const std::vector<vertex>& tile,
                     const VineParams& params, std::vector<vertex>& out);

/*
    Appends an OVERLAY - archer.glb's `vine_curl`, the thin strands wound round the trunk - laid
    copy for copy with the trunk: the trunk tile's period, the same twist and taper, so each copy
    of the wrap sits on the copy of the trunk it was modelled round. See SplineDeformParams::
    tile_length for why the overlay's own extent cannot be used. Returns the copies laid.
*/
int   BuildVineOverlay(const Spline& spline, const VinePath& path, const std::vector<vertex>& overlay,
                       const std::vector<vertex>& trunk_tile, const VineParams& params,
                       std::vector<vertex>& out);

/*
    Appends the leaves along the trunk to `out`.

    `blocks` may be NULL. Given, a leaf whose tip would end up inside a live block is turned to
    the opposite side of the trunk, and dropped if that is inside one too: a vine lying on the
    ground or up a wall otherwise puts nearly half its leaves into the surface (11 of 25 on the
    step vine, measured), where they poke through a thin one or cost a draw for nothing.
*/
void  ScatterVineLeaves(const Spline& spline, const VinePath& path, const VineParams& params,
                        const std::vector<StageBlock>* blocks, std::vector<VineLeaf>& out);

//The largest distance of any vertex from the tile's Z axis.
float VineTileRadius(const std::vector<vertex>& tile);

/*
    Stand-ins until archer.glb carries the pieces. The tile is an irregular octagon a little
    flattened, 0.42 long and about 0.1 in radius, in world units; the leaf a folded diamond 0.36
    long. Both single-material (matid 0) and in the conventions above, so an authored piece drops
    in with nothing else changing.
*/
void  MakeVinePlaceholderTile(std::vector<vertex>& out);
void  MakeVinePlaceholderLeaf(std::vector<vertex>& out);

//The level's vines, placed by hand. `level` is a StageLevel; only the main level has any.
void  DeclareVines(int level, std::vector<VinePath>& out);

//The rotation taking local X, Y, Z onto the given orthonormal, right-handed axes.
quat  QuatFromBasis(const vec3& x, const vec3& y, const vec3& z);

#endif

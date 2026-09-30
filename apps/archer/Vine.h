#ifndef _ARCHER_VINE_H_
#define _ARCHER_VINE_H_

#include <vector>

#include "Spline.h"
#include "SplineDeform.h"
#include "type_quat.h"
#include "type_vertex.h"
#include "Stage.h"
#include "TerrainField.h"

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
    VINE_LEAF_KIND_COUNT,
    //Past the vine's own kinds, which are all ScatterVineLeaves deals and all the static vines
    //load: the grown plants' other leaves.
    VINE_LEAF_BAMBOO = VINE_LEAF_KIND_COUNT,
    VINE_LEAF_ALL_KINDS
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
    //Its start is buried - in the rock it grew out of, or the trunk a branch leaves - so it has
    //no taper there: full thickness where it comes out, as a rooted stem is, rather than pinched.
    bool  f_rooted = false;
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
    //While growing, the front closes to grow_tip_scale over this - SplineDeformParams'. A plant
    //that wears a tip piece on its front (the bamboo) keeps full thickness there instead.
    float grow_tip_length = 0.35f;
    float grow_tip_scale  = 0.0f;

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
//number of tiles laid down, 0 if there was nothing to lay. `grown` >= 0 lays only the trunk up to
//that distance along the curve, closed to a growing point (SplineDeformParams::grown).
int   BuildVineTrunk(const Spline& spline, const VinePath& path, const std::vector<vertex>& tile,
                     const VineParams& params, std::vector<vertex>& out, float grown = -1.0f);

/*
    Appends an OVERLAY - archer.glb's `vine_curl`, the thin strands wound round the trunk - laid
    copy for copy with the trunk: the trunk tile's period, the same twist and taper, so each copy
    of the wrap sits on the copy of the trunk it was modelled round. See SplineDeformParams::
    tile_length for why the overlay's own extent cannot be used. Returns the copies laid.
*/
int   BuildVineOverlay(const Spline& spline, const VinePath& path, const std::vector<vertex>& overlay,
                       const std::vector<vertex>& trunk_tile, const VineParams& params,
                       std::vector<vertex>& out, float grown = -1.0f);

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

/*
    --- GROWN VINES ------------------------------------------------------------------------------
    vine_plan.md sections 9-11. A grown vine's shape is WALKED once, when the arrow strikes, and
    growth only reveals it (SplineDeformParams::grown). The walk is a function of the hit, the
    species, a seed and the blocks - nothing else - so the same shot always grows the same vine,
    and a later step can move it into the rules unchanged.

    A SPECIES is how a kind of plant grows and what it wears. One table (VineSpeciesFor), in code
    until the numbers want tuning live; the vine is the first row, the rest (roots, bamboo, thorny,
    grape) arrive with their art. Distances are world units, rates per unit of length walked.
*/
enum VineSpeciesKind{
    VINE_SPECIES_VINE = 0,
    //Every plant's roots, and all a normal arrow grows under a platform: short, dark and quick,
    //wandering hard, a fork or two near the tip, no leaves (vine_plan.md section 11).
    VINE_SPECIES_ROOTS,
    //The vine as a creeper: what a vine arrow grows into a wall or a top - up the face, over the
    //lip, across and down (vine_plan.md step 8). The vine's look, the hug habit.
    VINE_SPECIES_CREEPER,
    //Bamboo: a clump of canes, fast and nearly straight up - negative gravity - from wherever it
    //is struck, bending up out of a wall. No branches; leaf sprays at the upper nodes (GrowBamboo).
    VINE_SPECIES_BAMBOO,
    VINE_SPECIES_COUNT
};

struct VineSpecies{
    const char* name = "";

    //--- The walk ---
    float length_min = 3.0f;
    float length_max = 8.0f;
    float step = 0.08f;             //one step of the walk; the path keeps a point every point_spacing
    float point_spacing = 0.3f;
    /*
        How the heading turns, per unit walked: toward straight down by `gravity` (negative: up, for
        a cane), and sideways by a smooth wander of `wander` over a wavelength - never white noise,
        which kinks. `wander_depth` is the share of the wander that goes into and out of the screen
        rather than across it, so a vine hanging in the play plane still has some roundness.
    */
    float gravity = 2.4f;
    float wander = 1.1f;
    float wander_wavelength = 1.6f;
    float wander_depth = 0.35f;
    //Kept from the blocks on top of the trunk's own radius, so a vine lying on a floor beds in
    //rather than sinking into it (negative) or hovering (large).
    float clearance = -0.02f;
    //Once it has come to rest on a floor, how much further it creeps along it before it stops.
    float rest_length = 0.8f;
    /*
        THE HUG, for a creeper (vine_plan.md step 8); 0 is a hanging plant. Within hug_reach of a
        surface (beyond its keep) the heading is drawn toward lying hug_gap off it at `hug` per
        unit, and the bias runs ALONG the surface rather than through it: up at `climb` until it
        has come over onto a top, then `gravity`, so it crosses the top and drapes down the far
        side instead of climbing back. The wander is in the surface's plane. Further from any
        surface than hug_reach it hangs, as a vine does. It never comes to rest: it creeps.
    */
    float hug = 0.0f;
    float hug_reach = 0.6f;
    float hug_gap = 0.0f;
    float climb = 0.0f;

    //--- Branches: side strands off the main one, one level deep ---
    int   branch_min = 0;
    int   branch_max = 2;
    float branch_from = 0.35f;      //where along the main strand they may leave, as fractions
    float branch_to = 0.8f;
    float branch_angle_deg = 35.0f; //how far off the main strand's heading they leave
    float branch_length = 0.4f;     //of the main strand's length

    //--- The look ---
    float thickness = 1.0f;         //VinePath::thickness, and the radius the walk keeps off the blocks
    float thickness_jitter = 0.15f;
    float branch_thickness = 0.6f;  //a branch's, relative to the main strand's

    //--- The growth: how it is revealed (vine_plan.md section 9) ---
    //Ticks from the strike to full length. The front eases out - fast from the arrow, slowing to
    //a stop - because a constant rate reads as a progress bar.
    int   grow_ticks = 150;
    //A leaf appears once the front is this far past it, and opens over the next leaf_unfold: from
    //nothing, lying along the stem, to its full size and lift. The delay is past the growing tip's
    //taper (SplineDeformParams::grow_tip_length), so a leaf is never seated on a trunk that is
    //still thinner than it will be.
    float leaf_delay = 0.45f;
    float leaf_unfold = 0.6f;
};

//Where the growing front of a strand is, `ticks` after the strike, as a distance along a curve
//of `length`: eased out, and exactly `length` from grow_ticks on.
float VineGrowthFront(const VineSpecies& species, float length, int ticks);
/*
    How far open a leaf at distance `s` is, 0 not yet there .. 1 fully open, with the front at
    `front`. Eased out, like the front.
*/
float VineLeafOpen(const VineSpecies& species, float s, float front);

const VineSpecies& VineSpeciesFor(int kind);

//One strand of a grown vine: its path, and where it leaves its parent (-1: from the anchor).
struct VineStrand{
    VinePath path;
    int   parent = -1;
    float s_on_parent = 0.0f;
    float length = 0.0f;            //as walked
    bool  f_rested = false;         //came to rest on a floor rather than reaching its length
};

struct VineGrowth{
    std::vector<VineStrand> strands;    //[0] the main one, from the anchor; branches after it
};

//The seed for a growth from where it struck and which arrow slot made it - distinct for two
//arrows into one spot, the same for the same shot replayed.
int   VineGrowthSeed(const vec3& point, int arrow);

/*
    WHAT A VINE GROWS AGAINST: a signed distance, negative inside, anywhere. Two of them:

      VineBlockField   the blocks' boxes, in the play plane - what the walk used first, and still
                       right wherever the box IS the look (the blockout, the ledges, the platforms).
      VineLevelField   the level as drawn: every box, except the blocks a terrain surface has
                       melted, which are that surface instead - the rounded lips, the drips, the
                       bellies under floating stones (TerrainField.h). vine_plan.md step 6.
*/
class VineField{
public:
    virtual ~VineField(){}
    virtual float Distance(const vec3& p) const = 0;
    //The outward direction at p, by central differences; zero where the field is flat.
    vec3 Normal(const vec3& p) const;
};

class VineBlockField : public VineField{
public:
    explicit VineBlockField(const std::vector<StageBlock>& b) : blocks(b){}
    float Distance(const vec3& p) const override;
private:
    const std::vector<StageBlock>& blocks;
};

class VineLevelField : public VineField{
public:
    //`surfaces` must outlive the field; the blocks are copied.
    VineLevelField(const std::vector<StageBlock>& blocks, const std::vector<const TerrainSurface*>& surfaces);
    float Distance(const vec3& p) const override;
private:
    std::vector<StageBlock> boxes;      //the live blocks no surface melts
    std::vector<const TerrainSurface*> surfaces;
};

/*
    Walks a vine out of `anchor` on a surface whose outward normal is `normal` (an underside is
    (0,-1,0)), starting along the normal and bending with the species. It slides along any live
    block rather than entering it - the trunk's radius (from `params` and the species' thickness)
    plus the species' clearance kept off every face - and comes to rest on a floor it reaches.
    False if it could not walk at all.
*/
bool  GrowVine(const VineSpecies& species, const VineParams& params, const vec3& anchor,
               const vec3& normal, int seed, const std::vector<StageBlock>& blocks, VineGrowth& out);
/*
    The same against any field. The start is first marched out along the normal to where the
    field is open - so a vine struck into a box the terrain has drawn a belly under begins on the
    belly, not up inside it - and its path starts a trunk's radius back in, so it comes out of the
    rock there. Against the plain blocks nothing needs marching, and this is the version above.
*/
bool  GrowVine(const VineSpecies& species, const VineParams& params, const vec3& anchor,
               const vec3& normal, int seed, const VineField& field, VineGrowth& out);
/*
    A spray of roots out of one strike: GrowVine with the roots species, two to four times, each
    from a little way along the surface and leaning a little away from the rest, appended to `out`
    (every strand's parent index kept pointing into `out`). The number, the spread and every root
    are hashed on the seed. False if none could grow.
*/
bool  GrowRoots(const VineSpecies& species, const VineParams& params, const vec3& anchor,
                const vec3& normal, int seed, const VineField& field, VineGrowth& out);
/*
    A clump of bamboo out of one strike: GrowVine with the bamboo species, three to five canes
    spread across and into the surface, the middle ones tallest, each leaning a little off the
    normal. Out of a wall they come out and bend up. Appended to `out`; false if none could grow.
*/
bool  GrowBamboo(const VineSpecies& species, const VineParams& params, const vec3& anchor,
                 const vec3& normal, int seed, const VineField& field, VineGrowth& out);
/*
    A cane's leaf sprays: at its nodes - `nodes_per_tile` to each copy of a stalk tile
    `tile_length` long at scale 1, counted and stretched as the deform lays them, so a spray sits
    on a ring - over the upper part only, alternating sides with a hashed turn, each leaning out
    and up off the cane. Kind VINE_LEAF_BAMBOO. Kept out of the field like the vine's leaves.
*/
void  ScatterBambooLeaves(const Spline& spline, const VinePath& path, const VineParams& params,
                          float tile_length, int nodes_per_tile, const VineField* field,
                          std::vector<VineLeaf>& out);
//From p out along `normal` to where the field is open - p itself if it already is. At most 3 units.
vec3  VineMarchOut(const VineField& field, const vec3& p, const vec3& normal);
//ScatterVineLeaves, keeping the leaves out of any field rather than out of the blocks' boxes.
void  ScatterVineLeaves(const Spline& spline, const VinePath& path, const VineParams& params,
                        const VineField* field, std::vector<VineLeaf>& out);

//The distance from p to the nearest live block, in the play plane (blocks fill the slab's depth);
//negative inside one. What the walk keeps the trunk's radius clear of.
float VineBlockDistance(const std::vector<StageBlock>& blocks, float x, float y);

//The rotation taking local X, Y, Z onto the given orthonormal, right-handed axes.
quat  QuatFromBasis(const vec3& x, const vec3& y, const vec3& z);

#endif

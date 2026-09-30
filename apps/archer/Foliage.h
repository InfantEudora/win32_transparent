#ifndef _ARCHER_FOLIAGE_H_
#define _ARCHER_FOLIAGE_H_

#include "Stage.h"
#include "type_vec3.h"

#include <vector>

/*
    Where the plants go on the blockout - ferns and flowers scattered over the tops of the boxes,
    thickest in the inside corners the way ambient occlusion is darkest there.

    Pure placement: blocks in, a list of plants out. No engine type anywhere, for the same reason
    Stage.h has none - `make rules` builds this against stage_test.cpp alone and checks the two
    properties that matter (nothing grows under a box standing on another, and a corner is
    denser than open floor). ApplicationArcher turns the list into Objects.

    --- THE THREE STEPS -------------------------------------------------------------------------
      1. The exposed tops. Each growing block's top edge, minus wherever another block sits on it
         or hangs lower above it than a plant is tall. That subtraction is the whole of "only the
         top block of a stack grows anything": the lower block's top is covered, so it is not a
         surface.
      2. An occlusion score per spot, from rays fanned across the upper half-circle in the play
         plane against every block. Open floor scores 0, the foot of a wall about half, a narrow
         slot more. A cliff edge - a CONVEX corner - scores 0, so plants do not crowd a drop.
      3. A walk along each top, placing a plant with a probability that rises with the score, a
         minimum spacing so a clump reads as a clump rather than a pile, and the species chosen
         by the score too: ferns in the shade, flowers in the open.

    DETERMINISTIC. Every random number is a hash of where it is being drawn, so the same level
    always grows the same garden and nothing here touches the engine's shared RRandom stream.
*/

enum FoliageKind{
    FOLIAGE_FERN = 0,       //fern_1: the tall, broad one - wants shade
    FOLIAGE_FERN_LOW,       //fern_2: ground cover, 0.08 tall - goes anywhere, fills the gaps
    FOLIAGE_FLOWER,         //flower: wants the open
    //grass_1: a small clump of blades, the ones cut off the terrain tiles. NOT drawn in the
    //lottery the other three share - it has a pass and a density of its own, because a lawn is
    //many small clumps and the lottery tops out at under one plant a unit on open ground.
    FOLIAGE_GRASS,
    //grass_2: the second clump, drawn by the same pass - each clump picks one of the two.
    FOLIAGE_GRASS_2,
    FOLIAGE_KIND_COUNT
};

/*
    Everything that decides how the garden looks. All of it is tuning, and the app puts the
    first four on sliders.
*/
struct FoliageParams{
    //Plants per unit of length on open floor, and in a fully shaded corner. The density at a spot
    //runs between the two with the occlusion score.
    float density_open    = 0.8f;
    float density_corner  = 12.0f;
    //How far the occlusion rays reach. Bigger means a wall's influence spreads further out across
    //the floor in front of it - it is the size of the "darkening" around a corner.
    float ao_radius       = 3.0f;
    //Shapes the falloff: above 1 hugs the corner tighter, below 1 spreads it.
    float ao_gamma        = 1.0f;

    int   ao_rays         = 16;
    //Distance between candidate spots along a top. Well under the spacing below, so the spacing
    //rather than this grid decides where plants land.
    float step            = 0.05f;
    /*
        No two plants closer than this, measured in (x,z), in units of the pair's larger radius.

        UNDER 1, so neighbouring fronds interleave. It is also the ceiling on how dense a corner
        can get, whatever density_corner says: measured on a floor-and-wall, 1.1 held the foot of
        the wall to about 3 plants a unit at any density, and 0.8 lets it reach 5 to 6 against 0.9
        on the open floor.
    */
    float spacing         = 0.8f;
    //Tries per candidate spot at different depths, so a dense corner can fill its depth rather
    //than being limited to one plant per step.
    int   tries_per_step  = 3;

    /*
        Depth, as it would be on a block of the default depth at z 0 (STAGE_BLOCK_HALF_DEPTH, the
        slab -1.5 .. 1.5): a block with its own depth or z keeps the same two margins from its own
        back and front, so plants stay on its flat top however it is set. Biased toward the back
        (bias > 1) so the plants stand behind the archer, who walks at z 0, rather than over her
        feet.
    */
    float z_back          = -1.35f;
    float z_front         = 1.20f;
    float z_bias          = 1.6f;

    /*
        The grass pass - see FOLIAGE_GRASS. The OPPOSITE curve to the others': thickest in the
        open, thinning into the shade, where the ferns take over. Spread evenly through the depth
        rather than biased back, because a clump this low hides nothing of her.
    */
    float grass_open      = 7.0f;     //clumps per unit of length, in the open
    float grass_corner    = 1.0f;     //and in a fully shaded corner
    int   grass_tries     = 4;
    float grass_2_share   = 0.5f;     //the fraction of clumps that are grass_2 rather than grass_1

    //How far a plant's centre keeps from a drop-off. Walls are hugged instead - see clearance.
    float edge_inset      = 0.10f;
    //How far a plant's centre keeps from a wall standing on its top, as a fraction of its radius.
    //Under 1 on purpose: a fern with its fronds into the wall reads as growing against it.
    float wall_clearance  = 0.5f;

    float scale_jitter    = 0.2f;     //each plant is 1 +- this times its kind's scale

    //Per-kind size at scale 1 (radius in the ground plane, height), measured off the meshes by
    //the app. The defaults are the archer.glb props at the character's scale, for the tests.
    float radius[FOLIAGE_KIND_COUNT] = { 0.55f, 0.36f, 0.29f, 0.24f, 0.24f };
    float height[FOLIAGE_KIND_COUNT] = { 0.64f, 0.19f, 0.55f, 0.27f, 0.27f };
};

struct FoliagePlant{
    int   kind  = FOLIAGE_FERN;
    float x = 0.0f, y = 0.0f, z = 0.0f;     //y is the surface it stands on
    float yaw   = 0.0f;                     //radians about +Y
    float scale = 1.0f;                     //multiplier on the kind's own scale
    float occlusion = 0.0f;                 //the score it was placed with, for inspection
};

/*
    The occlusion score at (x, y) - the fraction of the upper half-circle that is blocked within
    ao_radius, each hit counted by how close it is. 0 in the open, about 0.5 at the foot of a wall.
    Exposed for the tests and for anyone tuning ao_radius.
*/
float FoliageOcclusion(const std::vector<StageBlock>& blocks, float x, float y,
                       const FoliageParams& params);

/*
    What a biome (Stage.h) does to the garden: multipliers on the params' densities, and a keep
    chance per kind - drawn only where it is under 1, so the jungle's garden, all ones, is the
    garden it always was, plant for plant. In a biome's fade the rules blend toward the jungle's.
*/
struct FoliageBiome{
    float open = 1.0f;          //on density_open
    float corner = 1.0f;        //on density_corner
    float grass = 1.0f;         //on grass_open and grass_corner
    float keep[FOLIAGE_KIND_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
};
/*
    The rules per StageBiomeKind. The CAVE: no daylight, so no grass and no flowers; a low fern
    here and there as ground cover, thicker at the foot of a wall, and hardly a tall one.
*/
FoliageBiome FoliageBiomeFor(int biome);

/*
    The whole garden. `grows[i]` says whether block i may carry plants at all - the app uses it to
    keep them off the terrain bays. Whatever the mask says, only SOLID and LEDGE tops grow: not a
    one-way platform, and not a breakable wall that is about to be kicked away. EVERY live block
    shades and covers, whatever its kind, which is what puts ferns in the shadow under the
    platform. `biomes`, if given, set the rules spot by spot (FoliageBiomeFor). `out` is cleared
    first.
*/
void ScatterFoliage(const std::vector<StageBlock>& blocks, const std::vector<bool>& grows,
                    const FoliageParams& params, std::vector<FoliagePlant>& out,
                    const std::vector<StageBiome>* biomes = NULL);

/*
    A TUFT - the small clump an arrow leaves where it grows something (docs/vine_plan.md section 11):
    on the top above an underside it struck, or where it stuck in a wall or a top. A few of the
    garden's own plants, mostly grass, now and then a low fern or a flower, spread on a disc
    around `at` in the plane square to `up` - so on a wall it grows out sideways. Each staggered
    by a few ticks, so the clump comes up rather than appearing whole.

    Hashed on where and the seed, like the rest of the garden, and nothing here is placed against
    the blocks: a tuft is small enough that the surface it stands on is the one it was asked for.
    The app turns each into a pooled plant; `scale` is a multiplier on the kind's own mesh.
*/
struct TuftPlant{
    int   kind = FOLIAGE_GRASS;
    vec3  position;
    vec3  up;                   //the plant's +Y
    float yaw = 0.0f;           //about `up`
    float scale = 1.0f;
    int   delay_ticks = 0;      //after the tuft starts
};
void ScatterTuft(const vec3& at, const vec3& up, int seed, float radius, std::vector<TuftPlant>& out);

#endif

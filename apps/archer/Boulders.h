#ifndef _ARCHER_BOULDERS_H_
#define _ARCHER_BOULDERS_H_

#include "Stage.h"

#include <vector>

/*
    Where the rocks lie - where they would have come to rest after falling.

    NOT the plants' spread. Foliage is a density over every exposed top; rocks are a few clusters,
    and each is at an INSIDE CORNER - the foot of a wall where it rises out of a platform top - the
    one place a fallen rock stops. Each cluster is one big rock pushed into the corner and toward
    the BACK of the platform, the far side from the camera, with a scatter of small ones round its
    base on the open side.

    Pure placement, like Foliage: blocks in, rocks out, no engine type, so `make rules` checks it.
    DETERMINISTIC - every random number is a hash of the corner it is drawn for.

    --- A CORNER, EXACTLY -------------------------------------------------------------------------
    A face of a block W at x_f, on the top of a block A, when:
      - A is a live SOLID or LEDGE (what the plants grow on);
      - W reaches down to A's top and rises at least min_wall above it - standing on A, or beside
        its end - and is not a one-way platform or a breakable wall (a rock heaped against a wall
        that is about to be kicked away reads wrong);
      - the open side of the face lies on A's top for at least `room`, and is not covered.

    --- DEPTH ------------------------------------------------------------------------------------
    Behind her: every rock's footprint stays behind z_front_max, which is behind her walking line,
    so she passes in front of the rocks rather than through them. A big rock that would not fit
    between the platform's back and that line is shrunk until it does.
*/
/*
    One kind per mesh in archer.glb, named after its node with the variant number dropped to the
    enum's. A cluster's big rock is drawn from the BIG kinds by the corner's hash, so a level gets
    both shapes and the same corner always gets the same one.
*/
enum BoulderKind{
    BOULDER_BIG_1 = 0,      //rock_big_1
    BOULDER_BIG_2,          //rock_big_2
    BOULDER_SMALL_1,        //rock_small_1
    BOULDER_KIND_COUNT
};
#define BOULDER_BIG_KINDS   2   //BOULDER_BIG_1 .. BOULDER_BIG_1 + this - 1

inline bool IsBigBoulder(int kind){
    return (kind >= BOULDER_BIG_1) && (kind < BOULDER_BIG_1 + BOULDER_BIG_KINDS);
}

struct BoulderParams{
    float min_wall      = 0.8f;     //a face must rise this far above the top to be a corner
    float room          = 1.5f;     //of open top beside it, at least
    float cluster_chance = 0.7f;    //per corner - "a few big ones", not one in every corner
    float big_scale_min = 1.6f;     //multiplier on the kind's own scale, hashed between these
    float big_scale_max = 2.3f;     //about 1.4 tall at the top, capped by the depth - see back_overhang
    int   small_min     = 3;        //small rocks round each big one, hashed between these
    int   small_max     = 6;
    float small_scale_min = 1.0f;
    float small_scale_max = 2.0f;     //under 1 they vanish in the ferns at a wall's foot
    float small_reach   = 1.4f;     //how far past the big rock's edge the small ones spread
    float z_front_max   = -0.15f;   //no rock's footprint comes nearer the camera than this
    /*
        How much of the BIG rock's footprint may hang past the platform's back edge. Without it the
        rock has to fit between that edge and z_front_max, 1.3 units on a default slab, which held
        every big rock to 0.92 of the mesh - all the same size, and small. Seen from the front the
        overhanging part is behind the rock itself, so it reads as a rock on the lip.
    */
    float back_overhang = 0.5f;
    float back_inset    = 0.10f;    //the nearest a small rock's CENTRE comes to the platform's back
    float sink          = 0.12f;    //fraction of its height set into the ground
    float small_tilt_deg = 14.0f;   //a small rock is tipped by up to this, the way a fallen one is

    //Per-kind size at scale 1 - radius in the ground plane (the widest the yaw can make it) and
    //height - measured off the meshes by the app. Defaults are the archer.glb rocks at her scale,
    //the second big one given the first's numbers until it is measured.
    //A kind at radius 0 - its mesh did not load - is never chosen.
    float radius[BOULDER_KIND_COUNT] = { 0.44f, 0.44f, 0.34f };
    float height[BOULDER_KIND_COUNT] = { 0.67f, 0.67f, 0.24f };
};

struct Boulder{
    int   kind = BOULDER_BIG_1;
    float x = 0.0f, y = 0.0f, z = 0.0f;     //y is the base, already sunk
    float yaw = 0.0f;                       //radians about +Y
    float tilt = 0.0f;                      //radians about the axis below
    float tilt_axis_yaw = 0.0f;             //the horizontal axis it tips about, as a yaw
    float scale = 1.0f;                     //multiplier on the kind's own scale
    float ground = 0.0f;                    //the top it stands on, before the sink
};

//Every corner that qualifies, as (x of the face, side the open ground is on: -1 or +1, block
//index of the top). Exposed for the tests.
struct BoulderCorner{
    float x = 0.0f;
    float side = -1.0f;
    int   top = -1;
};
void FindBoulderCorners(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                        std::vector<BoulderCorner>& out);

//The rocks. `out` is cleared first.
/*
    What a biome (Stage.h) does to the rocks. The jungle's is nothing - no draw is made for it, so
    its rocks are the rocks they always were. The CAVE is rubble: every corner gets its cluster,
    and small rocks lie strewn along the open floor as well, now and then a big one among them -
    what has come down from the roof. In a biome's fade both blend toward the jungle's.
*/
struct BoulderBiome{
    float cluster_at_least = 0.0f;  //a corner's cluster chance is at least this
    float rubble = 0.0f;            //rocks per unit of open top
    float rubble_big = 0.0f;        //the share of them that are big
};
BoulderBiome BoulderBiomeFor(int biome);

void ScatterBoulders(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                     std::vector<Boulder>& out, const std::vector<StageBiome>* biomes = NULL);

#endif

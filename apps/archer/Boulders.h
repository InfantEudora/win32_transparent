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
    //The cave's own - see THE CAVE'S FLOOR, ROOF AND WALLS below. Never placed outside a biome
    //that asks for them.
    BOULDER_FLAT,           //rock_flat: a flat stone, sunk into the floor
    BOULDER_STALAGMITE,     //stalagmite
    BOULDER_STALACTITE,     //stalagtite (the export's spelling): hangs, its origin at the root
    BOULDER_ROOT,           //root_big: hangs from the roof the same way, come through from above
    BOULDER_BONE,           //bone
    BOULDER_RIBCAGE,        //bone_ribcage
    BOULDER_SKULL,          //skull
    BOULDER_SKULL_STICK,    //skullonstick
    BOULDER_SNAKE,          //snake_skeleton_big
    BOULDER_MUSHROOM_1,     //mushroom_small_1
    BOULDER_MUSHROOM_2,     //mushroom_small_2
    BOULDER_CANTHARELL,     //mushroom_cantharell
    BOULDER_BRACKET_1,      //mushroom_tree_1: a bracket fungus, its origin on the wall, out along +Z
    BOULDER_BRACKET_2,      //mushroom_tree_2
    BOULDER_KIND_COUNT
};
#define BOULDER_BIG_KINDS   2   //BOULDER_BIG_1 .. BOULDER_BIG_1 + this - 1

inline bool IsBigBoulder(int kind){
    return (kind >= BOULDER_BIG_1) && (kind < BOULDER_BIG_1 + BOULDER_BIG_KINDS);
}

//What a kind is fixed to: a top it stands on, an underside it hangs from, or a wall's face.
enum BoulderMount{ BOULDER_ON_TOP = 0, BOULDER_UNDER, BOULDER_ON_WALL };
int  BoulderMountOf(int kind);
//The big rocks, and the cave's pieces big enough to be worth a place in the shadow pass.
bool BoulderCastsShadow(int kind);

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
    //A hanging kind's height is how far it hangs below its origin; a wall kind's is its thickness.
    float radius[BOULDER_KIND_COUNT] = { 0.44f, 0.44f, 0.34f,
                                         1.50f, 0.48f, 0.92f, 0.50f, 0.40f, 0.56f, 0.48f, 0.28f, 2.10f,
                                         0.22f, 0.20f, 0.32f, 0.30f, 0.34f };
    float height[BOULDER_KIND_COUNT] = { 0.67f, 0.67f, 0.24f,
                                         0.24f, 1.60f, 2.28f, 2.32f, 0.16f, 0.54f, 0.56f, 2.22f, 1.16f,
                                         0.68f, 0.50f, 0.52f, 0.14f, 0.18f };
    /*
        How far a standing kind's lowest point is below its origin. 0 for a model authored base
        down, which is most of them; the skull, the skull on its stick and the snake's skeleton
        have their origins inside them, and without this they stand sunk to the middle.
    */
    float base[BOULDER_KIND_COUNT] = { 0.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, 0.08f, 0.06f, 0.24f, 1.42f, 0.28f,
                                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    //The snake's skeleton lies at a fixed yaw, so it is fitted by its own extent through the slab
    //at yaw 0 rather than by its radius: back and front of its origin, at her scale.
    float snake_z_back  = -0.56f;
    float snake_z_front = 1.86f;

    //--- The cave's (THE CAVE'S FLOOR, ROOF AND WALLS) --------------------------------------------
    /*
        The nearest a cave piece that is not rock comes to its floor's back: the stream's front
        edge (Water.cpp: the ground's back + 0.3). A stone in the water reads as a stone in a
        stream, where a mushroom or a bone reads as a mistake.
    */
    float cave_back_inset = 0.30f;
    float hang_sink       = 0.20f;  //how far a hanging piece's root is set up into the rock
    float hang_z_back     = -3.0f;  //the furthest back a hanging piece hangs - past it is the bank
    float snake_scale     = 0.5f;   //at 1 it is 3.4 long and 2.4 deep, and the floor is 1.35 behind her
    float stick_scale     = 0.75f;  //at 1 the skull on its stick is a head taller than her
};

struct Boulder{
    int   kind = BOULDER_BIG_1;
    float x = 0.0f, y = 0.0f, z = 0.0f;     //the origin: y is the base, already sunk
    float yaw = 0.0f;                       //radians about +Y
    float tilt = 0.0f;                      //radians about the axis below
    float tilt_axis_yaw = 0.0f;             //the horizontal axis it tips about, as a yaw
    float scale = 1.0f;                     //multiplier on the kind's own scale
    //The top it stands on, before the sink - or for a hanging kind the underside it hangs from,
    //and for a wall kind the height it is at on the face.
    float ground = 0.0f;
};
//The front of a piece's footprint through the slab, which must stay behind z_front_max.
float BoulderFrontZ(const Boulder& b, const BoulderParams& params);
//Whether two wall pieces grow through each other - closer than their thicknesses up the face and
//their radii through the slab, both times `slack`. Pieces on different faces never do.
bool BoulderWallOverlap(const Boulder& a, const Boulder& b, const BoulderParams& params, float slack);

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
/*
    --- THE CAVE'S FLOOR, ROOF AND WALLS ------------------------------------------------------------
    Everything else a cave has lying about, placed after the rubble and clear of it - but for the two
    set pieces, which go before it so that it makes room for them - and only where a biome's numbers
    below are above zero, so no draw is made for the jungle at all. Densities are
    per unit of length, as the rubble's are, scaled by the biome's fade toward the mouth and by
    `deep` toward its far end: the back of a cave is where things have been gathering longest.

      ON THE ROOF'S UNDERSIDE: stalactites, thickest toward the back, and some share of them with
        a stalagmite grown up under the drip; hanging roots, the other way round - thickest nearest
        the mouth, where the jungle above is.
      ON THE FLOOR: stalagmites on their own as well, flat stones sunk into it, heaps of remains
        (a ribcage or a skull, with a bone or two), and clusters of mushrooms of one kind - more at
        a stalagmite's foot. Once each, the set pieces: the snake's skeleton deep inside, and a
        skull on a stick at the back of the floor just past where the daylight gives out.
      ON A WALL'S FACE: bracket fungus in short stacks, low down and apart - seen side on each one
        is a little shelf, and a tall row of them up a wall reads as steps to climb.

    Everything stays behind her walking line like every rock (z_front_max), so she passes in front
    of all of it and none of it covers a shot; nothing here collides. Minerals may stand in the
    cave's stream as the rubble does; bones and mushrooms keep cave_back_inset out of it - all but
    the snake, which is fitted to the whole floor behind her and lies with its tail at the water's
    edge. Hanging pieces go no further back than hang_z_back. Their y here is the box's underside;
    the app moves each up or down to the rock as drawn (ApplicationArcher's HangFromTerrain), which
    under the cave's roof is most of a unit lower, and from there the longest root reaches to about
    5.3 - still clear of her head at the top of a jump. Every draw is a hash of where it is, like the
    rest of this file.
*/
struct BoulderBiome{
    float cluster_at_least = 0.0f;  //a corner's cluster chance is at least this
    float rubble = 0.0f;            //rocks per unit of open top
    float rubble_big = 0.0f;        //the share of them that are big
    //The cave's own, per unit of open top, underside or (brackets) wall height.
    float drips = 0.0f;             //stalactites
    float drip_mites = 0.0f;        //the share of stalactites with a stalagmite under them
    float roots = 0.0f;             //hanging roots
    float mites = 0.0f;             //stalagmites standing on their own
    float flats = 0.0f;             //flat stones
    float remains = 0.0f;           //heaps of bones
    float mushrooms = 0.0f;         //clusters of mushrooms
    float mite_mushrooms = 0.0f;    //the chance a stalagmite has a cluster at its foot
    float brackets = 0.0f;          //stacks of bracket fungus
    //How much thicker the above are at the far end than at the mouth: each density runs from
    //1 - deep there to 1 + deep at the far end (roots the other way). 0 is even throughout.
    float deep = 0.0f;
    bool  f_set_pieces = false;     //the snake's skeleton and the skull on a stick
};
BoulderBiome BoulderBiomeFor(int biome);

void ScatterBoulders(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                     std::vector<Boulder>& out, const std::vector<StageBiome>* biomes = NULL);

#endif

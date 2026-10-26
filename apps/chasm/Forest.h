#ifndef _CHASM_FOREST_H_
#define _CHASM_FOREST_H_

#include <stdint.h>
#include <vector>
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"
#include "TerrainMesh.h"

/*
    THE FORESTS AND SCATTERED PROPS: where the modelled trees, rocks and bushes
    (assets/meshes/chasm_props.glb, art_source/chasm/chasm_props.blend) stand. grid_plan.md step 7.

    GENERATED, like the terrain: decided by the seed and the grid alone, so it is never saved and a
    map always grows the same forest. Decided PER PLOT from a hash of the seed and the plot - not
    drawn from a random stream - so a plot's tree does not depend on the order plots are visited.

    Where: large smooth noise patches are the forests. Their cores are dense conifers; a band round
    them thins out into oaks, pines and bushes; open land has the odd tree, bush and rock. Only on
    plots flat all round (every corner of every cell around the plot on the plot's level): nothing
    stands on a cliff edge or straddles a wall.

    Painting a zone does not remove props - the VIEW hides those on a plot with a house or in a
    field's cell. Felling as gameplay, which would make trees simulation state, is later.
*/

enum PropKind{
    PROP_PINE_A = 0, PROP_PINE_B, PROP_PINE_C, PROP_PINE_D,
    PROP_OAK_A, PROP_OAK_B, PROP_OAK_C,
    PROP_WILLOW,                //the swamp's tree (biomes_plan.md step 4)
    PROP_PALM,                  //the desert's (step 5)
    PROP_SNOW_PINE,             //the mountain's, below the snow (step 5)
    PROP_ROCK_A, PROP_ROCK_B, PROP_ROCK_C, PROP_ROCK_CLUSTER,
    PROP_STUMP, PROP_LOG, PROP_BUSH_A, PROP_BUSH_B,
    //Ground cover: small, numerous, and casting no shadow (PropCastsShadow).
    PROP_GRASS_A, PROP_GRASS_B, PROP_GRASS_C, PROP_FLOWERS_A, PROP_FLOWERS_B,
    PROP_FERN, PROP_SHRUB, PROP_MUSHROOMS, PROP_TWIG,
    //The wet band's own cover: the swamp's shallows and the rivers' banks (biomes_plan.md, "Reeds").
    PROP_REEDS_A, PROP_REEDS_B,
    //A rock bigger than any other, which a building waits on until it is broken (stone_plan.md). Last, after the
    //cover, so no kind before it moves.
    PROP_BOULDER,
    //An OUTCROP's look (Deposits.h): beds of rock on a pocket's floor.
    PROP_OUTCROP,
    PROP_KIND_COUNT
};
#define PROP_COVER_FIRST    PROP_GRASS_A
//Ground cover's shadow is invisible at the game's zoom and the shadow pass is the dearest one. Everything after the
//reeds is stone.
inline bool PropCastsShadow(int kind){ return kind < PROP_COVER_FIRST || kind >= PROP_BOULDER; }
//A tree, for felling; a ROCK, for breaking - a boulder too (stone_plan.md).
inline bool ForestIsTree(int kind){ return kind <= PROP_SNOW_PINE; }
inline bool ForestIsRock(int kind){
    return (kind >= PROP_ROCK_A && kind <= PROP_ROCK_CLUSTER) || kind == PROP_BOULDER;
}
/*
    What a GARDEN keeps of what grows on it (gardens_plan.md, the user's 2026-10-26): its broadleaf trees - the oaks, the
    willow, the palm - and the plants a garden would have. Not the pines, which are felled for it as for anything else,
    nor grass, twigs, mushrooms, reeds or stone.
*/
inline bool ForestGardenKeeps(int kind){
    return (kind >= PROP_OAK_A && kind <= PROP_PALM) || kind == PROP_BUSH_A || kind == PROP_BUSH_B
           || kind == PROP_FLOWERS_A || kind == PROP_FLOWERS_B || kind == PROP_FERN || kind == PROP_SHRUB;
}
//What breaking a rock gives, by its size (stone_plan.md, current tuning).
inline int ForestRockStone(int kind){
    return (kind == PROP_BOULDER) ? 12 : (kind == PROP_ROCK_CLUSTER) ? 6 : (kind == PROP_ROCK_C) ? 4 : ForestIsRock(kind) ? 2 : 0;
}
#define FOREST_BOULDER_CHANCE   0.30f   //of the open land's rocks, the share that are boulders
//The node name of each kind in chasm_props.glb.
extern const char* prop_asset_names[PROP_KIND_COUNT];

struct PropInstance{
    uint8_t kind = 0;
    bool f_fruit = false;   //an oak that is a FRUIT TREE (foraging_plan.md)
    vec3 pos;           //on the ground's relief
    float yaw = 0.0f;   //radians
    float scale = 1.0f;
    int plot = -1;      //the plot it stands on - a house there hides it
    int coarse = -1;    //the coarse cell under it - a field there hides it
};

/*
    THE PLANTS THAT BEAR FRUIT (foraging_plan.md, the user's 2026-10-12): the two bushes, and of the oaks - "the
    light green trees" - a small share, from a hash of their own, so every other prop stands where it did.
*/
#define FOREST_FRUIT_TREE_CHANCE    0.10f
inline bool ForestBears(const PropInstance& p){
    return p.kind == PROP_BUSH_A || p.kind == PROP_BUSH_B || p.f_fruit;
}

struct ForestData{
    std::vector<PropInstance> props;
    std::vector<std::vector<int>> chunk_props;  //per terrain chunk, indices into props
    int count[PROP_KIND_COUNT] = {};
    /*
        Per fine vertex: 1 inside a forest's dense CORE - its noise past the core's threshold - whatever stands there,
        a cliff's edge or a river's bank included, so the forest is one shape rather than the plots a tree took.
        The starting land's forest as an area (Areas.h) is these.
    */
    std::vector<uint8_t> core;
    //Per fine vertex: the tree, bush or rock standing in that plot (pr.plot), -1 none - a garden's planting keeps clear of
    //it (Gardens.h). One at most: the forest grows one a plot, and the odd one strayed in from beside it comes second.
    std::vector<int> plot_prop;
    float build_ms = 0.0f;
};

struct DepositData;
//`deposits` (may be NULL) says where the outcrops stand.
void BuildForest(const Grid& g, const GridPicker& picker, const Terrain& t, const TerrainMeshData& mesh,
                 const DepositData* deposits, ForestData& out);

#endif

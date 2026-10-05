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
    PROP_ROCK_A, PROP_ROCK_B, PROP_ROCK_C, PROP_ROCK_CLUSTER,
    PROP_STUMP, PROP_LOG, PROP_BUSH_A, PROP_BUSH_B,
    //Ground cover: small, numerous, and casting no shadow (PropCastsShadow).
    PROP_GRASS_A, PROP_GRASS_B, PROP_GRASS_C, PROP_FLOWERS_A, PROP_FLOWERS_B,
    PROP_FERN, PROP_SHRUB, PROP_MUSHROOMS, PROP_TWIG,
    PROP_KIND_COUNT
};
#define PROP_COVER_FIRST    PROP_GRASS_A
//Ground cover's shadow is invisible at the game's zoom and the shadow pass is the dearest one.
inline bool PropCastsShadow(int kind){ return kind < PROP_COVER_FIRST; }
//The node name of each kind in chasm_props.glb.
extern const char* prop_asset_names[PROP_KIND_COUNT];

struct PropInstance{
    uint8_t kind = 0;
    vec3 pos;           //on the ground's relief
    float yaw = 0.0f;   //radians
    float scale = 1.0f;
    int plot = -1;      //the plot it stands on - a house there hides it
    int coarse = -1;    //the coarse cell under it - a field there hides it
};

struct ForestData{
    std::vector<PropInstance> props;
    std::vector<std::vector<int>> chunk_props;  //per terrain chunk, indices into props
    int count[PROP_KIND_COUNT] = {};
    float build_ms = 0.0f;
};

void BuildForest(const Grid& g, const GridPicker& picker, const Terrain& t, const TerrainMeshData& mesh,
                 ForestData& out);

#endif

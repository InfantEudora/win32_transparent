#ifndef _CHASM_TERRAIN_MESH_H_
#define _CHASM_TERRAIN_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "Grid.h"
#include "Terrain.h"

/*
    THE TERRAIN AS TRIANGLES: the ground of every level, the cliff walls between them, and a skirt
    down the map's outline so the map reads as a block cut out of the world rather than a sheet.

    grid_plan.md section 4. Per fine cell:
      - all four corners on one level: two flat triangles at that height.
      - mixed: marching squares on high corners against low ones, cut at the edge midpoints. The
        high part is one polygon at the high height, each run of low corners its own polygon at the
        low height, and between them a wall down the cut. In the saddle case (high, low, high, low)
        the high corners are joined, so the plateau stays in one piece.
    Wall points are edge midpoints, which the neighbouring cell computes identically, and the
    wall's roughness is a function of world position alone - so neighbouring walls meet without a
    crack however the cells around them are cut.

    VIEW ONLY: built from the terrain, uploaded on the render thread, read by nothing in a tick.
    Cut into square chunks so a frame draws only the chunks in view (Renderer culls by mesh bounds)
    and a later edit rebuilds only the chunks it touches (section 6).
*/

/*
    Mesh material slots - TerrainMesh writes these as vertex matid; the app binds a material to
    each. FOUR, because an Object has NUM_MATERIAL_SLOTS = 4. A stand-in until the palette texture
    (step 4), where every colour becomes a UV into one material and this list goes away.
*/
#define TERRAIN_SLOT_GROUND     0       //plateau and shard tops
#define TERRAIN_SLOT_FLOOR      1
#define TERRAIN_SLOT_ROCK_A     2
#define TERRAIN_SLOT_ROCK_B     3       //and the skirt
#define TERRAIN_NUM_SLOTS       4

#define TERRAIN_CHUNK_SIZE      48.0f       //world units on a side
#define TERRAIN_SKIRT_BOTTOM    -90.0f      //below the floor, so the block has a base

struct TerrainChunk{
    std::vector<vertex> verts;
};

struct TerrainMeshData{
    std::vector<TerrainChunk> chunks;
    int chunks_x = 0;
    int chunks_z = 0;
    int num_triangles = 0;
    int num_wall_triangles = 0;
    float build_ms = 0.0f;
};

void BuildTerrainMesh(const Grid& g, const Terrain& t, TerrainMeshData& out);

#endif

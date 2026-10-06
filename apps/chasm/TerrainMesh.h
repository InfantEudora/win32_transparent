#ifndef _CHASM_TERRAIN_MESH_H_
#define _CHASM_TERRAIN_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "Grid.h"
#include "Terrain.h"

/*
    THE TERRAIN AS TRIANGLES: the ground of every level, the cliff walls between them, and a skirt
    down the map's outline so the map reads as a block cut out of the world rather than a sheet.

    grid_plan.md, "Columns" (2026-10-06): every plot is a COLUMN standing at its own height. Per
    fine cell:
      - all four corners at one height: two flat triangles at that height.
      - mixed: in quarters, each corner's - the corner, its two edges' midpoints, the cell's centre -
        at its plot's top, and a wall on every inner half-edge (midpoint to centre) between two
        quarters at different heights. A plot's top is the quarters round it from every cell it is a
        corner of. (Terrain has joined every saddle, so no four walls share a centre.)
    Walls are cut at one global set of rings, and a ring point's place is a function of the plots
    round it, the ring and the half-edges through it - a midpoint's from both cells on its edge - so
    neighbouring walls meet without a crack however the cells around them are cut.

    VIEW ONLY: built from the terrain, uploaded on the render thread, read by nothing in a tick.
    Cut into square chunks so a frame draws only the chunks in view (Renderer culls by mesh bounds)
    and a later edit rebuilds only the chunks it touches (section 6).
*/

/*
    COLOUR IS A UV INTO THE PALETTE (Palette.h): every vertex has matid 0 and picks its cell, so the
    whole terrain draws with the one palette material.
*/

#define TERRAIN_CHUNK_SIZE      48.0f       //world units on a side
#define TERRAIN_SKIRT_BOTTOM    -90.0f      //below the floor, so the block has a base

struct TerrainChunk{
    std::vector<vertex> verts;
    std::vector<int> quads;     //the fine quads whose ground is in this chunk - what else lays out
                                //per chunk (the zones, ZoneMesh.h) uses the same split
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

//The level plus its small BUMP only, the same on every map. Not the ground: for anything that stands
//or is drawn on the ground, use Terrain::GroundHeight, which adds the world's relief (hills, the
//mountain) on top of this. Both leave out the rivers' channels: nothing stands near enough a river
//to be over one (Terrain.h, RIVERS).
float TerrainGroundHeight(const vec2& p, float level_height);
//The chunk a fine quad belongs to, by its centre - the same rule BuildTerrainMesh uses.
int TerrainChunkOfQuad(const Grid& g, const TerrainMeshData& m, int quad);

#endif

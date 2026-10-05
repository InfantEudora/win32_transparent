#ifndef _CHASM_WATER_MESH_H_
#define _CHASM_WATER_MESH_H_

#include <vector>
#include "type_vertex.h"
#include "Grid.h"
#include "Terrain.h"

/*
    THE WATER AS TRIANGLES (grid_plan.md step 9): the rivers' surface and the falls. Both are drawn
    by assets/shaders/chasm_water.glsl rather than the palette - the water moves - so their UVs are
    the shader's coordinates, not palette cells:

      - FLAT, the rivers: every plateau cell a channel lowers, at TERRAIN_WATER_Y. The ground cuts
        through it, so the shore is wherever the two cross, and the shader finds that line in the
        G-buffer to draw foam along it. A cell at the rim is laid only over its plateau part, so the
        water stops exactly at the lip. uv.x is across the river (-1..1 at the water's edges), uv.y
        the distance from its source - the shader runs streaks down it.
      - SHEETS, the falls: from just behind the lip, curling over it and out past the wall's rough
        strata, then straight down into the mist. uv.x across (-1..1), uv.y down - 1 where it meets
        the mist (CHASM_MIST_TOP), so the shader's white foot sits on the blanket.

    View only, like the terrain mesh. Small - a few thousand triangles - so one object each.
*/

struct WaterMeshData{
    std::vector<vertex> flat;
    std::vector<vertex> sheets;
    float build_ms = 0.0f;
};

void BuildWaterMesh(const Grid& g, const Terrain& t, WaterMeshData& out);

#endif

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
        water stops exactly at the lip - except at a fall, where it runs out to the fall's BRINK, a
        straight line just past every rock under it (waterfall_plan.md). uv.x is across the river
        (-1..1 at the water's edges), uv.y the distance from its source - the shader runs streaks
        down it.
      - SHEETS, the falls: hung from the brink - its top row the river's own points there, to the bit,
        with the river's UVs and normal, so the two are one surface - curling over and out past the
        wall's rough strata, then straight down into the mist. uv.x carries the river's across down
        (eased to +-SHEET_EDGE at its edges), uv.y the river's along in flow time, so the streaks pour
        on unbroken; the shader finds the drop and the mist from the world height.

    View only, like the terrain mesh. Small - a few thousand triangles - so one object each.
*/

//The rivers' streaks, world units a second (SetWaterFlatUniforms); a fall's sheet counts its uv.y by it.
#define WATER_RIVER_FLOW    2.2f

//A sheet as drawn, for what goes round it: the foam and spray at its foot, the wet rock behind it.
struct WaterSheetShape{
    /*
        Its foot as a fall for the foam and spray (Mist.h, FoamBall): the terrain's fall or spring with
        `lip` moved FOAM_SHEET_OUT behind where the sheet meets the mist.
    */
    TerrainFall foot;
    vec2 origin;                //the lip on the rim line...
    vec2 out;                   //...and out over the chasm; across is out turned left, as a fall's
    float mid = 0.0f;           //along the rim from origin: the sheet's middle at its top
    float half = 0.0f;          //and half its width there
    float top_y = 0.0f;         //where it goes over
    float front = 0.0f;         //out from origin, the furthest the sheet hangs
};
#define WATER_MAX_SHEETS    12  //the ground shader's wet rock (chasm_ground.frag) - more are left dry

struct WaterMeshData{
    std::vector<vertex> flat;
    std::vector<vertex> sheets;
    //Every sheet drawn: the falls, then the springs, in the order UpdateMist numbers their foam.
    std::vector<WaterSheetShape> shapes;
    int brinks = 0;
    int sheets_pushed = 0;      //sheets a column stood in the way of, sent further out

    /*
        POOLS, the swamp's standing water (biomes_plan.md step 4): every plateau cell of the swamp whose
        ground dips under TERRAIN_WATER_Y gets a whole flat cell of water there, and the hummocks cut
        through it as a river's banks do. The same shader as the rivers, slower (its own program, for
        its own flow speed); uv.x a gentle wave inside +-0.5, so no river-edge darkening, and uv.y the
        world's z - the streaks just drift.
    */
    std::vector<vertex> pools;
    float build_ms = 0.0f;
};

void BuildWaterMesh(const Grid& g, const Terrain& t, WaterMeshData& out);

#endif

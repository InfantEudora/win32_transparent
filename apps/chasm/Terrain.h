#ifndef _CHASM_TERRAIN_H_
#define _CHASM_TERRAIN_H_

#include <stdint.h>
#include <vector>
#include "Grid.h"

/*
    THE TERRAIN LEVELS: which of a handful of heights every fine vertex stands at.

    grid_plan.md section 4. A level is not a height field - buildable land is flat within a level -
    and a level step is not a storey: the chasm wall is many tens of them. Levels come from the
    feature lines (section 3): the chasm is the region the rim line encloses together with the
    map's south edge, a shard is the region inside its closed line.

    A vertex ON a line belongs to the higher side - the plateau for the rim, the shard for a shard's
    outline - so the drop starts half a cell outside the pinned chain (section 3, "Where the edge
    actually shows"), which is what makes the cliff top as smooth as the chain is.

    Derived from the grid and its features alone, so like the grid it is never saved, and it is
    immutable once built.
*/

#define TERRAIN_PLATEAU     0
#define TERRAIN_SHARD       1
#define TERRAIN_FLOOR       2
#define TERRAIN_NUM_LEVELS  3

struct TerrainLevel{
    const char* name;
    float height;       //world y
};
extern const TerrainLevel terrain_levels[TERRAIN_NUM_LEVELS];

/*
    RIVERS (grid_plan.md step 9). A river is not a level: it is a channel pressed into the plateau's
    ground along a smoothed line, deepest along the middle and rising to nothing a bank's width
    past the water's edge - and the water is a flat surface at TERRAIN_WATER_Y that the ground cuts
    through, so the shore is wherever the two cross. Because the channel is a function of position
    alone, cells on either side of an edge lower it identically (no cracks), and where a river
    reaches the rim the wall's top edge is lowered with it: the notch the fall pours through comes
    for free.

    Nothing stands in or beside a river: a vertex within the bank plus a margin is WET, and a wet
    vertex is no one's to build on or grow on (Zones.cpp, Forest.cpp). The margin is wider than a
    plot, so anything that does stand somewhere sees no channel under it at all - which is why
    TerrainGroundHeight, the ground for things that stand, can ignore rivers.
*/
#define TERRAIN_WATER_Y         -0.5f   //the rivers' surface, against the plateau at 0
#define TERRAIN_RIVER_DEPTH     1.15f   //how far the middle of a channel is lowered
#define TERRAIN_RIVER_BANK      1.5f    //the channel rises over this much either side of the water's edge
#define TERRAIN_WET_MARGIN      3.0f    //past the bank, still too close to build or grow

struct TerrainRiver{
    std::vector<vec2> points;   //world, smoothed, from the source; the last runs past the rim
    float width = 6.0f;         //across the water, world units
    float length = 0.0f;        //along `points`
};

//Where a river goes over the rim: its centre line crossing the rim's line.
struct TerrainFall{
    int river = -1;
    vec2 lip;                   //on the rim line
    vec2 out;                   //unit, level, away from the plateau into the chasm
    vec2 across;                //unit, along the rim
    float width = 0.0f;
    float along = 0.0f;         //distance from the river's source to the lip
};

class Terrain{
public:
    //`features` are in world coordinates, in GridSettings order: the rim first, then shards.
    void Build(const Grid& g, const std::vector<GridLine>& features);

    std::vector<uint8_t> level;     //per fine vertex
    float Height(int v) const { return terrain_levels[level[v]].height; }

    std::vector<TerrainRiver> rivers;
    std::vector<TerrainFall> falls;
    std::vector<uint8_t> wet;       //per fine vertex: too near a river to build or grow on
    //How far a river's channel lowers the ground at p - 0 away from every river.
    float RiverDip(const vec2& p) const;
    /*
        The river nearest p: which one, how far along it from its source, and how far across from
        its centre line as a share of half its width (negative on its left). False if none is within
        `reach` of p. For laying the water out - a few thousand calls, not one per ground vertex.
    */
    bool RiverCoords(const vec2& p, float reach, int& river, float& along, float& across) const;

    int level_count[TERRAIN_NUM_LEVELS] = {};
    int wet_count = 0;
    float build_ms = 0.0f;

private:
    /*
        Every point's distance past its nearest river's water edge (negative in the water), on a
        square raster over the map and read bilinearly, so RiverDip is continuous and cheap. Far
        from every river it holds a large value.
    */
    vec2 edge_origin;
    float edge_cell = 1.0f;
    int edge_w = 0;
    int edge_h = 0;
    std::vector<float> edge;
    float EdgeDistance(const vec2& p) const;
    void BuildRivers(const Grid& g, const std::vector<GridLine>& features);
};

/*
    The features in world coordinates, from the grid when it carries them (Grid::lines past the
    four outline lines) and otherwise the default chasm mapped onto the grid's bounds - which is
    what an unpinned grid gets, until the grid side pins them itself (grid_plan.md step 3).
*/
std::vector<GridLine> TerrainFeatureLines(const Grid& g);

//The rivers, their centre lines in 0..1 map coordinates and before smoothing - fixed, like the
//default chasm, until the map is generated from more than a seed.
std::vector<TerrainRiver> ChasmDefaultRivers();

#ifdef DEBUG
//The terrain's own checks, added to the grid's report (README.md, "Checks live in the app").
void RunTerrainChecks(const Grid& g, const Terrain& t, const std::vector<GridLine>& features,
                      GridCheckReport& report);
#endif

#endif

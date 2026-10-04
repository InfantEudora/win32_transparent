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

class Terrain{
public:
    //`features` are in world coordinates, in GridSettings order: the rim first, then shards.
    void Build(const Grid& g, const std::vector<GridLine>& features);

    std::vector<uint8_t> level;     //per fine vertex
    float Height(int v) const { return terrain_levels[level[v]].height; }

    int level_count[TERRAIN_NUM_LEVELS] = {};
    float build_ms = 0.0f;
};

/*
    The features in world coordinates, from the grid when it carries them (Grid::lines past the
    four outline lines) and otherwise the default chasm mapped onto the grid's bounds - which is
    what an unpinned grid gets, until the grid side pins them itself (grid_plan.md step 3).
*/
std::vector<GridLine> TerrainFeatureLines(const Grid& g);

#ifdef DEBUG
//The terrain's own checks, added to the grid's report (README.md, "Checks live in the app").
void RunTerrainChecks(const Grid& g, const Terrain& t, const std::vector<GridLine>& features,
                      GridCheckReport& report);
#endif

#endif

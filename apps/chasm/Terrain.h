#ifndef _CHASM_TERRAIN_H_
#define _CHASM_TERRAIN_H_

#include <stdint.h>
#include <vector>
#include "Grid.h"

/*
    THE TERRAIN LEVELS: which of a handful of heights every fine vertex stands at.

    grid_plan.md section 4. A level is not a height field - buildable land is flat within a level -
    and a level step is not a storey: the chasm wall is many tens of them. Levels come from the
    feature lines of the seed's layout (section 3, step 10): the chasm is what its rims bound
    together with the map's edges; an ISLAND (shard, column, ledge) is the region inside its closed
    line; a BALCONY is the region between its line and the stretch of rim it leaves and rejoins
    (GridFeature::region), a step below the rim.

    A vertex ON a line belongs to the higher side - the plateau for a rim, the island or balcony for
    its own line - so the drop starts half a cell outside the pinned chain (section 3, "Where the
    edge actually shows"), which is what makes the cliff top as smooth as the chain is. The fixed
    vertices where a balcony meets its rim are the rim's, plateau.

    Levels are numbered in the order they were added, not by height - never renumber, saves and
    picks name them by value; terrain_levels has each one's height.

    Derived from the grid and its features alone, so like the grid it is never saved, and it is
    immutable once built.
*/

#define TERRAIN_PLATEAU     0
#define TERRAIN_ISLAND      1       //shards, columns, ledges: reached by zeppelin
#define TERRAIN_FLOOR       2       //under the mist, never built on
#define TERRAIN_BALCONY     3       //on a wall a step below the rim: reached by winch
#define TERRAIN_NUM_LEVELS  4

/*
    BIOMES (biomes_plan.md), per fine vertex. Step 1 has two: the north MOUNTAIN, which closes the chasm
    off from the map's north edge and is impassable to everything and buildable by nothing, and
    TEMPERATE, everything else. Never renumber - a biome will pick palette rows and rules by value.
*/
#define TERRAIN_BIOME_TEMPERATE 0
#define TERRAIN_BIOME_MOUNTAIN  1
#define TERRAIN_BIOME_POCKET    2       //step 3: a meadow in the mountain and its valley - open, buildable
#define TERRAIN_BIOME_SWAMP     3       //step 4: lowland in the south, pools between hummocks
#define TERRAIN_BIOME_DESERT    4       //step 5: dunes in the south, on the swamp's other side
#define TERRAIN_NUM_BIOMES      5
const char* TerrainBiomeName(int biome);

struct ChasmLayout;
/*
    The biome at p on `level`, from the layout alone - the one rule, so the vertices' biomes (Terrain::
    biome) and the drawn ground's colours (TerrainMesh) agree. `dither` is added to the south regions'
    masks before they are tested at a half: 0 for the rules, a little noise for colour, so an edge drawn
    triangle by triangle is ragged rather than a staircase of cells.
*/
int TerrainBiomeAt(const ChasmLayout& layout, const vec2& p, int level, float dither = 0.0f);

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
    Terrain::GroundHeight, the ground for things that stand, can ignore rivers.
*/
#define TERRAIN_WATER_Y         -0.5f   //the rivers' surface, against the plateau at 0
#define TERRAIN_RIVER_DEPTH     1.15f   //how far the middle of a channel is lowered
#define TERRAIN_RIVER_BANK      1.5f    //the channel rises over this much either side of the water's edge
#define TERRAIN_WET_MARGIN      3.0f    //past the bank, still too close to build or grow

//The relief (step 2, BuildRelief). World units.
#define RELIEF_HILL_HEIGHT      14.0f   //the most a hill stands above the plateau
#define RELIEF_HILL_WAVELENGTH  110.0f  //hill to hill
#define RELIEF_HILL_REGION      320.0f  //the size of a rolling region, and of a flat one
#define RELIEF_MOUNTAIN_RISE    70.0f   //from the foot inward, over which the mountain climbs to its full height
#define RELIEF_MOUNTAIN_BASE    10.0f   //the mountain's floor above the plateau, past its rise
#define RELIEF_PEAK_HEIGHT      42.0f   //the crags on top of that
#define RELIEF_PEAK_WAVELENGTH  80.0f
#define RELIEF_RIVER_FADE       24.0f   //past a river's wet margin, over which the relief comes back
#define RELIEF_POCKET_EDGE      7.0f    //past a pocket's edge, over which the crags rise from its floor
//The swamp (step 4): its ground about the rivers' water level, so that half of it stands just out.
//Hummocks steep enough that a pool deepens fast from its shore: the water shader foams wherever the
//ground is within 0.16 under the surface, and gentle ones made a pool mostly foam.
#define SWAMP_FLOOR             -0.25f  //the mean, against TERRAIN_WATER_Y at -0.5: about 40% of the swamp wet
#define SWAMP_HUMMOCK           1.20f   //how far hummocks rise and pools sink either side of it
#define SWAMP_HUMMOCK_SIZE      15.0f   //hummock to hummock
#define SWAMP_SHORE             0.20f   //ground this little above the water is still too wet to use
//The desert (step 5): dunes, gentle enough to build between.
#define DUNE_BASE               0.4f
#define DUNE_HEIGHT             3.2f
#define DUNE_WAVELENGTH         34.0f
//Snow (step 5): above this, gentle mountain ground is snow; below it, scree. Wanders on noise.
#define SNOW_LINE               12.0f
#define SNOW_LINE_SWING         5.0f
#define SNOW_LINE_WAVELENGTH    70.0f

struct TerrainRiver{
    std::vector<vec2> points;   //world, smoothed, from the source; the last runs past the rim
    float width = 6.0f;         //across the water, world units: the layout's, the river's usual width
    float length = 0.0f;        //along `points`
    /*
        Half the water's width at each point (2026-10-06): the usual width wandering a little either
        way along the course, and swelling into a LAKE or two on the longer rivers - only where the
        rims and the map's edge leave room, and back to `width` toward the source and the fall.
        Everything that knew the water's edge from `width` reads this instead (Terrain::BuildRivers).
    */
    std::vector<float> half;
    int lakes = 0;
    float HalfAt(size_t seg, float t) const{
        return half[seg] + (half[seg + 1] - half[seg]) * t;
    }
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
    //`features` are TerrainFeatureLines(g): world coordinates, in the layout's order.
    void Build(const Grid& g, const std::vector<GridLine>& features);

    std::vector<uint8_t> level;     //per fine vertex
    float Height(int v) const { return terrain_levels[level[v]].height; }
    std::vector<uint8_t> biome;     //per fine vertex, TERRAIN_BIOME_*
    bool Mountain(int v) const { return biome[v] == TERRAIN_BIOME_MOUNTAIN; }
    int biome_count[TERRAIN_NUM_BIOMES] = {};

    /*
        THE RELIEF (biomes_plan.md step 2): height on top of the levels, a smooth function of position
        alone - rugged peaks rising from the mountain's foot, soft hills in regions of the plateau, and
        nothing along a river, whose flat water needs flat ground under it. Only the PLATEAU gets it:
        the chasm's floor and shards stay flat. Read from a raster bilinearly, so the cells on either
        side of an edge agree.

        GroundHeight is THE ground for anything that stands or is drawn on it: the level, its small
        bump (TerrainGroundHeight) and, on the plateau, the relief. Not the river channel, which only
        the terrain mesh cuts (and nothing stands near a river - see wet).
    */
    float Relief(const vec2& p) const;
    float GroundHeight(const vec2& p, float level_height) const;
    //Per fine vertex: GroundHeight at it, on its own level - for the slope rules, read often.
    std::vector<float> ground;
    float relief_max = 0.0f;
    int swamp_pool_count = 0;       //swamp vertices wet by its pools
    //The height above which gentle mountain ground is snow (step 5).
    float SnowLine(const vec2& p) const;
    uint32_t relief_seed = 0;

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

    vec2 relief_origin;
    float relief_cell = 2.0f;
    int relief_w = 0;
    int relief_h = 0;
    std::vector<float> relief;
    void BuildRelief(const Grid& g);
};

/*
    The features in world coordinates: Grid::lines past the four outline lines, in the layout's
    order (Grid::LineKind says what each one is).
*/
std::vector<GridLine> TerrainFeatureLines(const Grid& g);

//The level a feature line's own vertices stand on, its high side: plateau for a rim, shard level
//for everything closed that stands in the chasm.
uint8_t TerrainLevelOfKind(int kind);

#ifdef DEBUG
//The terrain's own checks, added to the grid's report (README.md, "Checks live in the app").
void RunTerrainChecks(const Grid& g, const Terrain& t, const std::vector<GridLine>& features,
                      GridCheckReport& report);
#endif

#endif

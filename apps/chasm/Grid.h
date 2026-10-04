#ifndef _CHASM_GRID_H_
#define _CHASM_GRID_H_

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "type_vec2.h"

/*
    THE GROUND EVERYTHING IS BUILT ON: a Townscaper-style irregular quad grid, at two levels.

    Made the way Oskar Stalberg made Townscaper's - see docs/grid_plan.md section 1 for the steps
    and why each is there. In short: a triangle lattice, random neighbouring triangles merged into
    quads, everything split into quads (the COARSE level), relaxed toward squares, split once more
    (the FINE level) and relaxed again.

    --- COORDINATES ------------------------------------------------------------------------------
    A grid is flat, so positions are vec2 and **vec2::y IS WORLD Z**. Height belongs to terrain
    (step 3), not to the grid. Every polygon is wound with positive signed area in (x, z), which is
    what the relaxation's +90 degree rotation is written against; see GridSquareFit.

    --- THE TWO LEVELS ARE ONE VERTEX SPACE --------------------------------------------------------
    Fine vertex i IS coarse vertex i for every i < coarse.pos.size(): the fine level is built by
    appending midpoints and centroids to a copy of the coarse positions. So a coarse corner never
    needs translating, and after the fine relaxation the coarse positions are simply copied back.
    A coarse quad's SHAPE is the union of its four fine children (GridQuad::parent), not the
    four-corner polygon its corners would make: the fine relaxation bends coarse edges, and nothing
    holds them straight, deliberately.

    Everything here is decided by GridSettings and nothing else, so a grid is never saved - the
    seed is (README.md, "The world is a seed plus edits").
*/

/*
    A LINE THAT VERTICES SLIDE ALONG while the grid relaxes - grid_plan.md section 3. A vertex
    pinned to one stays on it but moves along it freely, so the vertices space themselves out and
    the quads beside them square up against it. The map's four straight edges are lines, and so is
    every feature pinned into the grid: the chasm rim, the shards.

    An open polyline. A closed one repeats its first point at the end.
*/
struct GridLine{
    std::vector<vec2> points;
};

#define GRID_PIN_FREE   -1      //relaxes freely
#define GRID_PIN_FIXED  -2      //never moves: where lines meet, like the map's corners

//The chasm map's features, in GridSettings::features' normalised coordinates: the rim (open,
//both ends on the v = 1 edge) and one shard (closed).
std::vector<GridLine> ChasmDefaultFeatures();

struct GridSettings{
    uint32_t seed = 1;
    //How many FINE cells to aim for. The lattice is sized from it, so the count that comes out is
    //close but not exact - the merge decides how many quads each triangle becomes.
    int target_fine_cells = 100000;
    //Map width (x) over depth (z).
    float aspect = 16.0f / 9.0f;
    //Lattice triangle side, in world units. A fine cell comes out about a quarter of this across.
    float triangle_side = 8.0f;
    int relax_passes_coarse = 40;
    int relax_passes_fine = 40;
    //How far toward its best-fit square a vertex moves per pass, 0..1.
    float relax_strength = 0.5f;
    /*
        Lines pinned into the grid, as drawn: corners of a polyline, smoothed when generated. In
        NORMALISED map coordinates - u = 0..1 left to right (x), v = 0..1 from the -z edge to the
        +z edge - so they keep their place when the map's size changes. An open feature whose end
        lies on the map's edge is fixed there.
    */
    std::vector<GridLine> features = ChasmDefaultFeatures();

    bool operator==(const GridSettings& o) const;
};

struct GridQuad{
    int v[4] = {-1,-1,-1,-1};   //corners, positively wound in (x, z)
    int parent = -1;            //fine: the coarse quad it was split from. coarse: -1
};

struct GridLevel{
    std::vector<vec2> pos;                      //x = world x, y = world z
    std::vector<GridQuad> quads;
    std::vector<std::pair<int,int>> edges;      //each once, smaller index first, sorted
    std::vector<uint8_t> valence;               //per vertex: how many edges meet there
    std::vector<uint8_t> f_boundary;            //per vertex: on the map's outline
    std::vector<int> pin;                       //per vertex: GRID_PIN_*, or the Grid::lines index it slides along
    int num_bad_edges = 0;                      //edges shared by more than two quads - never valid
};

class Grid{
public:
    //Builds the whole grid from `s`. Deterministic: the same settings give the same bits, which
    //Hash() is there to prove.
    void Generate(const GridSettings& s);

    GridSettings settings;
    GridLevel coarse;
    GridLevel fine;
    /*
        What GridLevel::pin indexes, shared by both levels, in world coordinates. Lines 0-3 are the
        map's -x, +x, -z and +z edges; feature i of the settings is line feature_line_base + i,
        smoothed. A feature's chain is the edges whose two ends are both on its line - pinned to
        it, or the fixed vertex at an end of it that lies on the map's edge.
    */
    std::vector<GridLine> lines;
    int feature_line_base = 4;

    int num_lattice_triangles = 0;
    int num_leftover_triangles = 0;     //triangles the merge found no partner for
    int num_unfinished_features = 0;    //features whose chain could not be completed (the check says where)
    vec2 bounds_min, bounds_max;
    float generate_ms = 0.0f;

    //Topology and the fine positions' exact bits. Equal hashes mean the same grid.
    uint64_t Hash() const;

    //The fine quad's centroid, for markers and for jumping the camera to it.
    vec2 FineQuadCentre(int q) const;
};

//How square a quad is: 1 for a perfect square, toward 0 as it skews or degenerates. One minus the
//relative distance from its corners to their best-fit square.
float GridQuadSquareness(const vec2 p[4]);

//The best-fit square that the relaxation pulls a quad toward: target corners for p[0..3].
void GridSquareFit(const vec2 p[4], vec2 target[4]);

#ifdef DEBUG
/*
    THE GRID'S CHECKS - debug builds only, run as a command in the app (docs/README.md, "Checks
    live in the app"). Every failure carries a position, so the debug view can mark it and the
    camera can be sent to it.
*/
struct GridIssue{
    std::string kind;       //"valence", "folded", "edge", "outline", "feature", "squareness", "feature_squareness"
    int index = -1;         //the fine vertex or quad it is about
    vec2 where;
    bool f_failure = true;  //false: worth looking at, not wrong (the worst-squareness quads)
};

struct GridCheckResult{
    std::string name;
    bool f_pass = true;
    bool f_skipped = false;     //nothing to check against - says why in `detail`, does not fail
    std::string detail;
};

//How square the fine quads touching one feature's chain are.
struct GridChainFigures{
    int line = -1;              //Grid::lines index
    int vertices = 0;           //fine vertices on the chain
    int quads = 0;
    float squareness_mean = 0.0f;
    float squareness_min = 0.0f;
};

struct GridCheckReport{
    bool f_pass = true;
    std::vector<GridCheckResult> results;
    std::vector<GridIssue> issues;
    uint64_t hash = 0;
    float squareness_mean = 0.0f;
    float squareness_min = 0.0f;
    //The quads with no pinned corner - what the grid does when nothing is holding it.
    float squareness_interior_mean = 0.0f;
    float squareness_interior_min = 0.0f;
    std::vector<GridChainFigures> chains;
    int valence_hist[8] = {};   //interior fine vertices by valence, 7 = seven or more
    float check_ms = 0.0f;
};

//Runs every check on `grid`. Regenerates it once from the same settings to prove determinism,
//so it costs about one more Generate.
GridCheckReport RunGridChecks(const Grid& grid);
#endif

#endif

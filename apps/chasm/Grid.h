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

/*
    THE CHASM'S LAYOUT, generated from the seed (grid_plan.md, "Step 10, generated chasm"): which
    lines are pinned into the grid and what each one is, and where the rivers run. World
    coordinates, already smoothed - the grid pins exactly these lines, and the terrain reads its
    levels off them by kind.

    A RIM is the edge of a rift: plateau on one side, floor on the other. It is open when the rift
    reaches the map's edge (both ends exactly on it, fixed there) and closed when it does not. No
    rift reaches the north (-z) edge - the frozen end's room - which is what lets the terrain decide
    floor or plateau by a ray to the north. Shards, columns and terraces are closed lines standing
    in a rift at shard level; they differ in shape only.
*/
#define GRID_FEATURE_RIM        0
#define GRID_FEATURE_SHARD      1
#define GRID_FEATURE_COLUMN     2
#define GRID_FEATURE_TERRACE    3
#define GRID_FEATURE_KINDS      4
extern const char* const grid_feature_names[GRID_FEATURE_KINDS];

/*
    How close two feature lines may come, in lattice sides - two of them, a line and itself across
    a narrow place, or a line and the map's edge away from its ends. Pinning needs about two (no
    two chains may share a lattice triangle), and a level change sits half a fine cell outside its
    chain, so at this spacing no fine cell can see three levels, which the terrain mesh does not cut.
*/
#define GRID_FEATURE_SPACING    2.5f

struct GridFeature{
    int kind = GRID_FEATURE_RIM;
    GridLine line;
    bool f_end_on_outline[2] = {false,false};   //an open line's ends, on the map's edge
};

struct GridRiverLine{
    std::vector<vec2> points;   //from the source on the map's edge; the last is a little past its rim
    float width = 6.0f;
};

struct ChasmLayout{
    std::vector<GridFeature> features;      //rims first, then terraces, shards, columns
    std::vector<GridRiverLine> rivers;
    //What the seed asked for and what came of it - the generator drops what will not fit.
    int rifts = 0;
    int forks = 0;
    int count[GRID_FEATURE_KINDS] = {};
    int wanted[GRID_FEATURE_KINDS] = {};    //rims: unused
    int rivers_wanted = 0;
    int attempts = 0;                       //rift layouts drawn until one kept its spacing
    bool f_rifts_ok = true;                 //false: every attempt failed, the last is used anyway
    /*
        THE NORTH MOUNTAIN (biomes_plan.md step 1): its foot, the z the mountain comes down to at each
        x, west to east every few units - north of it is mountain. Shallow on noise, and reaching south
        in a tongue past the main rift's tip, which is what seals the two sides of the chasm from each
        other: the only way round the main rift is across the line from its tip to the north edge, and
        the tongue covers it. Drawn last, from a stream of its own, so it moves nothing else.
    */
    std::vector<vec2> mountain_foot;
    /*
        MOUNTAIN POCKETS (biomes_plan.md step 3): small flat meadows inside the mountain, each in a spur
        the foot bulges out round it, entered by one valley through the spur's front - so a pocket is
        reachable from the side of the chasm it stands on and from nowhere else. Mainly for resources.
    */
    struct Pocket{
        vec2 centre;
        float radius = 16.0f;               //the meadow's, before its edge's wobble
        std::vector<vec2> valley;           //its centre line, from the meadow's edge out past the foot
        float floor = 6.0f;                 //the meadow's height above the plateau
        int side = 0;                       //-1 west of the main rift, +1 east
    };
    std::vector<Pocket> pockets;
    int pockets_wanted = 0;
    //Why tries were turned down: no room, a rim, a river, another pocket, too long a valley.
    int pocket_rejects[5] = {};
    vec2 main_tip;                          //the main rift's spine's end
    float main_mouth_x = 0.0f;              //where the main rift leaves the south edge
    float generate_ms = 0.0f;
};

//The mountain's foot at x: the z north of which is mountain. -infinity with no foot.
float ChasmMountainFootAt(const ChasmLayout& layout, float x);

/*
    Where p stands against pocket `k`: its distance outside the pocket (meadow or valley; negative
    inside), and along the valley from the meadow's edge (0) to its mouth (1) - 0 in the meadow.
*/
#define CHASM_VALLEY_HALF_WIDTH 5.0f
float ChasmPocketDistance(const ChasmLayout::Pocket& k, const vec2& p, float* along = nullptr);

//The layout for a seed on the map rectangle lo..hi (x, z), lattice side `side`. Deterministic, and
//draws from its own RRandom, never the grid's or the simulation's.
ChasmLayout GenerateChasmLayout(uint32_t seed, const vec2& lo, const vec2& hi, float side);

/*
    Corners made into a smooth line: centripetal Catmull-Rom, resampled every `spacing`. An open
    line keeps its end points exactly; a closed one repeats its first point at the end.
*/
GridLine GridSmoothLine(const std::vector<vec2>& corners, bool f_closed, float spacing);

/*
    The closest two lines come: every point of `a` against every segment of `b`. When they are the
    same line, pairs closer than `skip_arc` along it are not counted - a line is always close to
    itself nearby. `where` gets the point of `a` at the closest place.
*/
float GridLineGap(const GridLine& a, const GridLine& b, float skip_arc, vec2* where);

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
    //The chasm's features are not settings: they follow from the seed (ChasmLayout).

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
        map's -x, +x, -z and +z edges; layout feature i is line feature_line_base + i. A feature's
        chain is the edges whose two ends are both on its line - pinned to it, or the fixed vertex
        at an end of it that lies on the map's edge.
    */
    std::vector<GridLine> lines;
    int feature_line_base = 4;
    ChasmLayout layout;
    //The kind of what line l is, GRID_FEATURE_*, or -1 for the outline.
    int LineKind(int l) const{
        int f = l - feature_line_base;
        return (f >= 0 && f < (int)layout.features.size()) ? layout.features[f].kind : -1;
    }

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

#ifndef _ARCHER_WIND_H_
#define _ARCHER_WIND_H_

#include "Stage.h"

#include <stdint.h>
#include <vector>

/*
    The wind over the level - a 2D velocity field in the play plane, built from the blockout so it
    always matches it. See docs/wind_plan.md for the reasoning; this is what a reader of the code needs.

    Engine-free for the reasons Foliage and Stage are: `make rules` can test it, and when the
    balance mechanic wants wind as an input the field can move into Stage as it is.

    --- A STREAM FUNCTION ------------------------------------------------------------------------
    The velocity is the curl of a scalar psi: u = dpsi/dy, v = -dpsi/dx. Whatever psi is, the flow
    has no sources or sinks - leaves carried by it never bunch up or thin out - and a surface is
    just a line psi is constant along, which flow cannot cross. Four layers make psi:

      1. MEAN FLOW   potential flow around the blocks, solved once per level (Laplace, SOR) for a
                     wind of 1 and scaled by `speed`. Faster over a hilltop, still in a corner.
      2. EDDIES      in the lee of each downwind top corner: one held in place (the bubble that
                     turns the flow back along the ground) and two shed, half a cycle apart,
                     each swelling, drifting and fading downstream.
      3. WAVES       a few slow sine waves drifting with the wind - gentle turbulence.
      4. GUSTS       fronts travelling along x, multiplying the VELOCITY as they pass.

    2 and 3 are multiplied by a ramp of the distance to the nearest block, zero at a surface
    (Bridson's curl-noise trick), so psi stays constant on every surface whatever they do. 4
    scales the velocity rather than psi: a velocity scaled by a scalar is still tangent to a wall.

    --- A PURE FUNCTION OF THE TICK --------------------------------------------------------------
    Nothing here has state beyond what Build computed. The eddies' phases and the gust schedule are
    worked out from the tick (and a hash of the corner or gust index), so the same level at the
    same tick blows the same way - a restart, a replay, a test. Velocity() is const and safe to
    call from any thread, as long as nothing calls Build() at the same time. The app never does:
    it builds a FRESH field on the background worker and publishes it whole (UpdateWind).

    --- WHAT BLOCKS THE WIND ----------------------------------------------------------------------
    SOLID, LEDGE and live BREAKABLE blocks, invisible ones included (they stand for scenery). NOT
    one-way platforms and tree arms: thin, and wind through them reads fine. The level's two ends
    are EXTRUDED outward - the field past the last block sees the last block's profile - so the
    wind arrives level with the ground rather than dropping into the empty margin and back out.
*/

struct WindParams{
    //--- Needs a rebuild -------------------------------------------------------------------------
    float cell          = 0.5f;     //grid spacing
    float margin_x      = 6.0f;     //domain beyond the outermost blocks, each side
    //The flow is solved under a lid this high above the highest block top. The lid holds the
    //total flux fixed, so it forces the flow faster over high ground: too low and every hill is a
    //jet. 1.5 x the level's relief is about where the speed-up stops changing.
    float lid_above     = 24.0f;
    float wall_ramp     = 1.0f;     //eddies and waves fade to nothing over this distance to a wall
    //A corner sheds eddies only if the ground drops at least this far downwind of it; the eddy is
    //sized by the drop, capped at eddy_max_drop so a cliff does not get a vortex the size of a
    //house.
    float eddy_min_drop = 1.0f;
    float eddy_max_drop = 4.0f;

    //--- Free to change at any time --------------------------------------------------------------
    float speed         = 0.5f;     //mean wind in units/s. SIGNED: + blows toward +x
    //Peak swirl speed of a corner's bound eddy, as a fraction of |speed| (the shed ones are half
    //that). About 1 is what it takes to turn the flow back along the ground in the lee; much
    //above 1.5 the lee blows back hard enough to look like a fan.
    float eddy_strength = 1.0f;
    //Shedding rate, as the Strouhal number: one cycle every drop / (strouhal * |speed|) seconds.
    //0.2 is the textbook value for a bluff body.
    float eddy_strouhal = 0.2f;
    float wave_strength = 0.12f;    //turbulence, as a fraction of |speed|
    float wave_length   = 10.0f;    //longest wave, in units
    float gust_strength = 0.6f;     //a gust's peak extra, as a fraction of the field
    float gust_width    = 14.0f;    //length of a gust along x
    int   gust_period   = 480;      //ticks between gusts, on average

    //The free half alone - what decides whether a built field needs retuning rather than rebuilding.
    bool SameFree(const WindParams& o) const {
        return (speed == o.speed) && (eddy_strength == o.eddy_strength) && (eddy_strouhal == o.eddy_strouhal) &&
               (wave_strength == o.wave_strength) && (wave_length == o.wave_length) &&
               (gust_strength == o.gust_strength) && (gust_width == o.gust_width) && (gust_period == o.gust_period);
    }
};

//The turbulence: this many travelling sine waves in psi - see WindField::Waves.
#define WIND_WAVES              3

struct WindWave{
    float kx = 0.0f, ky = 0.0f;     //wave vector
    float amp = 0.0f;               //psi amplitude
    float offset = 0.0f;            //phase at the origin, drift and evolution included
};

//Per corner: one bound eddy held in the lee, and two shed downstream - see CornerEddies.
#define WIND_EDDIES_PER_CORNER  3

struct WindVec{
    float x = 0.0f;
    float y = 0.0f;
};

//A top corner on the downwind side of a block that the ground falls away behind.
struct WindCorner{
    float x = 0.0f;
    float y = 0.0f;
    //How far the ground falls just downwind, or the exposed downwind face if that is shorter (a
    //floating block), capped at eddy_max_drop.
    float drop = 0.0f;
    float seed = 0.0f;              //0..1, offsets this corner's shedding so corners do not beat in step
};

//One vortex blob at a given tick - what the debug view marks and the tests look at.
struct WindEddy{
    float x = 0.0f;
    float y = 0.0f;
    float radius = 1.0f;            //the Gaussian's sigma, vertically
    float stretch = 1.0f;           //and horizontally it is radius * stretch - a lee bubble is long and flat
    float strength = 0.0f;          //psi amplitude, signed; 0 at the ends of its life
};

struct WindStats{
    int   nx = 0, ny = 0;
    int   obstacles = 0;
    int   end_walls = 0;            //boundary walls left out - see Build
    int   components = 0;           //connected solids; all but the ground get their own psi
    int   iterations = 0;
    float residual = 0.0f;          //the last sweep's largest change, relative to the lid's psi
    float build_ms = 0.0f;
    int   corners = 0;              //for the current wind direction
};

/*
    What the wind is built from: the level's blocks, and every STILL-AIR biome (Stage.h) as one
    more solid box. So the wind goes over and round the cave instead of through it, and everything
    that steers by the field's distance - leaves, streaks, fireflies - stays out of it, as they
    stay out of rock. The rules never see these boxes; only the wind does.
*/
std::vector<StageBlock> WindBlocks(const std::vector<StageBlock>& blocks, const std::vector<StageBiome>& biomes);

class WindField{
public:
    /*
        Builds the grid, the distance field, the mean flow and the corners. Returns false and does
        nothing when the obstacles and the geometry params are the same as last time - a restart
        of the same level costs nothing. The free params are always taken.
    */
    bool Build(const std::vector<StageBlock>& blocks, const WindParams& params);
    bool IsBuilt() const { return nx > 0; }
    /*
        What Build compares to decide there is nothing to do - the obstacles and the rebuild
        params - without building anything. BuiltKey is the one the field was built for (0 before
        the first Build), so KeyFor(blocks,params) != BuiltKey() means "these blocks need a build".
    */
    static uint64_t KeyFor(const std::vector<StageBlock>& blocks, const WindParams& params);
    uint64_t BuiltKey() const { return built_hash; }

    //The free params only. Changing a rebuild param here is ignored; call Build.
    void SetParams(const WindParams& params);
    const WindParams& Params() const { return params; }
    const WindStats& Stats() const { return stats; }

    //The wind at a point, all four layers.
    WindVec Velocity(float x, float y, int64_t tick) const;
    //Layer 1 alone, at the current speed - the steady picture, for the debug view and tests.
    WindVec MeanFlow(float x, float y) const;
    /*
        The whole field on a w x h grid from (x0,y0), `step` apart, x and y interleaved row by row
        into `out` (2*w*h floats) - what Renderer::SetWindField takes. The same values Velocity()
        gives at those points, much cheaper per point.
    */
    void Bake(int64_t tick, float x0, float y0, float step, int w, int h, float* out) const;
    //Layer 4 alone: 1 where no gust is passing.
    float GustFactor(float x, int64_t tick) const;

    //Signed distance to the nearest obstacle, negative inside one. Bilinear off the grid.
    float Distance(float x, float y) const;
    //Its gradient - the way out of the nearest surface, about unit length. Bilinear too.
    WindVec DistanceGradient(float x, float y) const;

    //The corners shedding for the current wind direction.
    const std::vector<WindCorner>& Corners() const { return (params.speed >= 0.0f) ? corners_pos : corners_neg; }
    //Every live eddy at a tick.
    void Eddies(int64_t tick, std::vector<WindEddy>& out) const;

    /*
        A streamline of the field frozen at `tick`, from (x,y), `step` units per point, until it
        leaves the domain, runs into a block, stalls, or has `max_points`. Appends x,y pairs.
        Frozen, so an eddy draws as the closed loop it is at that instant.
    */
    void TraceStreamline(float x, float y, int64_t tick, float step, int max_points, std::vector<float>& out) const;

    float MinX() const { return x0; }
    float MinY() const { return y0; }
    float MaxX() const { return x0 + (nx - 1) * cell; }
    float MaxY() const { return y0 + (ny - 1) * cell; }

private:
    WindParams params;
    WindStats  stats;
    uint64_t   built_hash = 0;

    int   nx = 0, ny = 0;
    float cell = 0.5f;
    float x0 = 0.0f, y0 = 0.0f;
    //The x range the blocks cover - outside it the level is extruded.
    float level_x0 = 0.0f, level_x1 = 0.0f;

    std::vector<uint8_t> solid;     //per node
    std::vector<float> dist;        //signed distance to the nearest obstacle, per node
    std::vector<float> dist_dx, dist_dy;
    std::vector<float> psi;         //mean flow's psi for a wind of 1
    std::vector<float> mean_u, mean_v;

    std::vector<WindCorner> corners_pos, corners_neg;

    int Index(int i, int j) const { return j * nx + i; }
    //Bilinear lookup of a per-node array; clamps to the domain.
    float Sample(const std::vector<float>& a, float x, float y) const;

    void BuildDistance(const std::vector<StageBlock>& obstacles);
    void SolveMeanFlow();
    void FindCorners(int dir, std::vector<WindCorner>& out) const;

    //Layers 2 and 3 as a psi and its gradient, before the wall ramp.
    void DetailPsi(float x, float y, int64_t tick, float& p, float& px, float& py) const;
    //Layer 3's waves at a tick; returns how many (0 when there is no turbulence).
    int Waves(int64_t tick, WindWave out[WIND_WAVES]) const;
    //Layer 3 alone, ADDED to p, px, py.
    void WavePsi(float x, float y, int64_t tick, float& p, float& px, float& py) const;
    //The eddy blobs, for a corner at a tick.
    void CornerEddies(const WindCorner& c, int dir, int64_t tick, WindEddy out[WIND_EDDIES_PER_CORNER]) const;
};

#endif

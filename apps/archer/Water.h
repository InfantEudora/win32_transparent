#ifndef _ARCHER_WATER_H_
#define _ARCHER_WATER_H_

#include "Stage.h"
#include "Backdrop.h"
#include "TerrainField.h"
#include "type_vertex.h"

#include <stdint.h>
#include <vector>

/*
    The waterfall - apps/archer/docs/water_plan.md. Everything about it that is not GL: where its
    pieces go, the rocks of the pool it lands in, the surfaces the shader draws, and the foam.
    Engine-free like Backdrop and Leaves, so `make rules` checks it (water_test.cpp).

    --- THE PIECES, TOP TO BOTTOM -------------------------------------------------------------------
      - The NOTCH: Backdrop.cpp cuts the bank's wall down to lip_y over the fall and sets that
        stretch back by BackdropParams::notch_recess, so the water comes out of a cleft rather
        than off a flat face. It knows where from LayoutWater, the same as everything here.
      - The UPPER SHEET: from a little way back on the notch's top, over its edge, and down to
        under the pool's surface - leaving the edge the way water does, with its forward speed,
        so it comes further out from the rock the further it has fallen (fall_reach * sqrt(drop)).
      - The POOL: on a SHELF of rock standing in the gap behind the ground, with rims round it.
        The shelf's top is pool_depth under the surface, so the rims' fillets make its shore.
      - The SPILL: a second, short sheet, out through a gap in the front rim, down into the gap
        behind the ground.
      - The STREAM: a flat surface along that gap, from under the shelf to stream_x_end.
      - The FOAM: balls that rise from where each sheet lands, drift up and away, and shrink.

    --- WHY THE SHELF IS ITS OWN MESH -----------------------------------------------------------
    The bank's mesher rounds every block by 0.6 and pushes it about by up to 0.85 of noise, which
    is right for a wall seen from far off and wrong for a rim half a unit thick: the rims came out
    as blobs that met in the middle and the pool was gone. So the shelf and its rims are meshed
    on their own with the ground's own sharper settings (WaterRockParams), into an object of their
    own, standing into the bank rather than smooth-unioned with it.

    --- THE SURFACES AND THE SHADER ---------------------------------------------------------------
    Two meshes, because the shader draws two different things: the SHEETS (uv.x across -1..1,
    uv.y 0 at the top of a sheet to 1 at its foot) and the FLAT water (uv.x world x, uv.y how far
    the water has come from where it started - from the landing point in the pool, so the rings
    spread from it, and along the stream from the spill). Both are world space, like the terrain.

    Nothing flat is cut to its shore. A surface is a plane that runs INTO the rock on every side
    it has one, and the depth test makes the shoreline wherever the rock actually is - so a rim
    that the noise has pushed in a little makes a little bay, and nothing has to be measured.
    What does have to hold is that every edge of a plane IS inside rock (or under the grass),
    and water_test checks that against the blocks.

    --- DETERMINISTIC ---------------------------------------------------------------------------
    The foam draws no shared random number: every ball hashes its emitter and its spawn count,
    and time is in ticks. It is LOOKS ONLY - the app steps it from the render thread with the
    other view effects, and nothing in the simulation reads it.
*/

//The ground's sharper mesher settings for the shelf and rims - see WHY THE SHELF IS ITS OWN MESH.
TerrainParams WaterRockParams();

struct WaterParams{
    float pour_back    = 1.2f;      //the upper sheet starts this far back on the notch's top
    /*
        ...and this far over it, and goes over the edge this far out past the notch's nominal
        face. Both are the bank's, not the water's: the notch is several columns of one height,
        and the smooth union lifts a top by up to smooth_k / 4 where two such overlap; and its
        mesher rounds the edge outward by round_r and the grass cap's lip. At 0.04 and 0 the first
        unit of the fall was inside the rock, and seen from the island there was no water at the
        top of the fall at all.
    */
    float lip_lift     = 0.30f;
    float lip_out      = 0.80f;
    /*
        Where the upper sheet comes down: this far from the notch's face to the pool's front rim.
        The sheet's reach (how far out it has come after one unit of drop, * sqrt(drop)) is worked
        out from it, so a taller fall arcs less rather than sailing out over the rim - and the
        pool in front of where it lands is left to be seen.
    */
    float land_at      = 0.4f;
    float min_reach    = 0.5f;      //but at least this much, or it runs down the rock's face
    float bow          = 0.15f;     //how far the middle of a sheet stands out from its edges
    float spread       = 0.15f;     //a sheet's foot is this much wider than its top, as a fraction
    float pool_depth   = 0.35f;     //the shelf's top, under the pool's surface
    float rim_height   = 0.20f;     //the rims' tops, over it
    float rim_depth    = 0.50f;     //the rims' thickness
    float spill_hw     = 0.35f;     //the spill's half width
    float spill_reach  = 0.70f;     //fall_reach, for the spill
    float under        = 0.30f;     //how far a sheet carries on under the water it falls into
    float channel      = 1.0f;      //how much further back than a ridge's usual front the stream
                                    //sets the ridges along it
    float cell         = 0.25f;     //the flat surfaces' grid, in world units
    int   sheet_rows   = 28;        //down a sheet
    int   sheet_cols   = 6;         //across one
};

/*
    Where every piece goes, worked out once from the water, its ground block and the two param
    sets. Backdrop.cpp reads it to cut the notch; everything else here is built from it.
*/
struct WaterLayout{
    //The bank, as Backdrop builds it here.
    float wall_front   = 0.0f;      //the wall's nominal face
    float recess_front = 0.0f;      //the notch's, set back
    float notch_l = 0.0f, notch_r = 0.0f;   //wall columns whose core overlaps this are notched
    float clear_l = 0.0f, clear_r = 0.0f;   //ridges overlapping this are left out - the pool's
    //Ridges along the stream are kept, for the light and shadow they give the wall, but set back
    //to this, so the stream has a channel in front of them.
    float channel_l = 0.0f, channel_r = 0.0f;
    float channel_back = 0.0f;
    //The pool.
    float shelf_front  = 0.0f;      //the shelf's nominal front face
    float shelf_back   = 0.0f;      //into the notch
    float shelf_l = 0.0f, shelf_r = 0.0f;
    float pool_front   = 0.0f;      //the pool surface's front edge, inside the front rim
    float lip_z        = 0.0f;      //where the upper sheet goes over the edge
    float land_z       = 0.0f;      //where the upper sheet meets the pool
    float fall_reach   = 0.0f;      //and the reach that gets it there - see WaterParams::land_at
    //The spill and the stream.
    float spill_x      = 0.0f;
    float spill_land_z = 0.0f;      //where the spill meets the stream
    float stream_front = 0.0f;      //the stream's front edge, inside the ground
    float stream_back  = 0.0f;      //and its back one, inside the bank
    float stream_l = 0.0f, stream_r = 0.0f;
    float ground_bottom = 0.0f;
    float ground_top = 0.0f;
    float ground_back = 0.0f;
};

void LayoutWater(const StageWater& w, const StageBlock& ground, const BackdropParams& bank,
                 const WaterParams& p, WaterLayout& out);

//The water's ground: the ground block (Backdrop's sense - nothing under it, standing on nothing)
//whose span holds w.x. -1 if there is none, and then the water is not built at all.
int WaterGround(const StageWater& w, const std::vector<StageBlock>& blocks);

//The shelf and its rims, appended to `out`. Mesh them with WaterRockParams.
void BuildWaterRocks(const StageWater& w, const WaterLayout& l, const WaterParams& p,
                     std::vector<StageBlock>& out);

//The two sheets, and the pool and stream, appended. matid 0 throughout. See THE SURFACES.
void BuildWaterSheets(const StageWater& w, const WaterLayout& l, const WaterParams& p,
                      std::vector<vertex>& out);
void BuildWaterFlats(const StageWater& w, const WaterLayout& l, const WaterParams& p,
                     std::vector<vertex>& out);


//--- The foam ----------------------------------------------------------------------------------

//Where foam comes from, and how it behaves. Every range is hashed per ball.
struct FoamEmitter{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float spread_x = 0.5f, spread_z = 0.2f;     //half extents of where a ball starts
    int   every = 2;                //ticks between balls
    int   life_min = 50, life_max = 90;         //ticks
    float radius_min = 0.12f, radius_max = 0.28f;
    float burst_min = 1.2f, burst_max = 2.0f;   //its first upward speed, units/s
    float rise = 0.3f;              //the speed it settles to, up
    float drag = 3.0f;              //1/s, toward that
    float drift_x = 0.0f, drift_z = 0.25f;      //the steady carry, units/s - the flow and "away"
    float wander = 0.3f;            //+- sideways speed, per ball
};

struct FoamBall{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float radius = 0.0f;            //its full size
    int   age = 0, life = 0;        //ticks
    int   emitter = -1;             //-1 free
    float Scale() const;            //its size right now: swells in, shrinks away to nothing
};

struct FoamSwarm{
    std::vector<FoamEmitter> emitters;
    std::vector<FoamBall> balls;    //fixed size, set by Init; a free slot has emitter -1
    std::vector<uint32_t> spawns;   //per emitter, how many it has made

    //Sizes the pool to what the emitters can have alive at once, and empties it.
    void Init(const std::vector<FoamEmitter>& e);
    //One tick at the absolute tick `tick`: every emitter due spawns one ball, every ball moves
    //and ages. A pool that is full drops the ball rather than growing - Init sized it for all.
    void Step(int64_t tick);
    int  Alive() const;
};

//The two landings' emitters for one water - the pool's, and the spill's in the stream - and a
//thin trail of flecks riding the stream away. Appended.
void WaterFoamEmitters(const StageWater& w, const WaterLayout& l, std::vector<FoamEmitter>& out);

#endif

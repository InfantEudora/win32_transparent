#ifndef _ARCHER_BACKDROP_H_
#define _ARCHER_BACKDROP_H_

#include "Stage.h"

#include <vector>

/*
    The rock BEHIND the terrain - the back wall of a cave, with the play area in front of it.

    The marching-cubes slab is a few units deep and ends; behind it the camera saw the painted
    backdrop, so the level read as a shelf floating in front of a picture. This is a tall wall of
    rock set back in z, reaching down to the slab's bottom and rising well above it, with RIDGES standing
    forward out of it and pines on its high points. The slab stands in front of it the way a ledge
    stands in a cave.

    LOOKS ONLY. The blocks this makes are never Stage::blocks - nothing collides with them, Stage
    never sees them, and the app meshes them with the same Terrain.cpp mesher on a params set of
    its own (rounder, noisier, coarser; it is further away). Pure placement, like Foliage: blocks
    in, blocks and tree spots out, no engine type, so `make rules` checks it.

    --- WHAT IT FOLLOWS ---------------------------------------------------------------------------
    The GROUND, and only the ground: every live, visible BLOCK_SOLID in the x range that has
    nothing under it and stands on nothing - the floor of a terrain bay. So the upper bay, which
    is all floaters, gets none; the ground bay's wall stands behind both.

    --- THE THREE PIECES --------------------------------------------------------------------------
      - The WALL: columns `segment` wide, each from drop_below under the ground to a height read off
        a smooth 1D noise along x - massifs up to wall_high, dips down to wall_low - so the ridge
        line rolls rather than stepping at random. Columns overlap, and the mesher's smooth union
        rolls them into one face. Its front is wall_gap behind the ground block's back.
      - The RIDGES: narrow buttresses in front of the wall, one every so often by a hash, lower than
        the wall where they stand. Their front is front_gap behind the ground's back - nearer the
        camera than the wall - which is what gives the face light and shadow instead of one plane.
      - The TREES: pine spots on the wall's high ground - a column tree_min_rise above the ground
        with no neighbour more than tree_max_step higher, so none stands deep in a fillet the
        smooth union has raised - at least tree_spacing apart. Size and yaw per spot, by hash.
        Strict local peaks only were tried first: two pines in 28 units.

    DETERMINISTIC - every random number is a hash of where it is drawn, so the same level always
    grows the same wall.

    --- WHY front_gap -----------------------------------------------------------------------------
    Above the ground's grass the rock is SEEN, and it must not reach forward over her walking line.
    The mesher pushes a surface out past its block's nominal faces by the rounding and both noises;
    front_gap holds the nearest nominal face (a ridge's) that far behind the ground's back face, so
    the surface bulges up to the back of the slab and no further. The app measures that on the
    finished mesh rather than trusting it.
*/
struct BackdropParams{
    /*
        How far under the ground block's bottom everything reaches. ZERO: the bank ends where the
        slab does, and the camera - always above the slab's bottom, and nearer the slab than the
        bank - sees that edge behind the slab, never under it. It was 16, and hung below the slab
        as a pale apron with nothing in front of it and nothing standing on it.
    */
    float drop_below   = 0.0f;
    //The wall.
    float wall_gap     = 3.0f;      //its front, this far behind the ground block's back
    float wall_half_depth = 2.5f;
    float wall_low     = 2.0f;      //the ridge line's lowest, above the ground's top
    float wall_high    = 19.0f;     //and its highest - above the upper bay's island (11.25), so its pines show
    float wall_wavelength = 11.0f;  //of the noise the ridge line follows, in world units
    float wall_jitter  = 0.6f;      //+- per column on top of the noise, so it is not too smooth
    float segment      = 2.0f;      //column width
    float overlap      = 0.5f;      //added to each side of a column, so neighbours merge
    float overhang_x   = 0.5f;      //past the ground's two ends
    //The ridges.
    float front_gap    = 1.3f;      //a ridge's front, this far behind the ground block's back
    float ridge_spacing = 3.0f;     //one candidate every this far along x...
    float ridge_chance = 0.65f;     //...kept with this probability
    float ridge_hw_min = 0.5f;      //half-width, hashed between these
    float ridge_hw_max = 1.2f;
    float ridge_drop_min = 1.0f;    //how far below the wall's top where it stands, hashed
    float ridge_drop_max = 5.0f;
    //The trees.
    float tree_min_rise = 3.0f;     //a spot's top at least this far above the ground's top
    //And no neighbour more than this higher, or the canopy runs into the rock beside it. 0.8 was
    //right for a wall of 13 and left two pines on one of 19, whose slopes step further per column.
    float tree_max_step = 1.5f;
    float tree_spacing = 3.0f;      //no two pines closer than this along x
    float tree_chance  = 0.7f;
    float tree_scale_min = 0.7f;    //multiplier on the pine's own scale, hashed between these
    float tree_scale_max = 1.4f;
    //A waterfall's notch (Water.h): its stretch of wall cut down to the lip and set back by this,
    //so the water comes out of a cleft...
    float notch_recess = 2.0f;
    //...with the columns either side standing at least this far over the lip, so it reads as one,
    float notch_rise   = 2.5f;
    //...cut this much wider than the pool's shelf each side, for the neighbours' rounding.
    float notch_margin = 0.6f;
    //Behind a block that reaches back into the bank (a cave's roof), the wall stands at least
    //this far over its top, so the two close - see A ROOF in Backdrop.cpp.
    float roof_rise    = 1.5f;
};

struct BackdropTree{
    float x = 0.0f, y = 0.0f, z = 0.0f;     //y is the top it stands on
    float yaw = 0.0f;                       //radians about +Y
    float scale = 1.0f;
};

/*
    The wall, ridges and tree spots behind every ground block whose centre is in [x_min, x_max) x
    [y_min, y_max). `out` and `trees` are cleared first; `grounds`, if given, gets the index into
    `blocks` of each ground block used. Wall columns come first in `out`, then ridges.

    `waters`, if given, are cut into it: over each fall the wall is notched and set back (see
    WaterLayout), no ridge stands in its pool, the ridges along its stream stand back from it -
    on whichever ground they are, since a stream runs on past its own - and no pine grows in the
    notch. Without them the bank is exactly what it always was.

    And wherever a block reaches back into the bank (Back() at or behind the wall's face - a cave's
    roof and walls), the wall under it rises roof_rise over its top, so the two close.
*/
void BuildBackdropBlocks(const std::vector<StageBlock>& blocks, float x_min, float x_max,
                         float y_min, float y_max, const BackdropParams& params,
                         std::vector<StageBlock>& out, std::vector<BackdropTree>* trees = NULL,
                         std::vector<int>* grounds = NULL,
                         const std::vector<StageWater>* waters = NULL);

#endif

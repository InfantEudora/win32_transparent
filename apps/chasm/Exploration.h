#ifndef _CHASM_EXPLORATION_H_
#define _CHASM_EXPLORATION_H_

#include <stdint.h>
#include <algorithm>
#include <string>
#include <vector>
#include "type_vec2.h"

class Grid;
class GridPicker;
class Terrain;

/*
    WHAT HAS BEEN SEEN (docs/play_mode_plan.md, step 2): a raster over the map, EXPLORATION_CELL world
    units a cell, each cell explored or not. Explored stays explored.

    SIMULATION STATE, the physics thread's alone like the zones: cleared only by the tick, by what the
    PEOPLE SEE - every person out of doors looks round every EXPLORATION_PERSON_TICKS (See, below) -
    saved and hashed, published as a copy for the view (the clouds) when it changes. A new map starts
    with nothing explored; the settlers at the camp look round on the first tick.

    Nothing is cleared round what is placed or built (user, 2026-10-08): a road planned at the clouds'
    edge showed what lay past it at once. Now it shows nothing until its builders walk out and look.

    The rule it carries: a PLAY zone command (one that says so) under unexplored ground is refused,
    "under the clouds". Debug commands - every tool's, and every recording made before play mode - are
    not held to it.
*/

#define EXPLORATION_CELL            2.0f    //world units a cell
#define EXPLORATION_PERSON_TICKS    10      //how often the people look round

/*
    SIGHT - a LINE OF SIGHT over the ground, not a disc. A cell is seen when a point SIGHT_TARGET over
    its ground can be seen from the eye, over the ground in between, within the range; the range grows
    with how far the eye stands above the cell, so a hill or a terrace step looks out further over lower
    ground, and a cliff hides what is on top of it from below.

    The air over anything lower than both the eye's ground and the PLATEAU'S GROUND there (the LID:
    its rise and fall carried on over the chasm, where the clouds lie) counts as ground at the lower of
    the two: from the rim one sees across the chasm from its very edge - the lid is what is seen - and
    from a balcony across it at the balcony's height.
    Current tuning (tuning_numbers_not_rules), against figures 0.86 tall and a house 2 across.
*/
#define SIGHT_EYE                   0.8f    //a person's eye over his ground
#define SIGHT_TARGET                1.0f    //a cell is seen when this much over its ground is - a person, a bush
#define SIGHT_RANGE                 20.0f   //how far on level ground
#define SIGHT_RANGE_PER_HEIGHT      1.0f    //and this much further for every unit the eye stands over the cell
#define SIGHT_RANGE_MAX             40.0f

//How far an eye over `eye_ground` sees something standing on `ground`: further the higher it stands over it.
inline float SightReach(float eye_ground, float ground){
    float over = eye_ground - ground;
    return std::min(SIGHT_RANGE_MAX,SIGHT_RANGE + SIGHT_RANGE_PER_HEIGHT * (over > 0.0f ? over : 0.0f));
}

//The ground under every cell of the exploration's raster, from the world alone: built once a map
//(ChasmWorld::sight) and immutable after, so any thread may read it.
struct ExplorationSight{
    void Build(const Grid& g, const GridPicker& picker, const Terrain& t);
    /*
        Whether the point at height `top` over `to` can be seen from an eye SIGHT_EYE over `eye_ground` at `from`: the
        ground between - taken as See takes it, the lid's air too - all below the straight line from the one to the
        other. The cells of both ends are left out: what either stands on does not hide it. How far is not asked
        here (SightReach). For a shot (zombies_plan.md, step 3).
    */
    bool Clear(const vec2& from, float eye_ground, const vec2& to, float top) const;
    vec2 origin;
    int width = 0;
    int height = 0;
    std::vector<float> ground;  //the ground's height at each cell's middle, row by row from origin
    std::vector<float> lid;     //and the plateau's ground there, had the cell been plateau (the clouds' lid)
    float lid_min = 0.0f;       //the lowest lid: nothing is seen as lower than this, or than the eye's ground
    float build_ms = 0.0f;
};

class Exploration{
public:
    //Everything unexplored, over the map from lo to hi (the grid's bounds).
    void Reset(const vec2& lo, const vec2& hi);
    void RevealAll();
    bool Explored(const vec2& p) const;
    //...and for the clouds, whose lattice runs a little past the raster: a point off it takes the nearest cell's.
    bool ExploredNearest(const vec2& p) const;
    //The cell holding p, -1 off the raster; and a cell's middle.
    int CellAt(const vec2& p) const;
    vec2 CellCentre(int cell) const;
    /*
        Marks what can be seen from the middle of `cell` with the eye over `eye_ground` (SIGHT, above);
        true when any of it was unexplored before. Depends on nothing but its arguments and the world,
        so a second look from the same place and height clears nothing more.
    */
    bool See(const ExplorationSight& sight, int cell, float eye_ground);
    //How far p is from unexplored ground, up to `reach` (reach when there is none that near) - the
    //clouds thin toward the explored edge by it.
    float DistanceToUnexplored(const vec2& p, float reach) const{ return DistanceTo(p,reach,0); }
    //And from explored ground - a cloud over unexplored ground shrinks toward it.
    float DistanceToExplored(const vec2& p, float reach) const{ return DistanceTo(p,reach,1); }
    float DistanceTo(const vec2& p, float reach, uint8_t value) const;
    bool Empty() const { return cells.empty(); }
    int CountExplored() const;

    //As a save holds it: run lengths, alternating unexplored and explored, from the first cell.
    std::string ToString() const;
    bool FromString(const std::string& s);
    uint64_t Hash() const;

    uint32_t version = 0;       //bumped on any change - the view rebuilds what moved
    vec2 origin;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> cells; //1 explored, row by row from origin
};

#endif

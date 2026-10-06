#ifndef _CHASM_EXPLORATION_H_
#define _CHASM_EXPLORATION_H_

#include <stdint.h>
#include <string>
#include <vector>
#include "type_vec2.h"

/*
    WHAT HAS BEEN SEEN (docs/play_mode_plan.md, step 2): a raster over the map, EXPLORATION_CELL world
    units a cell, each cell explored or not. Explored stays explored.

    SIMULATION STATE, the physics thread's alone like the zones: cleared only by the tick - round
    everything built when the zones change, round every person every EXPLORATION_PERSON_TICKS - saved
    and hashed, published as a copy for the view (the clouds) when it changes. A new map starts with
    nothing explored; the camp, a building, clears its own ground on the first tick.

    The rule it carries: a PLAY zone command (one that says so) under unexplored ground is refused,
    "under the clouds". Debug commands - every tool's, and every recording made before play mode - are
    not held to it.
*/

#define EXPLORATION_CELL            2.0f    //world units a cell
#define EXPLORATION_BUILT_RADIUS    26.0f   //cleared round every built plot, field cell and road
#define EXPLORATION_PERSON_RADIUS   14.0f   //cleared round a person, as he walks
#define EXPLORATION_PERSON_TICKS    10      //how often the people clear round themselves

class Exploration{
public:
    //Everything unexplored, over the map from lo to hi (the grid's bounds).
    void Reset(const vec2& lo, const vec2& hi);
    void RevealAll();
    //Clears a disc; true when any cell was unexplored before.
    bool Reveal(const vec2& centre, float radius);
    bool Explored(const vec2& p) const;
    //How far p is from unexplored ground, up to `reach` (reach when there is none that near) - the
    //clouds thin toward the explored edge by it.
    float DistanceToUnexplored(const vec2& p, float reach) const{ return DistanceTo(p,reach,0); }
    //And from explored ground - a cloud over unexplored ground shrinks toward it.
    float DistanceToExplored(const vec2& p, float reach) const{ return DistanceTo(p,reach,1); }
    float DistanceTo(const vec2& p, float reach, uint8_t value) const;
    bool Empty() const { return cells.empty(); }

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

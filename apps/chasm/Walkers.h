#ifndef _CHASM_WALKERS_H_
#define _CHASM_WALKERS_H_

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "type_vec2.h"
#include "Zones.h"

/*
    WALKERS (docs/roads_plan.md): figures that walk the plots by A*. Simulation state, the physics
    thread's alone, changed only by CHASM_CMD_WALKER commands and by the tick - so a replay walks
    every step again exactly.

    For now there is only the DEBUG walker: it has a home and a goal and walks between them, back and
    forth for ever, which is enough to prove the pathfinding and the replay before any economy is
    put on top, and keeps one on screen while roads are painted under it.

    --- THE GRAPH ------------------------------------------------------------------------------------
    The plots: a fine vertex joined to each vertex a fine edge joins it to. An edge is closed across a
    cliff (its ends on different levels), beside a river (either end wet), into a house that is not
    where the walk starts or ends, through a wall or palisade (ZoneBoundaryBetween) except at a
    gate (ZoneGateBetween), onto the north mountain, and up a slope steeper than WALKER_STEEPEST. It
    costs its
    length over the speed it is walked at - fastest on a road, slowest through a field, and slower
    the steeper it is.

    --- WHEN THE ZONES CHANGE -------------------------------------------------------------------------
    Every walker plans again from the plot it is walking toward, keeping the edge it is on - or, if
    that plot is now closed to it, turns back along the edge and plans from the one it came from. A
    walker with no way to its target finishes its edge and waits, and tries again at the next change.

    A path is SAVED, never planned again on load: A* from a later point on a path can find another of
    the same cost, and a replay has to be the same walk.
*/

#define WALKER_MAX              64
#define WALKER_SPEED_ROAD       3.2f    //world units a second, both ends of the edge road
#define WALKER_SPEED_GROUND     1.6f    //everywhere else
#define WALKER_SPEED_FIELD      0.9f    //both ends in a field
#define WALKER_SLOPE_COST       3.0f    //speed divided by 1 + this x the grade (rise over run)
#define WALKER_STEEPEST         0.75f   //rise over run past which an edge cannot be walked at all

struct Walker{
    int home = -1;              //plots
    int goal = -1;
    bool f_outward = true;      //walking home -> goal (false: back)
    /*
        The plots it is walking through: path[seg] -> path[seg + 1] is the edge it is on, `along` the
        distance walked down it. At the end of the path (seg == path.size() - 1) it is standing on its
        target, and turns round at the next tick.
    */
    std::vector<int> path;
    int seg = 0;
    float along = 0.0f;
    bool f_stuck = false;       //no way to the target at the last plan: waiting at the path's end
    uint32_t legs = 0;          //how many times it has reached a target
};

//Shared by the save and the hash: everything that decides where a walker goes next.
struct WalkerSet{
    std::shared_ptr<const ChasmWorld> world;
    std::vector<Walker> walkers;
    uint32_t zones_version = 0;     //the zones' version the paths were planned for
    uint32_t version = 0;           //bumped on any change - the view rebuilds a path line on it
};

/*
    The plot graph of one world: CSR adjacency of the fine level, with each edge's length. Built once
    per world and immutable, like the rest of what is generated.
*/
struct WalkGraph{
    std::shared_ptr<const ChasmWorld> world;
    std::vector<int> start;         //per vertex + 1
    std::vector<int> next;          //neighbour vertices
    std::vector<float> length;      //per entry of `next`
};

class Walkers{
public:
    void Reset(std::shared_ptr<const ChasmWorld> world);
    std::shared_ptr<const ChasmWorld> GetWorld() const { return set.world; }

    //Commands. True if anything changed; otherwise last_refusal says why not.
    bool Spawn(const ZoneState& z, int home, int goal);
    void Clear();
    std::string last_refusal;

    //One tick of `dt` seconds: plan again if the zones moved, then walk.
    void Tick(const ZoneState& z, float dt);

    //Put back exactly as saved: walkers, their paths and how far down them they are.
    void Restore(std::shared_ptr<const ChasmWorld> world, const std::vector<Walker>& walkers,
                 uint32_t zones_version);

    const WalkerSet& State() const { return set; }

    //Where a walker is now, on the ground plane (x, world z), and which way it faces.
    static vec2 Position(const ChasmWorld& w, const Walker& k, vec2* facing = NULL);

private:
    WalkerSet set;
    std::shared_ptr<const WalkGraph> graph;
    void EnsureGraph();
    //A* from `from` to `to`; the plots in order, both ends included. Empty when there is no way.
    std::vector<int> FindPath(const ZoneState& z, int from, int to) const;
    void Replan(const ZoneState& z, Walker& k);
    bool EdgeOpen(const ZoneState& z, int a, int b, int from, int to) const;
    float EdgeSpeed(const ZoneState& z, int a, int b) const;
    float EdgeLength(int a, int b) const;
};

#endif

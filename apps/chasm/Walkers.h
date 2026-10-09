#ifndef _CHASM_WALKERS_H_
#define _CHASM_WALKERS_H_

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "type_vec2.h"
#include "Zones.h"

/*
    WALKING (docs/roads_plan.md): the way between two plots by A*, for the economy's people (Economy.h,
    PlanRoute). Step 10 proved it with a DEBUG WALKER - a figure walking between a home and a goal for
    ever, saved and hashed - which was retired on 2026-10-07 (the user's: it had outlived its purpose,
    and the villagers walk these routes now). What is left is the graph and its rules.

    --- THE GRAPH ------------------------------------------------------------------------------------
    The plots: a fine vertex joined to each vertex a fine edge joins it to. An edge is closed across a
    cliff (its ends on different levels), beside a river (either end wet), into a house that is not
    where the walk starts or ends, through a garden's wall or a lot's fence (ZoneBoundaryBetween) except
    at a gate (ZoneGateBetween) or out of a yard the walk starts in (shut in it), onto a palisade that stands (ZoneWallStands - its gates are road), onto
    the north mountain, and up a slope steeper than WALKER_STEEPEST. It
    costs its length over the speed it is walked at - fastest on a road, slowest through a field, and
    slower the steeper it is.

    A WINCH (docs/buildings_plan.md step 2) adds one edge more: from its plot on the rim down to its
    landing on the balcony below (ZoneWinchLinks) - the only edge that crosses a level, and it takes
    WINCH_RIDE_SECONDS whatever its length. A winch's plot is a passage, walked through, not a
    building that is only ever a walk's first or last plot. The links are worked out again whenever
    the zones change.
*/

#define WALKER_SPEED_ROAD       3.2f    //world units a second, both ends of the edge road
#define WALKER_SPEED_GROUND     1.6f    //everywhere else
#define WALKER_SPEED_FIELD      0.9f    //both ends in a field
#define WALKER_SPEED_SWAMP      1.0f    //both ends in the swamp, off a road
#define WALKER_SLOPE_COST       3.0f    //speed divided by 1 + this x the grade (rise over run)
#define WALKER_STEEPEST         0.75f   //rise over run past which an edge cannot be walked at all
#define WINCH_RIDE_SECONDS      5.0f    //from the rim down to the balcony, or up
#define WALKER_OPEN_GROUND      24      //plots of open ground, joined, that make OUTSIDE; fewer, closed in by buildings, are a yard

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
    std::shared_ptr<const ChasmWorld> GetWorld() const { return world; }

    /*
        The grid's way from plot `from` to plot `to` - the economy's people (Economy.h): the plots in
        order, both ends included, and each edge's speed. False, and both empty, when there is no way.
    */
    bool PlanRoute(const ZoneState& z, int from, int to, std::vector<int>& plots, std::vector<float>& speeds);
    /*
        Whether a walk can end on plot `v` from OUTSIDE: a neighbour with nothing built on it (or a passage),
        the edge from it into `v` open, and behind it at least WALKER_OPEN_GROUND plots of open ground joined
        together - not a pocket a block of buildings closed in, which nobody gets to. What makes a building's
        plot its door (Economy::Derive); with none, the building is shut in (EconomyShutIn).
    */
    bool OpenFromOutside(const ZoneState& z, int v);

private:
    std::shared_ptr<const ChasmWorld> world;
    std::shared_ptr<const WalkGraph> graph;
    void EnsureGraph();
    //The winches' links (plot, landing), for the zones of version links_version.
    std::vector<std::pair<int,int>> links;
    uint32_t links_version = 0xFFFFFFFFu;
    void EnsureLinks(const ZoneState& z);
    bool IsLink(int a, int b) const;
    //A* from `from` to `to`; the plots in order, both ends included. Empty when there is no way.
    std::vector<int> FindPath(const ZoneState& z, int from, int to) const;
    bool EdgeOpen(const ZoneState& z, int a, int b, int from, int to) const;
    float EdgeSpeed(const ZoneState& z, int a, int b) const;
    float EdgeLength(int a, int b) const;
};

#endif

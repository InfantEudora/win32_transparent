#ifndef _CHASM_WALKERS_H_
#define _CHASM_WALKERS_H_

#include <algorithm>
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

    --- LAYERS (docs/ramparts_plan.md) ---------------------------------------------------------------
    A node of the walk is a plot and a LAYER on it: 0 its ground - the graph above, its rules unchanged,
    so with no decks A* sees what it always saw - or a DECK over it, apart from what stands on its ground
    (the arches' rule). A layer counts half storeys: a flat deck stands on whole storeys (even), a stair's
    middle on a half (odd); a flat node spans its one height, a stair the storey from under its middle to
    over it. Two nodes on neighbouring plots of one ground join where their spans meet (WalkLayersMeet); a
    CONNECTOR joins layers that do not - a guard tower's ladder, from the plot it stands on up to the deck,
    at the ladder edge's old cost. Decks are few: nodes after the plots', worked out again whenever the
    zones change, as the links are.

    WHO walks decides what is open: a tower's deck is anyone's (a dead end - its guards, and the caretakers
    who stock it), a castle's only the POSTED's - guards and anyone under an order (the user's, 2026-10-10:
    a wall any villager may walk is a way over a shut gate). The hook the zombies' own map will use
    (gameplay_plan.md, section 7).
*/

#define WALKER_SPEED_ROAD       3.2f    //world units a second, both ends of the edge road
#define WALKER_SPEED_GROUND     1.6f    //everywhere else
#define WALKER_SPEED_FIELD      0.9f    //both ends in a field
#define WALKER_SPEED_SWAMP      1.0f    //both ends in the swamp, off a road
#define WALKER_SLOPE_COST       3.0f    //speed divided by 1 + this x the grade (rise over run)
#define WALKER_STEEPEST         0.75f   //rise over run past which an edge cannot be walked at all
#define WINCH_RIDE_SECONDS      5.0f    //from the rim down to the balcony, or up
#define WALKER_OPEN_GROUND      24      //plots of open ground, joined, that make OUTSIDE; fewer, closed in by buildings, are a yard
#define WALKER_SPEED_DECK       WALKER_SPEED_ROAD   //along a deck: paved, as a road - never faster, or A*'s estimate breaks
#define WALKER_SPEED_STAIR      1.2f    //up or down a stair

#define WALK_GROUND             0       //a node's layer: its plot's ground
#define WALK_WHO_VILLAGER       0       //who walks: anyone...
#define WALK_WHO_POSTED         1       //...or a guard, or someone under an order: a castle's decks too

//Where a walk is: a plot, and the layer on it.
struct WalkPlace{
    int plot = -1;
    uint8_t layer = WALK_GROUND;
};

//A deck: walkable floor over a plot, apart from what stands on its ground.
struct WalkDeck{
    int plot = -1;
    uint8_t layer = 0;          //half storeys over the plot's ground: even a flat deck, odd a stair's middle
    bool f_posted = false;      //walked by the posted only (WALK_WHO_POSTED)
};

//Whether a layer is a stair's middle.
inline bool WalkLayerStair(int layer){
    return (layer & 1) != 0;
}
//The layer two nodes on neighbouring plots meet at - where their spans touch - or -1 where they do not.
inline int WalkLayersMeet(int a, int b){
    int lo = std::max(WalkLayerStair(a) ? a - 1 : a,WalkLayerStair(b) ? b - 1 : b);
    int hi = std::min(WalkLayerStair(a) ? a + 1 : a,WalkLayerStair(b) ? b + 1 : b);
    if (lo > hi){
        return -1;
    }
    return (lo == hi) ? lo : a;     //two stairs side by side, over the same storey: at their middle
}
#define WALK_MOST_DECKS         2       //decks over one plot: a castle piece and one over it
//The layers of plot v's decks, lowest first, into `out`: a castle piece's top and an arch over it (Zones.h) - or for
//a stair on an arch, the stair's alone - or a guard tower's platform once it stands whole with a ladder. Returns how
//many - 0, its ground its only node.
int WalkDeckLayers(const ZoneState& z, int v, int* out);
/*
    Whether node (a, la) and node (b, lb) on neighbouring plots join - the layer they meet at, or -1. On one ground,
    where their spans meet, and never to the ground under a block or a stair; and a STAIR comes down to the GROUND one
    way - on the side across from its higher neighbours, never off its flanks - while to other pieces it joins
    wherever they are, so a flight may turn. The walkers' joins and the castle's drawing (CastleMesh.h) both ask here.
*/
int WalkJoin(const ChasmWorld& w, const ZoneState& z, int a, int la, int b, int lb);
//The layer node (a, la) meets any of b's nodes at - its top deck asked first, its ground last - or -1.
int WalkJoinAny(const ChasmWorld& w, const ZoneState& z, int a, int la, int b);

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
        The grid's way from where a walk starts to plot `to` - the economy's people (Economy.h): the places in
        order, both ends included, and each edge's speed. It ends on `to`'s layer `to_layer` if he may walk it,
        else on its top deck he may walk, or its ground (GoalPlace). One starting on a deck only the posted walk
        gets off it whoever he is. False, and both empty, when there is no way.
    */
    bool PlanRoute(const ZoneState& z, WalkPlace from, int to, int who, std::vector<WalkPlace>& places,
                   std::vector<float>& speeds, int to_layer = -1);
    /*
        Where a walk by `who` to plot `to` ends: its layer `to_layer` when it has that place and he may walk it - the
        ground under an arch, the deck a click was on (ramparts_plan.md) - else its top deck he may walk, or its ground.
        -1 asks for the top.
    */
    WalkPlace GoalPlace(const ZoneState& z, int to, int who, int to_layer = -1);
    //The decks of zones `z`, in plot and layer order (LAYERS, above).
    const std::vector<WalkDeck>& Decks(const ZoneState& z);
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
    /*
        The decks for the zones of version decks_version, and every edge off the grid's that touches one -
        a deck to a deck, to the ground by a stair or a ladder - both ways, sorted by the node it leaves.
        A node is a plot's ground below the plots' count, a deck (by its index in `decks`) above it.
    */
    struct Join{
        int from;
        int to;
        float length;
        float speed;
    };
    std::vector<WalkDeck> decks;
    std::vector<Join> joins;
    uint32_t decks_version = 0xFFFFFFFFu;
    void EnsureDecks(const ZoneState& z);
    int FirstDeck(int plot) const;
    int DeckNode(int plot, int layer) const;
    bool JoinOpen(const ZoneState& z, const Join& j, int who, int from, int to) const;
    //A* between two nodes; the nodes in order, both ends included. Empty when there is no way.
    std::vector<int> FindPath(const ZoneState& z, int from, int to, int who) const;
    bool EdgeOpen(const ZoneState& z, int a, int b, int from, int to) const;
    bool GroundEnterable(const ZoneState& z, int v, int from, int to) const;
    float EdgeSpeed(const ZoneState& z, int a, int b) const;
    float EdgeLength(int a, int b) const;
};

#endif

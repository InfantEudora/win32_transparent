#ifndef _CHASM_WALKERS_H_
#define _CHASM_WALKERS_H_

#include <algorithm>
#include <climits>
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
    over it. A castle piece's layers count from its LEVEL (ZoneCastleBase, terraces_plan.md: whole storeys,
    as a terrace's top), a tower's from its ground - so heights are compared as heights (WalkNodeHalves). Two
    nodes on neighbouring plots of one ground join where their spans meet - a castle's deck and a terrace's
    top, or the hill, level with it, too - and a stair's foot comes down to the ground as much as a storey
    under it; a CONNECTOR joins layers that do not - a guard tower's ladder, from the plot it stands on up to
    the deck, at the ladder edge's old cost. Decks are few: nodes after the plots', worked out again whenever
    the zones change, as the links are.

    WHO walks decides what is open: a tower's deck is anyone's (a dead end - its guards, and the caretakers
    who stock it), a castle's only the POSTED's - guards and anyone under an order (the user's, 2026-10-10:
    a wall any villager may walk is a way over a shut gate). A ZOMBIE (zombies_plan.md) walks the villagers'
    ground and no deck at all; the walls that break under it are its step 4. It WADES through water too, at
    WALKER_SPEED_WADE (swamp_huts_plan.md step 2).
*/

#define WALKER_SPEED_ROAD       3.2f    //world units a second, both ends of the edge road
#define WALKER_SPEED_GROUND     1.6f    //everywhere else
#define WALKER_SPEED_FIELD      0.9f    //both ends in a field
#define WALKER_SPEED_SWAMP      1.0f    //both ends in the swamp, off a road
#define WALKER_SPEED_WADE       0.4f    //a zombie through the water (swamp_huts_plan.md step 2): a river buys time, not safety
#define WALKER_SLOPE_COST       3.0f    //speed divided by 1 + this x the grade (rise over run)
#define WALKER_STEEPEST         0.75f   //rise over run past which an edge cannot be walked at all
#define WINCH_RIDE_SECONDS      5.0f    //from the rim down to the balcony, or up
#define WALKER_OPEN_GROUND      24      //plots of open ground, joined, that make OUTSIDE; fewer, closed in by buildings, are a yard
#define WALKER_SPEED_DECK       WALKER_SPEED_ROAD   //along a deck: paved, as a road - never faster, or A*'s estimate breaks
#define WALKER_SPEED_STAIR      1.2f    //up or down a stair
#define WALK_TURRET_ROUND       1.8f    //an open turret's walk round its steps, per height climbed (castle_plan.md)

#define WALK_GROUND             0       //a node's layer: its plot's ground
#define WALK_WHO_VILLAGER       0       //who walks: anyone...
#define WALK_WHO_POSTED         1       //...or a guard, or someone under an order: a castle's decks too
#define WALK_WHO_ZOMBIE         2       //...or a zombie: the villagers' ground, and never a deck
#define WALK_WHO_ICE            3       //...or a villager who may cross the ice: a woodcutter after a site's tree (ICE, below)

//What MapFlood makes of frozen water: nothing (it is water), the ice there is now, or all the ice midwinter brings.
#define WALK_ICE_NONE           0
#define WALK_ICE_NOW            1
#define WALK_ICE_WINTER         2

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
    How high node (plot, layer) stands, in half storeys from y = 0: its ground's height rounded to a half storey (a
    terrace's top is one exactly); a castle piece's deck over its level (ZoneCastleBase); a tower's over its ground.
*/
int WalkNodeHalves(const ChasmWorld& w, const ZoneState& z, int plot, int layer);
//A stone tower's floors (castle_plan.md), in half storeys as WalkNodeHalves: its first storey's over its level to its
//guard room's, one every two.
void StoneTowerFloors(const ChasmWorld& w, const ZoneState& z, int plot, int& lowest, int& highest);
#define WALK_NO_JOIN            INT_MIN     //WalkJoin's: the two do not join
/*
    Whether node (a, la) and node (b, lb) on neighbouring plots join - the height they meet at, in half storeys
    (WalkNodeHalves), or WALK_NO_JOIN. On one ground, where their spans meet - a level's first flight's foot reaching
    down to ground as much as a storey under it - and never to the ground under a block or a stair; and a stair's
    FLIGHT only up its way and down it, never off its flanks; an open TURRET's top up its way, and its foot to its
    level's ground (castle_plan.md "The stairs"). The walkers' joins and the castle's drawing (CastleMesh.h) both ask
    here.
*/
int WalkJoin(const ChasmWorld& w, const ZoneState& z, int a, int la, int b, int lb);
//The height node (a, la) meets any of b's nodes at - its top deck asked first, its ground last - or WALK_NO_JOIN; the
//layer of b's it meets in `lb_out`.
int WalkJoinAny(const ChasmWorld& w, const ZoneState& z, int a, int la, int b, int* lb_out = NULL);

/*
    A ZOMBIE'S WAY (zombies_plan.md, step 4): what stands in a walk's way costs a zombie time instead of stopping it - a
    standing palisade plot or a shut gate to break, a building's door to breach (ZoneKindHasDoor), a garden's wall or a
    lot's fence to climb.
    The walkers know where those are; the economy knows how much of each is left, in seconds of one zombie at it.
*/
struct WalkBreaks{
    virtual ~WalkBreaks(){}
    virtual float WallLeft(int plot) const = 0;
    virtual float GateLeft(int plot) const = 0;
    virtual float DoorLeft(uint32_t building) const = 0;    //0 once it is breached: walked through
    virtual float ClimbSeconds() const = 0;
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
    std::shared_ptr<const ChasmWorld> GetWorld() const { return world; }

    /*
        The grid's way from where a walk starts to plot `to` - the economy's people (Economy.h): the places in
        order, both ends included, and each edge's speed. It ends on `to`'s layer `to_layer` if he may walk it,
        else on its top deck he may walk, or its ground (GoalPlace). One starting on a deck only the posted walk
        gets off it whoever he is. False, and both empty, when there is no way. A zombie's walk with `breaks` goes
        through what it can break, at the cost of breaking it (WalkBreaks).
    */
    bool PlanRoute(const ZoneState& z, WalkPlace from, int to, int who, std::vector<WalkPlace>& places,
                   std::vector<float>& speeds, int to_layer = -1, const WalkBreaks* breaks = NULL);
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
    /*
        SETTLEMENTS (docs/settlements_plan.md): how far each plot is to walk from the nearest of plots `from`, over
        the MAP - the ground's own rules (EdgeOpen's): a standing bridge over water, a winch up a cliff, nothing too
        steep, never the mountain - and not what is built on it. Water and mountain beside walkable ground are
        reached, as the land's edge, but lead nowhere. Out to `reach`; INFINITY past it. `dist` is by plot.
        `ice` (WALK_ICE_*) leads on over frozen water as well - a woodcutter's reach (ICE, below).
    */
    void MapFlood(const ZoneState& z, const std::vector<int>& from, float reach, std::vector<float>& dist,
                  int ice = WALK_ICE_NONE);
    /*
        ICE (seasons_plan.md, the user's 2026-10-12): the water under the snow is frozen - the same line the view
        draws its ice by, and a collector stops filling at (UnderSnow) - and the POSTED may walk it: someone under
        an order, a guard. Nobody else, and not MapFlood's territory. Set from the simulation's tick before a walk is
        planned (the economy's Tick and Order); a walk planned on the ice keeps it if the thaw comes on the way.
        ...and a WOODCUTTER (WALK_WHO_ICE; the user's, 2026-10-16) when there is no other way: a tree in a site's way
        across the water - a bridge's far bank - is felled in winter, and he walks back over the ice too.
    */
    void SetSeason(uint64_t tick);
    bool Frozen(int v) const;          //plot v under the snow now, by SetSeason's tick
    bool AnyIce() const { return f_ice; }
    bool FrozenInWinter(int v) const;  //...or once the snow lies furthest, midwinter: what never freezes is south of it

private:
    bool f_ice = false;                 //any snow at all on the map
    float ice_front = 0.0f;             //the snow front's z for it (CalendarSnowFrontZ)
    bool IceOpen(int who, int v) const; //water a walk by `who` may cross on v: frozen, and he is posted or WALK_WHO_ICE
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
    std::vector<int> FindPath(const ZoneState& z, int from, int to, int who, const WalkBreaks* breaks = NULL) const;
    //With `breaks`, a zombie's: open through what it can break or climb, its seconds added to `extra`.
    bool EdgeOpen(const ZoneState& z, int a, int b, int from, int to, int who = WALK_WHO_VILLAGER,
                  const WalkBreaks* breaks = NULL, float* extra = NULL) const;
    bool GroundEnterable(const ZoneState& z, int v, int from, int to, int who = WALK_WHO_VILLAGER) const;
    float EdgeSpeed(const ZoneState& z, int a, int b, int who = WALK_WHO_VILLAGER) const;
    float EdgeLength(int a, int b) const;
};

#endif

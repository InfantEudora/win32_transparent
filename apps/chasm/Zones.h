#ifndef _CHASM_ZONES_H_
#define _CHASM_ZONES_H_

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"
#include "TerrainMesh.h"

/*
    WHAT THE PLAYER PAINTED: the zones (grid_plan.md section 5). Simulation state - changed only by
    a command applied on a tick (ZoneOp), so a run can be replayed from the commands it was given.

    Three kinds, each on its natural unit:
      - HOUSING on fine PLOTS, growing in storeys: `storeys[v]` per fine vertex, 0 = none.
      - GROUND on fine PLOTS (step 8): `ground[v]`, a garden or town ground. It is what the
        boundaries enclose - a garden's low wall, a town's palisade (BoundaryMesh.h) - and a
        house may stand on it, hiding it until the house is gone.
      - FIELDS on COARSE CELLS: `field[c]` per coarse quad.

    THE RULES, all checked when a command arrives, so the state is always valid:
      - A house needs its plot flat: every corner of every fine cell around it on the plot's
        level. Nothing is built half over a cliff.
      - A field needs its coarse cell flat: all nine of its fine vertices on one level.
      - Ground needs its plot flat in the same way.
      - Houses and ground keep off fields: neither on a plot touching a field's cell, no field on
        a cell with a house or ground on any of its vertices.
      - At most ZONE_MAX_STOREYS storeys.

    A ZoneState is immutable once published (Zones::Publish), the way the world is, and carries the
    world it was painted on: a new map makes every index in it meaningless.
*/

#define ZONE_MAX_STOREYS    4

//What a plot's ground is. Never renumber - saves and recordings refer to these by value.
#define ZONE_GROUND_NONE    0
#define ZONE_GROUND_GARDEN  1       //green, walled: Townscaper's gardens
#define ZONE_GROUND_TOWN    2       //trodden earth, palisaded: A Little Age's town
#define ZONE_GROUND_COUNT   3
const char* ZoneGroundName(int ground);

//What a command asks for. Travels in SimCommand::value[0]; the plot or cell index in subtype.
enum ZoneOp{
    ZONE_OP_NONE = 0,
    ZONE_OP_HOUSE_PAINT,    //plot: a one-storey house if it has none - what a drag does
    ZONE_OP_HOUSE_ADD,      //plot: a house, or one storey more - what a click does
    ZONE_OP_HOUSE_REMOVE,   //plot: one storey less, the last one removes the house
    ZONE_OP_HOUSE_ERASE,    //plot: no house at all
    ZONE_OP_FIELD_PAINT,    //coarse cell: a field
    ZONE_OP_FIELD_ERASE,    //coarse cell: no field
    ZONE_OP_GROUND_PAINT,   //plot: ground of the kind in SimCommand::value[1] (ZONE_GROUND_*)
    ZONE_OP_GROUND_ERASE,   //plot: no ground
    ZONE_OP_COUNT
};
const char* ZoneOpName(int op);

struct ChasmWorld;     //ChasmWorld.h

struct ZoneState{
    std::shared_ptr<const ChasmWorld> world;   //what the indices below refer to
    std::vector<uint8_t> storeys;               //per fine vertex
    std::vector<uint8_t> ground;                //per fine vertex, ZONE_GROUND_*
    std::vector<uint8_t> field;                 //per coarse quad
    std::vector<uint32_t> chunk_version;        //per terrain chunk: bumped when its zones change
    uint32_t version = 0;
    int houses = 0;
    int grounds = 0;
    int fields = 0;
};

class Zones{
public:
    //Empty zones for `world`. Physics thread.
    void Reset(std::shared_ptr<const ChasmWorld> world);
    std::shared_ptr<const ChasmWorld> GetWorld() const { return state.world; }

    //Applies one command. True if anything changed; otherwise `last_refusal` says why not.
    //`kind` is the ground kind for ZONE_OP_GROUND_PAINT and ignored otherwise.
    bool Apply(int op, uint32_t index, int kind = 0);

    //Empty zones for `world`, then a saved set painted in through Apply - so a save can only ever
    //put the state where the rules allow. Returns how many houses and fields the rules refused,
    //which for a save made on the same world is 0.
    int Restore(std::shared_ptr<const ChasmWorld> world,
                const std::vector<std::pair<int,int>>& houses, const std::vector<int>& fields,
                const std::vector<std::pair<int,int>>& grounds);
    std::string last_refusal;

    //The current state, read-only. Physics thread.
    const ZoneState& State() const { return state; }

private:
    ZoneState state;
    void Touch(int fine_quad);
};

//The rules as questions, for the commands and for the view's hover (red when it would be refused).
//`why` may be NULL.
bool ZoneCanHouse(const ChasmWorld& w, const ZoneState& z, int plot, const char** why);
bool ZoneCanField(const ChasmWorld& w, const ZoneState& z, int coarse, const char** why);
bool ZoneCanGround(const ChasmWorld& w, const ZoneState& z, int plot, const char** why);

//Floor area, the capacity measure (README.md: plots differ in size, so never count plots).
struct ZoneStats{
    int houses = 0;
    int storeys = 0;
    float house_floor_area = 0.0f;  //plot area x storeys
    int fields = 0;
    float field_area = 0.0f;
    int gardens = 0;
    int towns = 0;
    float ground_area = 0.0f;
};
ZoneStats ComputeZoneStats(const ChasmWorld& w, const ZoneState& z);

#ifdef DEBUG
//The rules re-checked over the whole state - what the commands should have guaranteed.
void RunZoneChecks(const ChasmWorld& w, const ZoneState& z, GridCheckReport& report);
#endif

#endif

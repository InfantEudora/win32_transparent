#ifndef _CHASM_AREAS_H_
#define _CHASM_AREAS_H_

#include <stdint.h>
#include <vector>
#include "type_vec2.h"

class GridPicker;
class Terrain;
struct ForestData;
struct ExplorationSight;

/*
    AREAS (docs/areas_plan.md): the parts of the map there are to be discovered - each river, each waterfall, each
    biome as one, and the starting land split by the forest into its open ground and its forest.

    GENERATED with the map and never saved, like the forest: an area is the exploration's cells its ground lies in,
    and it is SEEN once any of them is explored - the floatstone seam's rule. Which are seen is worked out again
    from the saved exploration after a load (ApplicationChasm::LookForAreas), so nothing about them is saved but the
    journal's lines of when each was found.

    THE ORDER IS THE INDEX the discovered event carries in its amount, which the journal saves: rivers in the
    terrain's order, then falls in theirs, then the land's kinds in AREA_* order. A map always builds the same list.
*/
#define AREA_RIVER          0       //one a river, branch or stream: along its water
#define AREA_WATERFALL      1       //one a fall: round its lip
#define AREA_PLATEAU        2       //the starting land's open ground: the temperate plateau outside the forest's cores
#define AREA_FOREST         3       //...and its forest: inside them
#define AREA_MOUNTAIN       4       //the biomes, one area each, wherever theirs is
#define AREA_MEADOW         5       //a mountain meadow (TERRAIN_BIOME_POCKET)
#define AREA_SWAMP          6
#define AREA_DESERT         7
#define AREA_KINDS          8

#define AREA_FALL_REACH     6.0f    //a waterfall's ground: this far round its lip, or half its width if wider

struct Area{
    int kind = AREA_RIVER;
    int which = 0;              //for a river or a fall: the terrain's index of it
    std::vector<int> cells;     //the exploration's cells (Exploration::CellAt) its ground is in, ascending
    vec2 middle;                //its cell nearest the middle of them all: the overlay's post
};

struct AreaData{
    std::vector<Area> areas;
    float build_ms = 0.0f;
};

void BuildAreas(const GridPicker& picker, const Terrain& t, const ForestData& forest, const ExplorationSight& sight,
                AreaData& out);

const char* AreaKindName(int kind);     //"river", "forest": the tools' and the overlay's
const char* AreaKindTold(int kind);     //"a river", "the forest": the notice line's and the journal's
uint32_t AreaKindColour(int kind);      //the overlay's, 0xAARRGGBB as the grid view's lines take it

#endif

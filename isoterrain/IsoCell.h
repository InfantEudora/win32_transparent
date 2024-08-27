#ifndef _ISO_TERRAIN_CELL_H_
#define _ISO_TERRAIN_CELL_H_
#include "Object.h"
#include "type_int2.h"
#include "AssetManager.h"

/*
    It's a square cell in a larger grid.
    We should probably render the entire grid as a single mesh.
    Maybe later on we can construct it from this.
*/

#define CELL_TERRAIN_NONE    0
#define CELL_TERRAIN_EMPTY   1
#define CELL_TERRAIN_GRASS   2
#define CELL_TERRAIN_DIRT    3
#define CELL_TERRAIN_ROCK    4
#define CELL_TERRAIN_WATER   5

class IsoCell;

class IsoCell : public virtual Object{
public:
    IsoCell();
    ~IsoCell();

    int2 coordinate;
    int terrain_type = CELL_TERRAIN_NONE;
    void SetTerrainType(int newtype);
    void ApplyPreset(int preset);
};


#endif
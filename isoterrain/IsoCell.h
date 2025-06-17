#ifndef _ISO_TERRAIN_CELL_H_
#define _ISO_TERRAIN_CELL_H_
#include "Object.h"
#include "type_int3.h"
#include "AssetManager.h"
#include <map>
#include <array>

class IsoCell;
#include "IsoWall.h"
#include "IsoTerrain.h"

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

bool DirectionIsValid(int direction);

class IsoCell : public virtual Object{
public:
    IsoCell();
    ~IsoCell();

    int3 coordinate;
    int terrain_type = CELL_TERRAIN_NONE;
    void SetTerrainType(int newtype);
    void ApplyPreset(int preset);

    IsoTerrain* terrain = NULL;
    AssetManager* assetmanager = NULL;

    static std::map<int,int> terrain_material_map;

    // We may have one or more props. These are also our children.
    // Since children are generic objects, we can only add/remove those via the following functions.
    // TODO: Maybe lock adding via generic Add/Remove child functions somehow.

    std::array<int,4>wall_indices = {-1,-1,-1,-1};

    //We can have one prop smack bang in the middle.
    int prop_index = -1;

    IsoCell* GetNeighbour(int direction);

    IsoWall* PlaceWall(const std::string& asset_name,int direction);
    Object* PlaceTree(const std::string& asset_name);
    Object* PlaceStairs(const std::string& asset_name,int direction);
    Object* RaiseTerrain();
};


#endif
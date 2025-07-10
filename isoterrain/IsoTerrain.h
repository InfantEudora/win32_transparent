#ifndef _ISO_TERRAIN_H_
#define _ISO_TERRAIN_H_

#include "AssetManager.h"

/*
    It's a square cell in a larger grid.
    We should probably render the entire grid as a single mesh.
    Maybe later on we can construct it from this.

    You must be able to specify a list of assets that will be tilable together.

    I.e., a ground floor can be as simple as a square. And maybe that can fit 4 walls, multiple props.
    It might be able to tile next to another floor type, or maybe not...
*/

class IsoTerrain;
#include "IsoCell.h"

class IsoTerrain : public Object{
public:
    int width = -1; //Y
    int depth = -1; //X
    int height = -1;


    int cell_count = -1; //= width * depth * height
    //Settings
    vec3 center_offset = {};
    float height_factor = 0.5f;

    std::vector<IsoCell*> cells;     // An array of width*depth cells
    std::vector<IsoWall*> pillars;   // In the X-Y direction on extra for cells.
    std::string base_tile = "tile_floor.001";
    std::string wall_tile = "wall_full.001";

    AssetManager* assetmanager = NULL;  //Used to load assets from file.

    void CreateTerrain(int w, int d, int h);
    IsoCell* FindCellByWorldPosition(vec3& at);
    IsoCell* GetCellByCoordinate(int3 coord);

    void    GetPillarsByCellCoordinate(int3 cell_coord, std::array<IsoWall*,4>&walls);
    IsoWall* GetPillarByCoordinate(int3 coord);
};


#endif
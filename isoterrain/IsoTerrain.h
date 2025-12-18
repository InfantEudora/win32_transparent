#ifndef _ISO_TERRAIN_H_
#define _ISO_TERRAIN_H_

#include "AssetManager.h"
#include "RRandom.h"
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

    std::vector<IsoCell*>   cells;      // An array of width*depth cells. A quad.
    std::vector<IsoWall*>   walls;      // The edges per quad.
    std::vector<IsoWall*>   pillars;    // In the X-Y direction one extra for cells. The vertices of a quad
    std::vector<IsoStairs*> stairs;     //
    std::string base_tile = "tile_floor.001";
    std::string wall_tile = "wall_full.001";

    AssetManager* assetmanager = NULL;  //Used to load assets from file.
    PhysicsWorld* physicsworld = NULL;
    RRandom* randgen = NULL;

    void CreateTerrain(PhysicsWorld* physicsworld, RRandom* randgen, int w, int d, int h);
    void ClearUpdateCounts();
    IsoCell* FindCellByWorldPosition(vec3& at);
    IsoCell* GetCellByCoordinate(int3 coord);

    void    GetWallsByCellCoordinate(int3 cell_coord, std::array<IsoWall*,4>&walls);
    IsoWall* GetWallByCoordinate(int3 coord);

    void    GetPillarsByCellCoordinate(int3 cell_coord, std::array<IsoWall*,4>&walls);
    IsoWall* GetPillarByCoordinate(int3 coord);

    IsoStairs* GetStairsByCellCoordinate(int3 cell_coord);
    void PlaceStairs(IsoStairs* stairs, IsoCell* cell);

};


#endif
#ifndef _ISO_TERRAIN_H_
#define _ISO_TERRAIN_H_
#include "IsoCell.h"

/*
    It's a square cell in a larger grid.
    We should probably render the entire grid as a single mesh.
    Maybe later on we can construct it from this.
*/

class IsoTerrain;

class IsoTerrain : public Object{
public:
    int width = -1; //Y
    int depth = -1; //X

    int cell_count = -1; //= width * depth
    std::vector<IsoCell*> cells;     //An array of width*depth cells

    void CreateTerrain(int w, int d);

};


#endif
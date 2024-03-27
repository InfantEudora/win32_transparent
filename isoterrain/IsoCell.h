#ifndef _ISO_TERRAIN_CELL_H_
#define _ISO_TERRAIN_CELL_H_
#include "Object.h"

/*
    It's a square cell in a larger grid.
    We should probably render the entire grid as a single mesh.
    Maybe later on we can construct it from this.
*/

class IsoCell;

class IsoCell : public Object{
public:
   IsoCell();

};


#endif
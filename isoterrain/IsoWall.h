#ifndef _ISO_WALL_H_
#define _ISO_WALL_H_
#include "Object.h"
#include "type_int3.h"
#include "AssetManager.h"
#include <map>
#include <array>
#include "IsoDirection.h"

/*
    IsoWalls placed on IsoCells. There can be 4 walls on a single tile.
    When adjacent cells have adjacent walls, one of the cells own the wall.
*/

class IsoWall;
#include "IsoCell.h"

class IsoWall : public virtual Object{
public:
    IsoWall();
    ~IsoWall();

    //Reference to the cell its on
    IsoCell* cell = NULL;
    int direction = DIRECTION_NONE;

    void Lower();
    Object* PlaceStairs(int direction);
};


#endif
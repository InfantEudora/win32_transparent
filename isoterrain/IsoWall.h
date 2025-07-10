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

#define PILLAR_NONE             -1
#define PILLAR_UNINITIALISED    0
#define PILLAR_VALID            1

class IsoWall : public virtual Object{
public:
    IsoWall();
    ~IsoWall();

    //Reference to the cell its on
    IsoCell* cell = NULL;
    int direction = DIRECTION_NONE;
    int pillar = PILLAR_NONE;       // Pillar none means it a wall
    int3 coordinate = {};           // Pillar / Wall coordinates are identical?

    void Lower();
};


#endif
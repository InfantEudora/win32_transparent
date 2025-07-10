#ifndef _ISO_STAIRS_H_
#define _ISO_STAIRS_H_
#include "Object.h"
#include "type_int3.h"
#include "AssetManager.h"
#include <map>
#include <array>
#include "IsoDirection.h"

/*
    IsoStairs are a single object that can be placed on a cell. When a cell has stairs, you can move between different heights.ABC
*/

class IsoStairs;
#include "IsoCell.h"

class IsoStairs : public virtual Object{
public:
    IsoStairs();
    ~IsoStairs();

    //Reference to the cell its on
    IsoCell* cell = NULL;
    int direction = DIRECTION_NONE;
};


#endif
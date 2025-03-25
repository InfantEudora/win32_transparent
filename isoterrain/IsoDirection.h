#ifndef _ISO_DIRECTION_H_
#define _ISO_DIRECTION_H_

#include "type_vec3.h"
#include "AssetManager.h"
#include <map>
#include <array>
#include "IsoDirection.h"

#define DIRECTION_NONE      -1
#define DIRECTION_NORTH      0
#define DIRECTION_EAST       1
#define DIRECTION_SOUTH      2
#define DIRECTION_WEST       3

namespace IsoDirection{
    inline int NormalToDirection(vec3& normal){
        //Get the closest value of xyz
        if (normal.x > 0.8){
            return DIRECTION_EAST;
        }
        if (normal.x < -0.8){
            return DIRECTION_WEST;
        }
        if (normal.z > 0.8){
            return DIRECTION_SOUTH;
        }
        if (normal.z < -0.8){
            return DIRECTION_NORTH;
        }
        return DIRECTION_NONE;
    };

    inline bool DirectionIsValid(int direction){
        if ((direction > DIRECTION_NONE) && (direction <= DIRECTION_WEST)){
            return true;
        }
        return false;
    }
};

#endif
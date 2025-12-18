#ifndef _ISO_DIRECTION_H_
#define _ISO_DIRECTION_H_

#include "type_vec3.h"
#include "AssetManager.h"
#include <map>
#include <array>
#include <string>
#include "IsoDirection.h"

#define DIRECTION_NONE      -1
#define DIRECTION_NORTH      0
#define DIRECTION_EAST       1
#define DIRECTION_SOUTH      2
#define DIRECTION_WEST       3

namespace IsoDirection{
    inline int NormalToDirection(const vec3& normal){
        //Ignore Y component
        vec3 normal2 = vec3(normal.x, 0.0f, normal.z);
        normal2.normalize();
        float theta = atan2f(normal2.z, normal2.x);
        theta += TYPE_PI / 4; // Offset by 45 degrees to align directions
        //Convert to direction
        if (theta < 0) theta += 2 * TYPE_PI;
        int direction = (int)(theta / (TYPE_PI / 2)) % 4;
        if (direction == 0) return DIRECTION_EAST;
        if (direction == 1) return DIRECTION_SOUTH;
        if (direction == 2) return DIRECTION_WEST;
        if (direction == 3) return DIRECTION_NORTH;
        return DIRECTION_NONE;
    };

    inline bool DirectionIsValid(int direction){
        if ((direction > DIRECTION_NONE) && (direction <= DIRECTION_WEST)){
            return true;
        }
        return false;
    }

    inline std::string ToString(int direction){
        if (direction == DIRECTION_NORTH){
            return "North";
        }else if (direction == DIRECTION_EAST){
            return "East";
        }else if (direction == DIRECTION_SOUTH){
            return "South";
        }else if (direction == DIRECTION_WEST){
            return "West";
        }
        return "Invalid";
    }
};

#endif
#ifndef _TYPE_RAY_H_
#define _TYPE_RAY_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "type_plane.h"

struct ray;

struct ray{
    vec3    origin;
    vec3    direction;
    ray(){};

    bool    intersects_plane(const plane& p, vec3& at);   //Returns if the ray intersects specified plane, if so at is set.
};

//TODO
inline bool ray::intersects_plane(const plane& p, vec3& at){

    float d = p.normal.dot(-direction);
    //printf("Intersect test %.3f\n",d);

    if (abs(d) > FT_EPSILON){
        vec3 delta = p.pos - origin;
        float dist = delta.dot(p.normal) / d;
        //printf("Dist = %.3f\n",dist);


        vec3 result = -direction * dist;
        result += origin;
        at = result;

        return (dist >= 0);
    }

    return false;
}


#endif
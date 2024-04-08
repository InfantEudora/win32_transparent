#ifndef _TYPE_VERTEX_H_
#define _TYPE_VERTEX_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "type_vec2.h"
#include "type_vec3.h"

struct vertex;

struct vertex{
    vec3    pos;
    vec3    normal;
    vec2    uv;
    vec3    tangent;
    int32_t matid;
    vertex(){};
};

struct line{
    vec3 from;
    vec3 to;
    line(){};
};

#endif
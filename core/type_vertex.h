#ifndef _TYPE_VERTEX_H_
#define _TYPE_VERTEX_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "type_vec2.h"
#include "type_vec3.h"
#include "type_int3.h"

//Mush be matched with a vertex, to describe alternate positions and normals...
struct morph_vertex;

struct morph_vertex{
    vec3    pos;
    vec3    normal;
    morph_vertex(){};
};

struct vertex;

struct vertex{
    vec3    pos;
    vec3    normal;
    vec3    tangent;
    vec2    uv;
    int32_t matid;
    vertex(){};
};

struct skinned_vertex;

struct skinned_vertex{
    vec3    pos;
    vec3    normal;
    vec3    tangent;
    vec2    uv;
    int32_t matid;
    int3    bones;      // Bone ids that infuence this vertex
    vec3    weights;    // Bone weights

    skinned_vertex(){};
};

struct line{
    vec3 from;
    vec3 to;
    line(){};
};

#endif
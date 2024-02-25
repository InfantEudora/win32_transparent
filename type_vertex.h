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
    int32_t matid;

    vertex(){};
    vertex(float a,float b,float c,float d,float e,float f,float g,float h);
};

inline vertex::vertex(float a,float b,float c,float d,float e,float f,float g,float h){
    pos.set(a,b,c);
    normal.set(d,e,f);
    uv.set(g,h);
    matid = 0;
};

#endif
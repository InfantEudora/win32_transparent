#ifndef _TYPE_PLANE_H_
#define _TYPE_PLANE_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "type_vec3.h"

struct plane;

struct plane{
    vec3    pos;
    vec3    normal;
    plane(){};
};

#endif
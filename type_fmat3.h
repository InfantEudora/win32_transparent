#ifndef _TYPE_FMAT3_H_
#define _TYPE_FMAT3_H_
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct fmat3;
#include "type_vec3.h"

struct fmat3{
    fmat3(){};

	vec3 vertex[3];

    //Functions
    fmat3& identity();  //Set's this matrix to identity.
    fmat3& set_rotation(const vec3& axis, float angle);
};

inline fmat3& fmat3::identity(){
	vertex[0].x = 1;
	vertex[1].y = 1;
	vertex[2].z = 1;
	return *this;
}

//Set the matrix to be a rotation matrix around a specified axis
//Axis must be a normalised axis
inline fmat3& fmat3::set_rotation(const vec3& axis, float angle){
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

	vertex[0].x = oc * axis.x * axis.x + c;
	vertex[0].y = oc * axis.x * axis.y - axis.z * s;
	vertex[0].z = oc * axis.z * axis.x + axis.y * s;

	vertex[1].x = oc * axis.x * axis.y + axis.z * s;
	vertex[1].y = oc * axis.y * axis.y + c;
	vertex[1].z = oc * axis.y * axis.z - axis.x * s;

	vertex[2].x = oc * axis.z * axis.x - axis.y * s;
	vertex[2].y = oc * axis.y * axis.z + axis.x * s;
	vertex[2].z = oc * axis.z * axis.z + c;

	return *this;
}

#endif
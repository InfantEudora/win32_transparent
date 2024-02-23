#ifndef _TYPE_FMAT4_H_
#define _TYPE_FMAT4_H_
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct fmat4;
#include "type_fmat3.h"
#include "type_vec3.h"
#include "type_vec4.h"

struct fmat4{
    fmat4(){};

	vec4 vertex[4];

    //Functions
    fmat4& identity();  //Set's this matrix to identity.
    fmat4& rotationmatrix(const fmat3& mr);
    fmat4& rotationmatrix(const vec3& axis, float angle);
    fmat4& set_position(const vec3& pos);
};

inline fmat4& fmat4::identity(){
	vertex[0].x = 1;
	vertex[1].y = 1;
	vertex[2].z = 1;
    vertex[3].w = 1;
	return *this;
}

//Set this matrix to be a rotation matrix, the same as specified rotation matrix
inline fmat4& fmat4::rotationmatrix(const fmat3& mr){
	vertex[0].x = mr.vertex[0].x;
	vertex[0].y = mr.vertex[0].y;
	vertex[0].z = mr.vertex[0].z;
    vertex[0].w = 0;

	vertex[1].x = mr.vertex[1].x;
	vertex[1].y = mr.vertex[1].y;
	vertex[1].z = mr.vertex[1].z;
    vertex[1].w = 0;

	vertex[2].x = mr.vertex[2].x;
	vertex[2].y = mr.vertex[2].y;
	vertex[2].z = mr.vertex[2].z;
    vertex[2].w = 0;

    vertex[3].x = 0;
	vertex[3].y = 0;
	vertex[3].z = 0;
	vertex[3].w = 1;
	return *this;
}

//Set the matrix to be a rotation matrix around a specified axis
//Axis must be a normalized axis
inline fmat4& fmat4::rotationmatrix(const vec3& axis, float angle){
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

	vertex[0].x = oc * axis.x * axis.x + c;
	vertex[0].y = oc * axis.x * axis.y - axis.z * s;
	vertex[0].z = oc * axis.z * axis.x + axis.y * s;
    vertex[0].w = 0;

	vertex[1].x = oc * axis.x * axis.y + axis.z * s;
	vertex[1].y = oc * axis.y * axis.y + c;
	vertex[1].z = oc * axis.y * axis.z - axis.x * s;
    vertex[1].w = 0;

	vertex[2].x = oc * axis.z * axis.x - axis.y * s;
	vertex[2].y = oc * axis.y * axis.z + axis.x * s;
	vertex[2].z = oc * axis.z * axis.z + c;
    vertex[2].w = 0;

    vertex[3].x = 0;
	vertex[3].y = 0;
	vertex[3].z = 0;
	vertex[3].w = 1;
	return *this;
}

//Set only the position component of the matrix
inline fmat4& fmat4::set_position(const vec3& pos){
    vertex[3].x = pos.x;
	vertex[3].y = pos.y;
	vertex[3].z = pos.z;
	return *this;
}

#endif
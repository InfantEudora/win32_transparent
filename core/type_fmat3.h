#ifndef _TYPE_FMAT3_H_
#define _TYPE_FMAT3_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct fmat3;
#include "type_vec3.h"

/*
	Matrix defined as:
	 	[0] [1] [2]
	x
	y
	z
*/
struct fmat3{
    fmat3(){};

	vec3 vertex[3];

    //Functions
    void    print();
    fmat3&  identity();  //Set's this matrix to identity.
    fmat3&  set_rotation(const vec3& axis, float angle);
	fmat3 	transpose();
	float 	determinant();
};

inline fmat3& fmat3::identity(){
	vertex[0] = {1,0,0};
	vertex[1] = {0,1,0};
	vertex[2] = {0,0,1};
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

inline void fmat3::print(){
    printf("fmat3:\n");
    vertex[0].print();
    vertex[1].print();
    vertex[2].print();
}

//	The transpose of a rotation matrix is the same as the inverse.
inline fmat3 fmat3::transpose(){
	fmat3 m;
	m.vertex[0].x = vertex[0].x;
	m.vertex[0].y = vertex[1].x;
	m.vertex[0].z = vertex[2].x;
	m.vertex[1].x = vertex[0].y;
	m.vertex[1].y = vertex[1].y;
	m.vertex[1].z = vertex[2].y;
	m.vertex[2].x = vertex[0].z;
	m.vertex[2].y = vertex[1].z;
	m.vertex[2].z = vertex[2].z;
	return m;
}

inline float fmat3::determinant(){
	float a = vertex[0].x;
	float b = vertex[1].x;
	float c = vertex[2].x;
	float d = vertex[0].y;
	float e = vertex[1].y;
	float f = vertex[2].y;
	float g = vertex[0].z;
	float h = vertex[1].z;
	float i = vertex[2].z;
	return (a*e*i) + (b*f*g) + (c*d*h) - (c*e*g) - (b*d*i) - (a*f*h);
}

#endif
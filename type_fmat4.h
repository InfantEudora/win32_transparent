#ifndef _TYPE_FMAT4_H_
#define _TYPE_FMAT4_H_
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct fmat4;
#include "type_helpers.h"
#include "type_fmat3.h"
#include "type_vec3.h"
#include "type_vec4.h"

struct fmat4{
    fmat4(){};

	vec4 vertex[4];

    //Functions
    void    print();
    void    clear();
    fmat4&  identity();  //Set's this matrix to identity.
    fmat4&  rotationmatrix(const fmat3& mr);
    fmat4&  rotationmatrix(const vec3& axis, float angle);
    fmat4&  perspectivematrix(float fov, float aspect, float znear, float zfar);
    fmat4&  lookatmatrix(const vec3& pos, const vec3& target, const vec3& up);
    fmat4&  set_position(const vec3& pos);

    fmat4   operator*(const fmat4& rhs) const;
};

inline void fmat4::clear(){
    vertex[0].clear();
    vertex[1].clear();
    vertex[2].clear();
    vertex[3].clear();
}

inline fmat4& fmat4::identity(){
    clear();
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

//Makes this a perspective projection matrix
inline fmat4& fmat4::perspectivematrix(float fov, float aspect, float znear, float zfar){
	float f = tan(toradians(fov)/2);
	vertex[0].x = 1/(aspect*f);
    vertex[0].y = 0;
	vertex[0].z = 0;
	vertex[0].w = 0;

    vertex[1].x = 0;
	vertex[1].y = 1/f;
	vertex[1].z = 0;
	vertex[1].w = 0;

    vertex[2].x = 0;
    vertex[2].y = 0;
	vertex[2].z = (zfar + znear) / (znear - zfar);
	vertex[2].w = -1;

    vertex[3].x = 0;
    vertex[3].y = 0;
	vertex[3].z = (2 * zfar * znear)/(znear-zfar);
    vertex[3].w = 0;
	return *this;
}

//Up usually vec3 up = {0,1,0};
//Creates a lookat matrix at -Z
inline fmat4& fmat4::lookatmatrix(const vec3& pos, const vec3& target, const vec3& up){
	vec3 t = target - pos;
	vec3 f = t.normalize();
	t = f.cross(up);
	vec3 s = t.normalize();
	vec3 u = s.cross(f);

	vec3 r = {};
	r.x = -s.dot(pos);
	r.y = -u.dot(pos);
	r.z = f.dot(pos);

	vertex[0].x = s.x;
	vertex[1].x = s.y;
	vertex[2].x = s.z;

	vertex[0].y = u.x;
	vertex[1].y = u.y;
	vertex[2].y = u.z;

	vertex[0].z = -f.x;
	vertex[1].z = -f.y;
	vertex[2].z = -f.z;

    vertex[0].w = 0;
    vertex[1].w = 0;
    vertex[2].w = 0;

	vertex[3].x = r.x;
	vertex[3].y = r.y;
	vertex[3].z = r.z;
	vertex[3].w = 1;
	return *this;
}

inline fmat4 fmat4::operator*(const fmat4& rhs) const {
	fmat4 mat;
	mat.vertex[0].x = (vertex[0].x * rhs.vertex[0].x) + (vertex[0].y * rhs.vertex[1].x) + (vertex[0].z * rhs.vertex[2].x) + (vertex[0].w * rhs.vertex[3].x);
	mat.vertex[0].y = (vertex[0].x * rhs.vertex[0].y) + (vertex[0].y * rhs.vertex[1].y) + (vertex[0].z * rhs.vertex[2].y) + (vertex[0].w * rhs.vertex[3].y);
	mat.vertex[0].z = (vertex[0].x * rhs.vertex[0].z) + (vertex[0].y * rhs.vertex[1].z) + (vertex[0].z * rhs.vertex[2].z) + (vertex[0].w * rhs.vertex[3].z);
	mat.vertex[0].w = (vertex[0].x * rhs.vertex[0].w) + (vertex[0].y * rhs.vertex[1].w) + (vertex[0].z * rhs.vertex[2].w) + (vertex[0].w * rhs.vertex[3].w);

	mat.vertex[1].x = (vertex[1].x * rhs.vertex[0].x) + (vertex[1].y * rhs.vertex[1].x) + (vertex[1].z * rhs.vertex[2].x) + (vertex[1].w * rhs.vertex[3].x);
	mat.vertex[1].y = (vertex[1].x * rhs.vertex[0].y) + (vertex[1].y * rhs.vertex[1].y) + (vertex[1].z * rhs.vertex[2].y) + (vertex[1].w * rhs.vertex[3].y);
	mat.vertex[1].z = (vertex[1].x * rhs.vertex[0].z) + (vertex[1].y * rhs.vertex[1].z) + (vertex[1].z * rhs.vertex[2].z) + (vertex[1].w * rhs.vertex[3].z);
	mat.vertex[1].w = (vertex[1].x * rhs.vertex[0].w) + (vertex[1].y * rhs.vertex[1].w) + (vertex[1].z * rhs.vertex[2].w) + (vertex[1].w * rhs.vertex[3].w);

	mat.vertex[2].x = (vertex[2].x * rhs.vertex[0].x) + (vertex[2].y * rhs.vertex[1].x) + (vertex[2].z * rhs.vertex[2].x) + (vertex[2].w * rhs.vertex[3].x);
	mat.vertex[2].y = (vertex[2].x * rhs.vertex[0].y) + (vertex[2].y * rhs.vertex[1].y) + (vertex[2].z * rhs.vertex[2].y) + (vertex[2].w * rhs.vertex[3].y);
	mat.vertex[2].z = (vertex[2].x * rhs.vertex[0].z) + (vertex[2].y * rhs.vertex[1].z) + (vertex[2].z * rhs.vertex[2].z) + (vertex[2].w * rhs.vertex[3].z);
	mat.vertex[2].w = (vertex[2].x * rhs.vertex[0].w) + (vertex[2].y * rhs.vertex[1].w) + (vertex[2].z * rhs.vertex[2].w) + (vertex[2].w * rhs.vertex[3].w);

	mat.vertex[3].x = (vertex[3].x * rhs.vertex[0].x) + (vertex[3].y * rhs.vertex[1].x) + (vertex[3].z * rhs.vertex[2].x) + (vertex[3].w * rhs.vertex[3].x);
	mat.vertex[3].y = (vertex[3].x * rhs.vertex[0].y) + (vertex[3].y * rhs.vertex[1].y) + (vertex[3].z * rhs.vertex[2].y) + (vertex[3].w * rhs.vertex[3].y);
	mat.vertex[3].z = (vertex[3].x * rhs.vertex[0].z) + (vertex[3].y * rhs.vertex[1].z) + (vertex[3].z * rhs.vertex[2].z) + (vertex[3].w * rhs.vertex[3].z);
	mat.vertex[3].w = (vertex[3].x * rhs.vertex[0].w) + (vertex[3].y * rhs.vertex[1].w) + (vertex[3].z * rhs.vertex[2].w) + (vertex[3].w * rhs.vertex[3].w);
	return mat;
}

inline void fmat4::print(){
    printf("fmat4:\n");
    vertex[0].print();
    vertex[1].print();
    vertex[2].print();
    vertex[3].print();
}

#endif
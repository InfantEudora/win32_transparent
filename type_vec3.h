#ifndef _TYPE_VEC3_H_
#define _TYPE_VEC3_H_

#include <stdint.h>
#include <stdbool.h>
struct vec3;

struct vec3{
	float x;
	float y;
	float z;

	vec3() : x(0), y(0), z(0) {}
    vec3(float x, float y, float z) : x(x), y(y), z(z){}

    vec3&   set(float x, float y, float z);
    vec3&  	normalize();
};

inline vec3& vec3::set(float x, float y, float z) {
    this->x = x; this->y = y; this->z = z; return *this;
}
inline vec3& vec3::normalize(){
	double s = (x*x) + (y*y)  + (z*z);
	if (s <= 0){
		this->set(0,0,0);
		return *this;
	}
	s = sqrt(s);
	x/=s; y/=s;z/=s;
	return *this;
}

#endif
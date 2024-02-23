#ifndef _TYPE_VEC3_H_
#define _TYPE_VEC3_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct vec3;

struct vec3{
    union{
	    float x;
        float r;
    };
	union{
	    float y;
        float g;
    };
	union{
	    float z;
        float b;
    };

	vec3() : x(0), y(0), z(0) {}
    vec3(float x, float y, float z) : x(x), y(y), z(z){}

    void    print();
    vec3&   set(float x, float y, float z);
    vec3&  	normalize();

    vec3   	operator+(const vec3& rhs) const;    // add rhs
    vec3&  	operator+=(const vec3& rhs);         // add rhs and update this object
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

inline vec3 vec3::operator+(const vec3& rhs) const {
    return vec3(x+rhs.x, y+rhs.y, z+rhs.z);
}

inline vec3& vec3::operator+=(const vec3& rhs) {
    x += rhs.x; y += rhs.y; z += rhs.z; return *this;
}

inline void vec3::print(){
    printf("vec3: %7.2f | %7.2f | %7.2f\n",x,y,z);
}

#endif
#ifndef _TYPE_VEC4_H_
#define _TYPE_VEC4_H_

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

struct vec4;
#include "type_vec3.h"


struct vec4{
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
	union{
	    float w;
        float a;
    };

	vec4() : x(0), y(0), z(0), w(0) {}
    vec4(float x, float y, float z,float w) : x(x), y(y), z(z), w(w){}
    vec4(const vec3& a, float w) : x(a.x), y(a.y), z(a.z), w(w){};

    void    print();
    void    set(float x, float y, float z, float w);
    void    clear();
    float  	dot(const vec4& vec) const;                     // dot product
    vec4&  	normalize();
	vec3 	xyz() const;	//Returns the xyz components
};

inline void vec4::set(float x, float y, float z, float w){
    this->x = x; this->y = y; this->z = z; this->w = w;
}

inline void vec4::clear(){
    this->x = 0; this->y = 0; this->z = 0; this->w = 0;
}

inline vec4& vec4::normalize(){
	double s = (x*x) + (y*y)  + (z*z) + (w*w);
	if (s <= 0){
		this->set(0,0,0,0);
		return *this;
	}
	s = sqrt(s);
	x/=s; y/=s; z/=s; w/=s;
	return *this;
}

inline float vec4::dot(const vec4& rhs) const{
	double r = 0;
	r += (x*rhs.x);
	r += (y*rhs.y);
	r += (z*rhs.z);
    r += (w*rhs.w);
	return (float)r;
}

inline vec3 vec4::xyz() const{
	return vec3(x,y,z);
}

inline void vec4::print(){
    printf("vec4: %7.2f | %7.2f | %7.2f | %7.2f\n",x,y,z,w);
}

#endif
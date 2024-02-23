#ifndef _TYPE_VEC4_H_
#define _TYPE_VEC4_H_

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
struct vec4;

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

    void   set(float x, float y, float z, float w);
    void    clear();
    vec4&  	normalize();
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

#endif
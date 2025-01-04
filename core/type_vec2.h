#ifndef _TYPE_VEC2_H_
#define _TYPE_VEC2_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "type_helpers.h"
struct vec2;

struct vec2{
    union{
	    float x;
        float r;
    };
	union{
	    float y;
        float g;
    };

	vec2() : x(0), y(0) {}
    vec2(float f) : x(f), y(f){}
    vec2(float x, float y) : x(x), y(y){}

    void    print();
    vec2&   set(float x, float y);
    float   length() const;
	float   distance(const vec2& vec) const;                // distance between two vectors
    vec2&  	normalize();
	float  	dot(const vec2& vec) const;                     // dot product
    vec2&   floor();
    vec2    lerp(const vec2& b, float k);                   // Linear interpolation by factor k from this to b
    float   angle();                                        // Returns the direction in -PI ... PI interval
    vec2&   rotate(float theta);                            // Rotate by theta radians

    vec2   	operator-() const;                              // unary operator (negate)
    vec2   	operator-(const vec2& rhs) const;               // subtract rhs
    vec2&  	operator-=(const vec2& rhs);                    // subtract rhs and update this object
    vec2   	operator+(const vec2& rhs) const;               // add rhs
    vec2&  	operator+=(const vec2& rhs);                    // add rhs and update this object
    vec2  	operator*(const float scale) const;             // scale
    friend vec2 operator*(const float a, const vec2 vec);   // scale
};

inline vec2& vec2::set(float x, float y) {
    this->x = x; this->y = y; return *this;
}

inline float vec2::length() const {
    return sqrt(x*x + y*y);
}

inline float vec2::distance(const vec2& vec) const {
    return sqrt((vec.x-x)*(vec.x-x) + (vec.y-y)*(vec.y-y));
}

inline vec2& vec2::normalize(){
	double s = (x*x) + (y*y);
	if (s <= 0){
		this->set(0,0);
		return *this;
	}
	s = sqrt(s);
	x/=s; y/=s;
	return *this;
}

inline float vec2::dot(const vec2& rhs) const{
	double r = 0;
	r += (x*rhs.x);
	r += (y*rhs.y);
	return (float)r;
}

inline vec2& vec2::floor(){
	x = floorf(x);
    y = floorf(y);
	return *this;
}

inline vec2 vec2::operator-() const {
    return vec2(-x, -y);
}

inline vec2 vec2::operator-(const vec2& rhs) const {
    return vec2(x-rhs.x, y-rhs.y);
}

inline vec2& vec2::operator-=(const vec2& rhs) {
    x -= rhs.x; y -= rhs.y; return *this;
}

inline vec2 vec2::operator+(const vec2& rhs) const {
    return vec2(x+rhs.x, y+rhs.y);
}

inline vec2& vec2::operator+=(const vec2& rhs) {
    x += rhs.x; y += rhs.y; return *this;
}

inline vec2 vec2::operator*(const float a) const {
	return vec2(x*a, y*a);
}

inline vec2 operator*(const float a, const vec2 vec) {
    return vec2(a*vec.x, a*vec.y);
}

inline void vec2::print(){
    printf("vec2: %7.2f | %7.2f\n",x,y);
}

inline vec2 vec2::lerp(const vec2& b, float k){
    vec2 diff = b - *this ;
    k = clamp(k,0.0f,1.0f);
    return *this + (diff * k);
}

inline float vec2::angle(){
    return atan2(y,x);
}

inline vec2& vec2::rotate(float theta){
    float xt = x*cosf(theta) - y*sinf(theta);
    float yt = x*sinf(theta) + y*cosf(theta);
    x = xt;
    y = yt;
    return *this;
}

#endif
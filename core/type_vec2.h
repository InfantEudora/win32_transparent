#ifndef _TYPE_VEC2_H_
#define _TYPE_VEC2_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
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
    vec2(float x, float y) : x(x), y(y){}

    void    print();
    vec2&   set(float x, float y);
    float   length() const;
	float   distance(const vec2& vec) const;     // distance between two vectors
    vec2&  	normalize();
	float  	dot(const vec2& vec) const;          // dot product

    vec2   	operator-() const;                   // unary operator (negate)
    vec2   	operator-(const vec2& rhs) const;    // subtract rhs
    vec2&  	operator-=(const vec2& rhs);         // subtract rhs and update this object
    vec2   	operator+(const vec2& rhs) const;    // add rhs
    vec2&  	operator+=(const vec2& rhs);         // add rhs and update this object
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

inline void vec2::print(){
    printf("vec2: %7.2f | %7.2f\n",x,y);
}

#endif
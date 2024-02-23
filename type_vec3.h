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
    float   length() const;
	float   distance(const vec3& vec) const;     // distance between two vectors
    vec3&  	normalize();
	float  	dot(const vec3& vec) const;          // dot product
    vec3   	cross(const vec3& vec) const;        // cross product

    vec3   	operator-() const;                   // unary operator (negate)
    vec3   	operator-(const vec3& rhs) const;    // subtract rhs
    vec3&  	operator-=(const vec3& rhs);         // subtract rhs and update this object
    vec3   	operator+(const vec3& rhs) const;    // add rhs
    vec3&  	operator+=(const vec3& rhs);         // add rhs and update this object
};

inline vec3& vec3::set(float x, float y, float z) {
    this->x = x; this->y = y; this->z = z; return *this;
}

inline float vec3::length() const {
    return sqrt(x*x + y*y + z*z);
}

inline float vec3::distance(const vec3& vec) const {
    return sqrt((vec.x-x)*(vec.x-x) + (vec.y-y)*(vec.y-y) + (vec.z-z)*(vec.z-z));
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

inline float vec3::dot(const vec3& rhs) const{
	double r = 0;
	r += (x*rhs.x);
	r += (y*rhs.y);
	r += (z*rhs.z);
	return (float)r;
}

inline vec3 vec3::cross(const vec3& rhs) const {
    return vec3((y*rhs.z) - (z*rhs.y), (z*rhs.x) - (x*rhs.z), (x*rhs.y) - (y*rhs.x));
}

inline vec3 vec3::operator-() const {
    return vec3(-x, -y, -z);
}

inline vec3 vec3::operator-(const vec3& rhs) const {
    return vec3(x-rhs.x, y-rhs.y, z-rhs.z);
}

inline vec3& vec3::operator-=(const vec3& rhs) {
    x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this;
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
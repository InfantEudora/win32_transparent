#ifndef _TYPE_HELPERS_H_
#define _TYPE_HELPERS_H_

#include <random>

#define TYPE_PI 	3.14159265359f
const float FT_EPSILON = 0.0000000125f;
//The argument is parenthesised, as a function-like macro's must be: without it
//toradians(up - rest) expanded to up - (rest/180)*pi, and the pinball flippers got a 101-radian
//hinge limit out of a 64-degree one (2026-09-13). Every call in the repo passed a single
//identifier or literal, so this changes nothing but that class of bug.
#define toradians(a)	(((a)/180.0f)*TYPE_PI)
#define todegrees(a)    (((a)/TYPE_PI)*180.0f)

//Linear interpolation of a and b by factor k
float flerp(float a, float b, float k);

float clamp(float in, float min, float max);
float fract(float in);
float fround(float a); //Round to nearest

float fscurve3 (float a);
float fscurve5(float a);

float smoothstep(float a, float b, float k);
float fmap(float v, float a, float b, float x, float y);

template <class T>
const T &max(const T &a, const T &b) {
    return (a < b) ? b : a;
}

template <class T>
const T &min(const T &a, const T &b) {
    return (b < a) ? b : a;
}

int RandInt();
int RandInt(int imin, int imax);
float RandFloat(float fmin, float fmax);

#endif
#ifndef _TYPE_HELPERS_H_
#define _TYPE_HELPERS_H_

#include <random>

#define TYPE_PI 	3.14159265359f
const float FT_EPSILON = 0.0000000125f;
#define toradians(a)	((a/180.0f)*TYPE_PI)
#define todegrees(a)    ((a/TYPE_PI)*180.0f)

//Linear interpolation of a and b by factor k
float flerp(float a, float b, float k);
float clamp(float in, float min, float max);

template <class T>
const T &max(const T &a, const T &b) {
    return (a < b) ? b : a;
}

template <class T>
const T &min(const T &a, const T &b) {
    return (b < a) ? b : a;
}

#endif
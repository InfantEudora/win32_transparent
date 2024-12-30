#include "type_helpers.h"

//Linear interpolation from a to b where a or b can be smallest, any range.
float flerp(float a, float b, float k){
    k = clamp(k,0.0,1.0);
    float vmin = min(a,b);
    float vmax = max(a,b);
    float range = vmax - vmin;
    float d = range * k;
    return vmin + d;
}

//This can be faster, if we say alpha must be 0 ... 1
float flerp_fast(float n0, float n1, float a){
    return ((1.0 - a) * n0) + (a * n1);
}

//Maps value a which must be between 0 ... 1 onto a cubic scurve. Both s-curve's have a derivative of 0 at a= 0 and a=1
float fscurve3 (float a){
    return (a * a * (3.0 - 2.0 * a));
}

float fscurve5(float a){
    float a3 = a * a * a;
    float a4 = a3 * a;
    float a5 = a4 * a;
    return (6.0 * a5) - (15.0 * a4) + (10.0 * a3);
  }


// Clamps a float between min/max
float clamp(float in, float min, float max) {
    if (in > max) {
        return max;
    } else if (in < min) {
        return min;
    }
    return in;
}
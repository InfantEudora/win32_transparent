#include "type_helpers.h"

float flerp(float a, float b, float k){
    k = clamp(k,0.0,1.0);
    float vmin = min(a,b);
    float vmax = max(a,b);
    float range = vmax - vmin;
    float d = range * k;
    return vmin + d;
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
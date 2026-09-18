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

float fract(float in){
    return in - floorf(in);
}

//Swaps values of a and b
void swap(float& a, float&b){
    float t = a;
    a = b;
    b = t;
}

//Smooth step function between a and b by a factor k
//Normal smoothstep only operates on range 0 ... 1 for a and b, and 0.. 1 for k.
//This maps and reverses a/b
float smoothstep(float a, float b, float k) {
    if (b == a){
        return a;
    }
    bool swapped = false;
    if (a > b) {
        swapped = true;
        swap(b,a);
    }
    //Distance
    float d = b - a;
    k = fmap((d*k) + a,a,b,0,1);
    // Evaluate polynomial
    k = k * k * (3 - 2 * k);
    if (!swapped)
        return (d * k) + a;
    return (d * (1.0-k)) + a;
}

/*
    Maps a float value in between a - b onto range x - y
    i.e. -10 ... 10 --> 0 ... 1
    for input 0 results in 0.5 as output.
    Input is clamped between a and b.
    a/b and x/y resp. can be in any order but are sorted from small to large
*/
float fmap(float v, float a, float b, float x, float y) {
    // Sort output min to max
    float outscale = 0;
    if (y > x) {
        outscale = (y - x);
    } else if (x > y) {
        outscale = (x - y);
        float t = x;
        x = y;
        y = t;
    }
    if (outscale == 0) {
        // Always maps to both x and y since they are the same.
        return x;
    }

    float inscale = 0;

    if (b > a) {
        inscale = b - a;
        v = clamp(v, a, b);
    } else if (a > b) {
        inscale = a - b;
        v = clamp(v, b, a);
        float t = a;
        a = b;
        b = t;
    }

    if (inscale == 0) {
        // Always gets clamped to the same input value.
        v = a;
        // And assigned half output value.
        return x + (0.5f * outscale);
    }

    float fact = outscale / inscale;
    v -= a;
    v *= fact;
    v += x;
    return v;
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

//Returns a random int between INT_MIN and INT_MAX
int RandInt(){
    return rand();
}

//Returns a random integer between min and max
int RandInt(int imin, int imax){
    if (imin >= imax){
        return min(imin,imax);
    }
    int dist = imax - imin;
    int r = abs(RandInt());
    r = r % (dist+1);
    return imin + r;
}

//Random float that does not need to be repeatable.
float RandFloat(float fmin, float fmax){
    //Size so that floats can be gotten in range 0 - 1/size
    float size = 1000;
    if (fmin >= fmax){
        return min(fmin,fmax);
    }
    float res = RandInt(fmin * size,fmax * size);
    res /= size;
    return res;
}
#ifndef _PERLIN_NOISE_H_
#define _PERLIN_NOISE_H_

#include "type_vec2.h"

class PerlinNoise;

class PerlinNoise{
public:
    PerlinNoise();
    ~PerlinNoise();

    float GetValue2D(float x, float y);

    float frequency = 0.1f;
    float lacunarity = 2.0f;
    int num_octaves = 5;
    float persistence = 0.5f;
    int seed = 0;
    float offset = 128;
    float scale = 1;
    vec2 coord;

};


#endif
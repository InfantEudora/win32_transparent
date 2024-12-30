#ifndef _WORLEY_NOISE_H_
#define _WORLEY_NOISE_H_

#include "type_vec2.h"

class WorleyNoise;

class WorleyNoise{
public:
    WorleyNoise();
    ~WorleyNoise();

    float GetValue2D(float x, float y);

    int grid_size = 24;
    float scale = 16.0f;

};


#endif
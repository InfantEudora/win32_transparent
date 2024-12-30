#include "WorleyNoise.h"
#include "type_helpers.h"
#include "type_vec3.h"

WorleyNoise::WorleyNoise(){

};

WorleyNoise::~WorleyNoise(){

};

//From https://www.shadertoy.com/view/4djSRW
//Returns noise for inputs > 1
float hash12(vec2 p){
    vec3 p3 = {p.x,p.y,p.x};
    p3 = p3 * 0.1031;
    p3.fract();
    p3 += p3.dot(vec3(p3.y,p3.z,p3.x) + vec3(33.33));
    return fract((p3.x + p3.y) * p3.z);
}

float WorleyNoise::GetValue2D(float x, float y){
    float value = 0.0f;
    vec2 inp = {x,y};
    vec2 p = inp * (1.0/grid_size);

    vec2 cell = p;
    cell.floor();

    value = (cell-p).length();

    if ((int)x % grid_size == 0){
        value += 255;
    }
    if ((int)y % grid_size == 0){
        value += 255;
    }

    //value = hash12(cell);

    vec2 worley_dif = vec2(hash12(cell)) + cell - p;
    float dist = worley_dif.length();
    value = dist;


    value *= scale;
    value = clamp(value,0,255);
    return value;
}
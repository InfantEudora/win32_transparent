#version 450 core
/*
    A flake (chasm_snow.vert): a soft round spot, in the light of the sun as it shines over a cool sky's share
    (ApplicationChasmWeather.cpp, SetSnowUniforms). Not lit through lighting.glsl: a flake is too small to have a side
    in shadow, and the sun's colour is what makes it read as morning or dusk.
*/

layout (location = 0) out vec4 color;

layout (location = 0) in vec2 vcorner;
layout (location = 1) in float valpha;

uniform vec3 snow_light = vec3(0.9);

void main(){
    float r2 = dot(vcorner,vcorner);
    float a = valpha * clamp(1.0 - r2,0.0,1.0);
    color = vec4(snow_light,a * (2.0 - a));
}

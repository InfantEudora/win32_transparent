#version 460 core
/*
    The wind streaks' fragment stage: a pale, soft-edged ribbon. The alpha along it (fading in at
    the tail, out at the tip, with the streak's life and the local wind) arrives per vertex; this
    only rounds it off across the width, so a ribbon two or three pixels wide reads as a wisp and
    not as a line.
*/
layout (location = 0) in vec2 vuv;
layout (location = 1) in float valpha;

layout (location = 0) out vec4 color;

uniform vec3 streak_color = vec3(0.90,0.95,1.0);

void main(){
    float across = 1.0 - vuv.y * vuv.y;
    color = vec4(streak_color,valpha * across);
}

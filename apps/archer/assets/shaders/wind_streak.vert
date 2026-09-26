#version 460 core
/*
    The wind streaks' vertex stage - apps/archer/Streaks.h builds the ribbons, already in world
    space and already facing the camera, so this only projects. What the fragment stage needs rides
    in the ordinary vertex slots: uv is (along 0..1, across -1..1) and normal.x is the alpha.
*/
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 3) in vec2 uv;

layout (location = 0) uniform mat4 mat_worldcam = mat4(1.0);

layout (location = 0) out vec2 vuv;
layout (location = 1) out float valpha;

void main(){
    vuv = uv;
    valpha = normal.x;
    gl_Position = mat_worldcam * vec4(position,1.0);
}

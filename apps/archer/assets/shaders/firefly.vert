#version 460 core
/*
    The fireflies' vertex stage - apps/archer/Fireflies.h builds each glow as a quad already in
    world space and facing the eye, so this only projects. uv is -1..1 across the quad and
    normal.x carries the fly's brightness.
*/
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 3) in vec2 uv;

layout (location = 0) uniform mat4 mat_worldcam = mat4(1.0);

layout (location = 0) out vec2 vuv;
layout (location = 1) out float vbright;

void main(){
    vuv = uv;
    vbright = normal.x;
    gl_Position = mat_worldcam * vec4(position,1.0);
}

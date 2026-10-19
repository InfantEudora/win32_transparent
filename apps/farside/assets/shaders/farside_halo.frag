#version 430 core
/*
    A planet's atmosphere seen from outside: the glow round its limb. Drawn on a shell a little
    larger than the planet, BACK faces only and without writing depth (SetHaloUniforms sets that
    state), so it shows only beyond the planet's edge - the planet's own depth hides the shell
    behind it - and never lies over the surface. The rim tint ON the surface is the planet
    shader's.

    How far a pixel is out from the limb comes from the shell's normal alone: for a view ray that
    leaves the shell through this back face, the ray's closest approach to the centre, over the
    shell's radius, is sqrt(1 - (N.V)^2). That is exact under perspective and needs no centre, so
    the shader has nothing per body to be told.
*/
layout (location = 0) out vec4 color;

layout (location = 0) in vec3 vposition;
layout (location = 1) in vec3 vnormal;

uniform vec3 eye_position;
uniform vec3 sun_direction = vec3(0.0,1.0,0.0);    //toward the sun
uniform float inner = 0.95;                         //the planet's radius over the shell's
uniform vec3 atmosphere_colour = vec3(0.35,0.62,1.0);
uniform float strength = 1.0;

void main(){
    vec3 N = normalize(vnormal);
    vec3 V = normalize(eye_position - vposition);
    float nv = dot(N,V);
    float closest = sqrt(max(1.0 - nv * nv,0.0));
    //0 at the planet's limb, 1 at the shell's edge.
    float t = clamp((closest - inner) / max(1.0 - inner,1e-4),0.0,1.0);
    float glow = pow(1.0 - t,2.5);

    //Brighter on the sunlit side: which way this pixel lies from the centre, on screen, against
    //the sun.
    vec3 across = N - V * nv;
    float l = length(across);
    float day = (l > 1e-4) ? smoothstep(-0.35,0.5,dot(across / l,normalize(sun_direction))) : 0.5;

    float a = glow * strength * (0.06 + 0.94 * day);
    color = vec4(atmosphere_colour,clamp(a,0.0,1.0));
}

#version 430 core
#include "texture_units.glsl"
/*
    The play mode's GHOST (ApplicationChasmGhost.cpp): what the tool in hand would place, under the
    cursor - see-through, lit like any surface so it reads as a shape, tinted green where it can be
    placed and red where it cannot. The outline pass draws its edge in the same colour.
*/

layout (location = 0) out vec4 color;

//default.vert's varyings, at its locations - lighting.glsl reads them.
layout (location = 0)  in vec3 vposition;
layout (location = 1)  in vec3 vnormal;
layout (location = 2)  in vec2 vuv;
layout (location = 3)  in mat3 TBN;
layout (location = 6)  flat in int vmatindex;
layout (location = 7)  flat in int vobjid;
layout (location = 8)  in vec4 vshadow;

#include "lighting.glsl"

uniform vec3  ghost_tint = vec3(0.45,0.95,0.45);
//A road being drawn is green and red plot by plot: a refused plot's vertices carry a negative u.
uniform vec3  ghost_refused = vec3(1.0,0.35,0.30);
uniform float ghost_alpha = 0.45;
//Self-lit share, so a ghost in shade is still the tool's colour.
uniform float ghost_fill = 0.35;

void main(){
    vec3 tint = (vuv.x < 0.0) ? ghost_refused : ghost_tint;
    vec3 albedo = mix(vec3(0.92),tint,0.65);
    color = vec4(LightSurface(albedo) + albedo * ghost_fill,ghost_alpha);
}

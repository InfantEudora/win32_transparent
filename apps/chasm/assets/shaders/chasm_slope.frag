#version 430 core
#include "texture_units.glsl"
/*
    The play mode's SLOPE GRID (ApplicationChasmGhost.cpp): round the ghost, the plots or field's cells the
    tool in hand could take, each outlined green where the land lets it stand, amber where it stands only on
    a foundation, and red where it does not - too steep, mostly, which the ground does not show. A vertex's
    u below zero is refused, past one on a foundation; its v is how strongly it shows, 0 to 1: the fade with
    distance from the cursor, and an amber or red tile's fill a share of it.
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

uniform vec3  slope_ok = vec3(0.45,0.95,0.45);
uniform vec3  slope_refused = vec3(1.0,0.35,0.30);
uniform vec3  slope_foundation = vec3(1.0,0.72,0.22);
uniform float slope_alpha = 0.8;
//Self-lit share, as the ghost's: a line in shade is still the tool's colour.
uniform float slope_fill = 0.35;

void main(){
    vec3 tint = (vuv.x < 0.0) ? slope_refused : (vuv.x > 1.0) ? slope_foundation : slope_ok;
    vec3 albedo = mix(vec3(0.92),tint,0.65);
    color = vec4(LightSurface(albedo) + albedo * slope_fill,slope_alpha * clamp(vuv.y,0.0,1.0));
}

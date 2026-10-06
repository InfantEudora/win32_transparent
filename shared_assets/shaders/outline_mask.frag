#version 460 core
//The engine-wide texture unit map. Mirrored in C++ by core/TextureUnits.h.
#include "texture_units.glsl"

/*
    Renderer::OutlinePass, step 1: what the mask holds at this pixel - which outlined object (its
    index + 1, so 0 is none), whether the scene HIDES it here, and its outline colour packed.

    HIDDEN is decided against the G-buffer: the scene's nearest surface at this pixel, as a world
    position, against this fragment's. Distances from the eye rather than depth values, with a
    little slack, because an outline-only copy of part of a merged mesh lies exactly ON the scene's
    surface and must count as in front of it, not flicker behind it. Without a G-buffer (a pipeline
    that has none) nothing is hidden.
*/

layout (location = 0) in vec3 world_position;

layout (location = 0) out ivec4 mask;     //RGBA32I: glad here has the signed clear, not the unsigned

layout (binding = TEXUNIT_GBUFFER_DEPTH) uniform sampler2D gbuffer_depth;
layout (binding = TEXUNIT_GBUFFER_POSITION) uniform sampler2D gbuffer_position;

uniform vec3 eye_position;
uniform int f_gbuffer = 0;
uniform int outline_id = 0;         //an int: Shader has no uint setter
uniform vec4 outline_color = vec4(1.0);

void main(){
    int hidden = 0;
    if (f_gbuffer != 0){
        ivec2 p = ivec2(gl_FragCoord.xy);
        //1.0 is the cleared depth: nothing drawn here, so nothing in front.
        if (texelFetch(gbuffer_depth,p,0).r < 1.0){
            float scene = distance(texelFetch(gbuffer_position,p,0).xyz,eye_position);
            float here = distance(world_position,eye_position);
            if (here > scene * 1.003 + 0.03){
                hidden = 1;
            }
        }
    }
    //The packed colour's bits go through int unchanged (GLSL's uint-to-int keeps the bit pattern).
    mask = ivec4(outline_id,hidden,int(packUnorm4x8(outline_color)),0);
}

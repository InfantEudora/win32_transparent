#version 430 core
//The engine-wide texture unit map - every layout(binding = ...) below names an entry in
//it rather than a number of its own. Mirrored in C++ by core/TextureUnits.h.
#include "texture_units.glsl"

/*
    SSAO, stage 3: puts the blurred occlusion onto the frame - see Renderer::CompositeSSAO.

    Outputs the multiplier and nothing else; the blend does the multiply (dst * src for colour,
    alpha left alone), so this draws over the MSAA target with no copy of it and every sample of
    an edge pixel is darkened by the same amount. Drawn after the solid and skinned passes and
    BEFORE the custom-material pass, so the occlusion lands on the solid scene only: a firefly or a
    cloud drifting in front of a crease is not darkened by a crease it has nothing to do with.

    It darkens the whole lit colour, not just an ambient term - default.frag's ambient is 0.1 of the
    albedo, so occlusion applied only there would be all but invisible. That is a choice about this
    engine's lighting, not a law; if an ambient term with some weight ever arrives, it is the place
    for this.

    TEXUNIT_LOWRES_COMPOSITE, which is the engine's "full-screen pass" unit: the low-res composite
    and the upscale use it too, each binding before it draws, and none of the three overlap.
*/

layout (location = 0) out vec4 color;

layout (binding = TEXUNIT_LOWRES_COMPOSITE) uniform sampler2D ao_texture;

void main(){
    //gl_FragCoord is in render pixels, and so is the AO image - a texelFetch, so there is no
    //filter or half-texel offset to get wrong.
    float ao = texelFetch(ao_texture, ivec2(gl_FragCoord.xy), 0).r;
    color = vec4(vec3(ao), 1.0);
}

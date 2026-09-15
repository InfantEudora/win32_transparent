#version 430 core

/*
    The upscale half of Renderer::CompositeLowRes: takes the reduced-resolution custom-shader
    target and puts it back over the frame, one solid NxN block per low-res pixel.

    --- WHY THIS IS A NEAREST FETCH AND NOT A FILTER ---------------------------------------------
    Everywhere else in graphics, drawing a half-resolution effect back at full size is a problem to
    be hidden - bilinear at minimum, usually a depth-aware or bilateral upsample so the effect does
    not bleed across the edges of the geometry it sits behind. This does none of that, ON PURPOSE.
    The blocks ARE the look: Renderer::SetCustomShaderScale is one number meaning both "render at
    1/N" and "show as NxN pixels", and an app opts in because it wants a pixelated effect and gets
    the speed as the same decision. Filtering here would spend the saving on hiding the thing it
    was spent to buy.

    What that costs, stated plainly so nobody has to rediscover it: the low-res shader resolved its
    own occlusion against the full-resolution G-buffer, but it did so once per BLOCK. So the effect
    overlaps the silhouette of the geometry in front of it by up to N-1 window pixels. Against a
    pixel-art look that reads as the art; against a photographic one it reads as a halo, and the
    fix there is a bilateral upsample, not a smaller N.

    --- PREMULTIPLIED ----------------------------------------------------------------------------
    The target was drawn into with glBlendFuncSeparate(...,GL_ONE,GL_ONE_MINUS_SRC_ALPHA) over a
    cleared buffer (see Renderer::CustomShaderPass), so a texel here is colour ALREADY multiplied
    by its own coverage, with the accumulated coverage in alpha. CompositeLowRes blends with
    GL_ONE/GL_ONE_MINUS_SRC_ALPHA to match, so the texel goes out untouched - no divide by alpha,
    no multiply. Dividing alpha back out here and blending the ordinary way would be the same
    picture everywhere except where alpha is near zero, which is the whole soft edge of a volume.
*/

layout (location = 0) out vec4 color;

layout (binding = 28) uniform sampler2D lowres_texture;   //TEXUNIT_LOWRES_COMPOSITE in Renderer.h

//Window pixels across one low-res pixel. Matches Renderer::lowres_scale, and is what turns a
//window coordinate into the block that was drawn for it.
uniform int lowres_scale = 2;

void main(){
    /*
        The block this window pixel belongs to, as an exact integer.

        texelFetch rather than texture(): an integer index cannot land between two texels, so
        there is no half-texel offset to get wrong and no dependence on the sampler's filter
        state. It is also the reason the vertex stage interpolates nothing - gl_FragCoord is
        already the number this needs.

        This assumes the low-res viewport's origin is the full-res one divided by the scale, which
        Renderer::CustomShaderPass sets up and which is exact whenever the viewport origin is a
        multiple of the scale. The one case that is not - an offset viewport, which only Tank uses
        and which has no custom shaders - lands one block out rather than wrong.
    */
    ivec2 block = ivec2(gl_FragCoord.xy) / lowres_scale;
    //Nothing should be able to land outside, since the target is sized by rounding the window UP.
    //The clamp is here so that a future viewport arrangement that does get this wrong shows as a
    //smeared edge column rather than as an undefined fetch.
    color = texelFetch(lowres_texture,min(block,textureSize(lowres_texture,0) - 1),0);
}

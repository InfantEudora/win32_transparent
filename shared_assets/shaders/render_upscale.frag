#version 430 core
//The engine-wide texture unit map - every layout(binding = ...) below names an entry in
//it rather than a number of its own. Mirrored in C++ by core/TextureUnits.h.
#include "texture_units.glsl"


/*
    Renderer::UpscaleFrame: the whole resolved scene, drawn at 1/render_scale of the window, put
    back over the window with one of three filters. See Renderer::SetRenderScale.

    EVERY FILTER IS DONE BY HAND, WITH texelFetch. The sampler's own LINEAR would give bilinear for
    free, but it would have to be switched on resolve_tex_id, which other passes read expecting
    NEAREST - and bicubic has to fetch the texels itself anyway. This way the three modes are one
    code path and depend on no sampler state.

    --- WHERE A WINDOW PIXEL FALLS IN THE SCENE --------------------------------------------------
    gl_FragCoord.xy / render_scale, in texels. Not gl_FragCoord / window size * scene size: the scene
    was sized by rounding the window UP, so the two ratios differ by a fraction of a texel, and the
    exact integer ratio is what keeps nearest's blocks square and lined up with the texels the scene
    passes actually drew (their viewport origin is also divided by render_scale).
*/

layout (location = 0) out vec4 color;

layout (binding = TEXUNIT_LOWRES_COMPOSITE) uniform sampler2D scene_texture;

uniform int render_scale = 2;
//Renderer::upscale_filter_t: 0 nearest, 1 bilinear, 2 bicubic.
uniform int upscale_filter = 0;

//Clamped, so the filters' outer taps at the frame's edge repeat the edge instead of fetching
//outside the texture, which is undefined.
vec4 Fetch(ivec2 p){
    return texelFetch(scene_texture,clamp(p,ivec2(0),textureSize(scene_texture,0) - 1),0);
}

//Catmull-Rom weights for the four taps around a sample, t being its position between the middle two.
vec4 CatmullRom(float t){
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4(-0.5 * t3 +       t2 - 0.5 * t,
                 1.5 * t3 - 2.5 * t2 + 1.0,
                -1.5 * t3 + 2.0 * t2 + 0.5 * t,
                 0.5 * t3 - 0.5 * t2);
}

void main(){
    vec2 src = gl_FragCoord.xy / float(render_scale);
    if (upscale_filter == 0){
        //The texel this pixel is part of - an NxN block per texel, nothing blended.
        color = Fetch(ivec2(src));
        return;
    }
    //Texel CENTRES are at +0.5, so the four texels around a point and its weights come from the
    //point moved back by half a texel.
    vec2 p = src - 0.5;
    ivec2 base = ivec2(floor(p));
    vec2 f = p - vec2(base);
    if (upscale_filter == 1){
        vec4 bottom = mix(Fetch(base),Fetch(base + ivec2(1,0)),f.x);
        vec4 top    = mix(Fetch(base + ivec2(0,1)),Fetch(base + ivec2(1,1)),f.x);
        color = mix(bottom,top,f.y);
        return;
    }
    vec4 wx = CatmullRom(f.x);
    vec4 wy = CatmullRom(f.y);
    vec4 sum = vec4(0.0);
    for (int j = 0; j < 4; j++){
        vec4 row = vec4(0.0);
        for (int i = 0; i < 4; i++){
            row += wx[i] * Fetch(base + ivec2(i - 1,j - 1));
        }
        sum += wy[j] * row;
    }
    //Catmull-Rom's negative lobes undershoot next to a hard edge. The colour buffer is HDR, so
    //only below zero is wrong; alpha is a coverage and goes back to 0..1.
    color = vec4(max(sum.rgb,vec3(0.0)),clamp(sum.a,0.0,1.0));
}

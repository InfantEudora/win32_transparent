#version 460 core

/*
    gbuffer - not an effect, a DIAGNOSTIC. Shows what the deferred pass actually left behind for a
    custom shader to composite against.

    Turn the test geometry on (the Effect panel's checkbox, or fx_scene) and this draws the cube
    and the ground plane out of the G-buffer alone. With it off, everything reads "nothing here",
    which is exactly the state that makes a depth-aware effect look broken - so this is the first
    thing to check before concluding that one is.

    It exists because the G-buffer is the part of the custom-material contract that is easiest to
    get wrong and hardest to see. Read the block at the top of shaders/engine.glsl, then look at
    `view = 2` here: position is a perfectly plausible-looking picture in the sky as well as on the
    geometry, because its clear value is a legal world coordinate. Depth is the only channel whose
    clear value (1.0) is outside the range anything real can occupy, and so the only one that
    answers "is there anything at this pixel".
*/
#include "engine.glsl"

//0 depth, 1 normal, 2 position, 3 the mask on its own.
uniform int   view        = 0;
uniform float depth_gamma = 60.0;   //depth is nearly all 1.0 at these ranges; this spreads it out

void main(){
    /*
        vuv, not gl_FragCoord/iResolution.

        The G-buffer textures are sized to the whole WINDOW while the 3D viewport may be a
        sub-rectangle of it (Renderer::viewport_x/_y - Tank uses one). This app never offsets its
        viewport, so the two agree here; in an app that does, sampling the G-buffer wants the
        window-relative coordinate and this is the line that would need saying differently. Worth
        knowing before pasting a depth-aware effect into Tank and finding it offset.
    */
    float scene_depth = texture(gbuffer_depth,vuv).r;
    bool f_hit = scene_depth < 1.0;

    vec3 color;
    if (view == 3){
        color = f_hit ? vec3(0.15,0.85,0.35) : vec3(0.10,0.10,0.14);
    }else if (!f_hit){
        //A visible "nothing was drawn here", rather than black - black looks like a bug and this
        //is the state the whole file is about.
        vec2 g = step(0.5,fract(vuv * vec2(iResolution.x,iResolution.y) / 32.0));
        float check = abs(g.x - g.y);
        color = mix(vec3(0.055,0.06,0.085),vec3(0.085,0.09,0.125),check);
    }else if (view == 1){
        color = texture(gbuffer_normal,vuv).xyz * 0.5 + 0.5;
    }else if (view == 2){
        //Wrapped, so the sign and the magnitude of a world coordinate are both readable.
        color = fract(abs(texture(gbuffer_position,vuv).xyz));
    }else{
        //Non-linear depth spends almost its whole range in the first few units, so a plain
        //display of it is a white screen with a hint of grey. Pushing it through a power is the
        //cheap way to see the shape; it is a diagnostic, not a measurement.
        color = vec3(pow(clamp(scene_depth,0.0,1.0),depth_gamma));
    }

    frag_color = vec4(color,1.0);
}

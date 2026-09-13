#version 460 core

/*
    Copy this to start a shadertoy port. Paste the snippet between the two includes, unchanged.

    THE SHADERTOY FORM. The app compiles whatever file you picked and has no notion of "kinds" -
    no C++ text splicing and no line-number fixups - so the only difference between this and a
    native effect is which files it includes. That is why every file here is complete and says so
    itself.

    What you get from shaders/shadertoy.glsl: iResolution, iTime, iTimeDelta, iFrame, iFrameRate,
    iMouse, iDate, iChannel0..3, iChannelResolution[4], iChannelTime[4].

    What you do NOT get, and what to do about it:
      - Multiple buffers (Buf A..D) and feedback. There is one pass here. An effect needing
        several is a job for the engine's own render targets, not for this file.
      - Cubemap and video channels. iChannel* are 2D textures: the seeded noise by default, or
        anything dropped in assets/textures/ and picked from the Effect panel.
      - mainSound, keyboard and microphone channels.

    If the snippet also wants the scene behind it - depth, the camera, the real lights - add
    `#include "engine.glsl"` below the first include and read the block at the top of that file
    before touching the G-buffer.

    Every `uniform` you add with a default becomes a slider in the Effect panel automatically, so
    a knob costs one line of GLSL and no C++. See uniform `hue_shift` below.
*/
#include "shadertoy.glsl"

//A knob. Its default is read back out of the linked program, so this number is what the panel
//and fx_state show until something changes it.
uniform float hue_shift = 0.0;

// ---- paste between the two includes, unchanged -----------------------------------------------
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 uv = fragCoord / iResolution.xy;

    //The stock shadertoy "new shader" gradient, plus one knob so the panel has something in it
    //and the seeded noise so iChannel0 is exercised on a fresh install.
    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0,2,4) + hue_shift);

    //texture(), not texelFetch(): the noise is 256x256 and this samples it at screen scale.
    float grain = texture(iChannel0,uv * 4.0).r;
    col = mix(col,col * (0.85 + 0.3 * grain),0.25);

    fragColor = vec4(col,1.0);
}
// ----------------------------------------------------------------------------------------------

#include "shadertoy_main.glsl"

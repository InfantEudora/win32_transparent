/*
    The prelude: everything shadertoy declares for you, declared here instead.

    Included by both forms of effect - a pasted snippet includes this and shadertoy_main.glsl, a
    native effect includes engine.glsl, which includes this. So this file is where every name the
    BENCH owns lives, and shaders/engine.glsl is where the names the ENGINE owns live.

    NO #version HERE. It has to stay the first line of the file doing the including - see
    Shader::ResolveIncludes in core/Shader.cpp, which documents that and also explains the
    `#line 1 <n>` wrappers that keep compile errors pointing at the right file.

    Everything below is set by ApplicationTestFX::SetEffectUniforms once per frame, on the render
    thread, with this program bound. A uniform an effect does not use is stripped by the GLSL
    compiler and the setter then warns once on stderr; that is the ordinary case, not a fault.
*/

//Guarded because engine.glsl includes this too, and an effect that needs both the shadertoy
//names and the scene's G-buffer includes both. Declaring vuv twice is a compile error, and
//"include what you need, in any order" is worth more than three lines.
#ifndef FX_SHADERTOY_GLSL
#define FX_SHADERTOY_GLSL

//From shaders/fullscreen.vert: 0..1 across the viewport, y UP.
layout (location = 0) in vec2 vuv;

/*
    The colour out.

    Declared here rather than in shadertoy_main.glsl so that a NATIVE effect - one writing its own
    main() and never including the epilogue - has it too. Which means: do not declare your own
    output at location 0. Write to frag_color.
*/
layout (location = 0) out vec4 frag_color;

//--- the shadertoy uniforms -------------------------------------------------------------------
//Viewport size in pixels; z is shadertoy's pixel aspect ratio, which is 1 on anything that is not
//a CRT.
uniform vec3  iResolution;

/*
    TIME COMES FROM THE SIMULATION TICK, not from a wall clock: iTime is the tick counter times
    the fixed timestep. So sim_pause freezes the effect, sim_step advances it by an exact number
    of frames, and a screenshot of frame N is reproducible. iTimeDelta is that timestep and is
    constant by construction - the engine never varies it (see Application::GetPhysicsTimestep).
*/
uniform float iTime;
uniform float iTimeDelta;
uniform float iFrameRate;
uniform int   iFrame;

/*
    Mouse, shadertoy's convention, in viewport pixels with y up:
      xy  current position while the button is held, and the last one otherwise
      zw  where the button went down, NEGATED while the button is up
*/
uniform vec4  iMouse;

//Declared so a snippet using it compiles, and fed from the TICK rather than the calendar: w is
//iTime and the date components are zero. A bench whose claim is "frame N looks like this" must
//not contain a value that changes at midnight.
uniform vec4  iDate;

/*
    The four channels, on fixed texture units.

    28-31 because units 0-3 are the shadow map and the G-buffer, 4-23 are material textures, 24 is
    the skybox cubemap and 25-27 are the app-reserved, cloud-shadow and occluder-field maps - see
    the TEXUNIT_* defines at the top of core/Renderer.h and TEXUNIT_FX_CHANNEL0 in
    apps/testfx/ApplicationTestFX.h, which is the same number written once on the C++ side.

    iChannel0 defaults to the seeded noise from Application::rrand: reproducible bytes the
    simulation can draw from as well, which a GLSL hash function can never be.
*/
//The engine-wide texture unit map - see core/TextureUnits.h for the C++ mirror.
#include "texture_units.glsl"
/*
    THE FOUR CHANNELS ARE BORROWED FROM THE TOP OF THE MATERIAL RANGE, not owned.

    They used to sit at 28-31, above everything the engine reserved. The reserved run now ends
    at 10 and materials take everything above it, so there is no 'above everything' left to sit
    in - four units had to come from somewhere, and the top of the material range is the one
    place where taking them costs nothing here: a shadertoy effect draws on a bare full-screen
    quad with no materials of its own.

    It does mean a testfx scene with enough material textures to reach this far would fight the
    channels. UploadMaterials already warns when materials run off the top of the range, which
    is the same wall by a different name. TEXUNIT_FX_CHANNEL0 in ApplicationTestFX.h is the C++
    side of this and derives the same number the same way.
*/
#define TEXUNIT_FX_CHANNEL0 (TEXUNIT_MATERIAL_FIRST + NUM_MATERIAL_UNITS - 4)
layout (binding = TEXUNIT_FX_CHANNEL0 + 0) uniform sampler2D iChannel0;
layout (binding = TEXUNIT_FX_CHANNEL0 + 1) uniform sampler2D iChannel1;
layout (binding = TEXUNIT_FX_CHANNEL0 + 2) uniform sampler2D iChannel2;
layout (binding = TEXUNIT_FX_CHANNEL0 + 3) uniform sampler2D iChannel3;

uniform vec3  iChannelResolution[4];
uniform float iChannelTime[4];

#endif

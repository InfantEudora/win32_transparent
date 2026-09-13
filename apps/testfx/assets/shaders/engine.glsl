/*
    The engine-side extras: what Renderer::CustomShaderPass hands every custom material, plus the
    camera basis this bench builds for raymarching.

    This is the file that makes an effect TRANSPLANTABLE. Everything declared here is bound by the
    renderer for any MESH_MODE_SHADER draw in any app, so a shader written against it works
    unchanged inside a game - the bench is not emulating the pipeline, it is running in it.

    Includes shaders/shadertoy.glsl, so a native effect gets vuv, frag_color, iTime and the four
    channels too; both files are guarded, so including both explicitly is fine and order-free.

    NO #version HERE - it stays the first line of the including file.
*/

#ifndef FX_ENGINE_GLSL
#define FX_ENGINE_GLSL

#include "shadertoy.glsl"

/*
    --- THE DEFERRED G-BUFFER, bound on units 1-3 by CustomShaderPass --------------------------

    READ THIS BEFORE USING IT. Depth is the ONLY channel that can tell you whether anything was
    drawn at a pixel. It is cleared to 1.0, which is outside the range any drawn fragment can
    occupy, so `depth < 1.0` means geometry and nothing else does.

    Reaching for position first is the trap everyone falls into: gbuffer_position is cleared to a
    perfectly legal world coordinate, and the world origin is an ordinary place for geometry to be
    - in a game built around the origin it is where all of it is. So an effect that samples
    position to find out how far away the scene is reads "the origin" for the empty sky, and the
    soft-intersection fade dissolves against the background. Nor is w a way out: deferred.frag
    writes the material's ALPHA there, not a flag. (The normal buffer clears to (1,0,0,0), a unit
    +X normal, which is just as legal a value.)

    The pattern:

        float scene_depth = texture(gbuffer_depth,vuv).r;
        if (scene_depth < 1.0){                              //something is there
            vec3 scene_world = texture(gbuffer_position,vuv).xyz;
            ...
        }

    The G-buffer is filled whether or not the test geometry is on - it is just empty when it is
    off, which is exactly the case the paragraph above is about. Turn the test geometry on before
    concluding that a depth-aware effect is broken.
*/
layout (binding = 1) uniform sampler2D gbuffer_depth;
layout (binding = 2) uniform sampler2D gbuffer_position;
layout (binding = 3) uniform sampler2D gbuffer_normal;

//--- set by CustomShaderPass on every custom shader, every frame -------------------------------
uniform mat4 mat_worldcam;
uniform vec3 eye_position;

/*
    --- THE CAMERA, AS A RAY BASIS ---------------------------------------------------------------

    iCamRight and iCamUp are PRE-SCALED by the field of view and the aspect ratio, so a native
    effect marches the scene's real camera with

        vec2 uv  = vuv * 2.0 - 1.0;                              //-1..+1, y up
        vec3 dir = normalize(iCamForward + uv.x*iCamRight + uv.y*iCamUp);
        vec3 org = iCamPos;

    and the engine camera then orbits whatever the effect draws, with the middle mouse button,
    like any other app here.

    Built on the CPU rather than by inverting a view-projection in GLSL because core/type_fmat4.h
    has no general matrix inverse - inverse_transform is rigid-only - and because four vec3s a
    frame beats a per-pixel inversion.

    iCamPos and eye_position are the same point, arriving by two different routes. Use either.
*/
uniform vec3 iCamPos;
uniform vec3 iCamRight;
uniform vec3 iCamUp;
uniform vec3 iCamForward;

/*
    --- THE SCENE'S REAL LIGHTS, bound globally on SSBO 2 -----------------------------------------

    Copied verbatim from shaders/custom.frag and breakout_shield.frag so the layout cannot drift
    from light_t in core/Light.h. `direction` all zero means a point light; a non-zero cos_angle
    makes it a cone. Walk it with lights.length() - the array is unsized because the count is
    whatever the scene has.
*/
struct Light{
    vec3    position;
    int     shadow;
    vec3    direction;  //all zero means a point light
    float   brightness;
    vec3    color;
    float   cos_angle;  //0 means a point light, otherwise a cone
    //Size of the source in world units, for the penumbra estimate. Appended last, and the
    //next one must be too - see core/light_t, which this is a hand copy of.
    float   radius;
    float   pad0;
    float   pad1;
    float   pad2;
};
layout (std430, binding = 2) buffer LightBuffer{
    Light lights[];
};

//--- small helpers, because every effect writes these otherwise --------------------------------

//The ray this pixel looks along, through the scene's camera. See the iCam* block above.
vec3 FxViewRay(){
    vec2 uv = vuv * 2.0 - 1.0;
    return normalize(iCamForward + uv.x * iCamRight + uv.y * iCamUp);
}

//True when the deferred pass drew something at this pixel, with its world position in `world_out`.
//The one correct way to ask - see the G-buffer block above for why the obvious way is not.
bool FxSceneHit(out vec3 world_out){
    float scene_depth = texture(gbuffer_depth,vuv).r;
    if (scene_depth >= 1.0){
        world_out = vec3(0.0);
        return false;
    }
    world_out = texture(gbuffer_position,vuv).xyz;
    return true;
}

#endif

/*
    The 2D overlay's vertex stage. See core/UIOverlay.h and docs/ui_overlay_plan.md.

    THIS FILE DELIBERATELY HAS NO #version, and it is the first in the tree that does not. A
    shader that omits it is declaring itself portable: core/Shader.cpp's LoadShaderSource prepends
    the platform's directive (`#version 430 core` on desktop, `#version 310 es` plus the precision
    defaults on Android) and a `#line 1 0` so error line numbers still match this file. A shader
    that states its own version is left alone, which is why the other eleven here are unaffected.

    Everything else in this file is valid on both, including the precision qualifiers a GLES
    fragment shader needs - desktop GLSL has accepted those since 1.30 and ignores them. So this
    is one source for both platforms rather than the fork the Android port ended up with.

    Geometry is a plain triangle list - no instancing, no SSBO, no UBO. That is not the idiom the
    rest of this renderer uses and it is the right one here: the port's test device reports
    GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS = 0, so an SSBO read from a vertex shader fails to LINK
    there, and its UBO size limit sits at the spec floor. Vertex attributes are the one path every
    GL and GLES version agrees on.
*/

//Locations are this program's own - the overlay has its own VAO and shares nothing with
//core/Mesh.h's ATTRIB_* scheme.
layout (location = 0) in vec2  a_pos;               //screen pixels, top-left origin, +Y down
layout (location = 1) in vec2  a_uv;
layout (location = 2) in vec2  a_local;             //offset from the rect's centre, pixels
layout (location = 3) in vec2  a_half_extent;       //the rect's half size, pixels
layout (location = 4) in float a_radius;
layout (location = 5) in float a_outline;
layout (location = 6) in float a_distance_scale;
layout (location = 7) in vec4  a_color;             //RGBA8, normalised on the way in

uniform vec2 screen_size;

out vec2 v_uv;
out vec2 v_local;

/*
    flat, not smooth, for everything that describes the QUAD rather than the point.

    All six vertices of a quad carry identical values for these, so interpolating them would be
    arithmetic that cannot change the answer. Marking them flat says so, and says it to the
    reader as much as to the compiler: v_local is the only thing here that is meant to vary
    across the surface, and the rounded-box distance is a function of it and of constants.
*/
flat out vec2  v_half_extent;
flat out float v_radius;
flat out float v_outline;
flat out float v_distance_scale;
flat out vec4  v_color;

void main(){
    //Pixels to clip space. The Y flip is what puts the origin top-left, matching
    //InputController's TouchRect - so the rectangle that gets drawn is the rectangle that gets
    //hit-tested, which is the one correspondence in this whole file worth being careful about.
    vec2 ndc = vec2((a_pos.x / screen_size.x) * 2.0 - 1.0,
                    1.0 - (a_pos.y / screen_size.y) * 2.0);

    //z = 0 and w = 1: the overlay does not participate in depth at all (UIOverlay::Draw turns
    //the test off), so there is nothing for a depth value to mean here.
    gl_Position = vec4(ndc,0.0,1.0);

    v_uv             = a_uv;
    v_local          = a_local;
    v_half_extent    = a_half_extent;
    v_radius         = a_radius;
    v_outline        = a_outline;
    v_distance_scale = a_distance_scale;
    v_color          = a_color;
}

#version 460 core

/*
    The vertex stage every effect in testfx shares, and the whole of why a fullscreen pass needs
    no renderer changes.

    IT IGNORES THE CAMERA, ON PURPOSE. The quad is MakeQuad(2,2), which spans -1..+1 in XY - and
    that is already clip space, so position.xy goes straight into gl_Position with no
    mat_worldcam, no instance transform and no projection. Consequences worth knowing before
    something here looks broken:

      - The quad covers the viewport at any window size and from any camera position. That is the
        point; it is also why moving the Effect Quad object in the Inspector does nothing.
      - Nothing culls it. Renderer::CullObjects renders every visible object - this engine has no
        frustum test - so a quad whose vertices are nowhere near the camera cannot vanish.
      - mat_worldcam and eye_position are still SET on this program every frame by
        Renderer::CustomShaderPass, and are still available to the fragment stage. They are simply
        not used here. A "program N has no uniform mat_worldcam" warning on stderr after a compile
        is that, and is expected: GLSL strips a uniform nothing reads.

    vuv is 0..1 across the viewport with y pointing UP, matching GL's clip space and so shadertoy's
    fragCoord. The fragment side builds fragCoord as vuv * iResolution.xy rather than reading
    gl_FragCoord, because gl_FragCoord is in WINDOW coordinates and this renderer supports an
    offset viewport (Renderer::viewport_x/_y, which Tank uses) - the derived form is right whatever
    the viewport is doing, and an effect written here is meant to be pasted into an app that might.

    z = 0, not 1: harmless either way while the depth test is off, and the sane value if anyone
    ever turns it back on.
*/

//Location 0 is `position` in this engine's vertex layout - see shaders/default.vert. The other
//four attributes are there in the buffer and deliberately not declared; nothing here needs them.
layout (location = 0) in vec3 position;

layout (location = 0) out vec2 vuv;

void main(){
    vuv = position.xy * 0.5 + 0.5;
    gl_Position = vec4(position.xy,0.0,1.0);
}

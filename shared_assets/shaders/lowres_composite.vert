#version 430 core

/*
    The vertex stage of Renderer::CompositeLowRes, which scales the reduced-resolution
    custom-shader target back over the frame.

    NO VERTEX BUFFER AND NO ATTRIBUTES. The three positions are built from gl_VertexID, so the
    draw is glDrawArrays(GL_TRIANGLES,0,3) against an empty VAO and the renderer needs no mesh,
    no upload and no instance data to run it. That matters more here than it looks: this pass runs
    inside the geometry passes, and a Mesh would have had to be registered, culled and batched
    like scene geometry to draw one triangle that is not in the scene.

    ONE TRIANGLE, NOT TWO. The vertices are (-1,-1), (3,-1), (-1,3) - a triangle large enough that
    the clipped part of it covers the whole viewport. A quad made of two triangles rasterises the
    pixels along its shared diagonal in two separate quads of fragments, which costs a little and,
    on some drivers, shows as a seam when the fragment shader is not perfectly uniform. There is
    no seam to have with one triangle.

    Nothing is interpolated to the fragment stage on purpose: the fragment shader needs the exact
    integer block a window pixel falls in, and reads gl_FragCoord for it. A varying uv would have
    to be un-rounded back into the same integer, which is the same arithmetic with a float error
    in the middle of it.
*/

void main(){
    //(0,0) -> (-1,-1), (1,0) -> (3,-1), (2,0) -> (-1,3).
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;
    //z = 0 rather than 1: harmless while the depth test is off, which CompositeLowRes makes sure
    //of, and the sane value if anyone ever turns it back on.
    gl_Position = vec4(x,y,0.0,1.0);
}

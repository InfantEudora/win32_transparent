#version 460 core

/*
    Writes one fragment's height into the occluder field. All of the work is done by the blend
    equation rather than by this shader: Renderer::RenderFieldPass sets

        glBlendEquationSeparate(GL_MAX,GL_MIN)

    so RGB max-blends and alpha min-blends, and a single pass over the geometry with no depth
    test and no culling leaves the tallest surface in R and the lowest in A. That is why the same
    value is written to both ends of the vector, and why this needs no sorting, no depth buffer
    and no second pass.

    G is written as zero and left alone, because the jump flood that fills it with the 2D
    distance field runs after this pass and MAX-blending a distance against garbage would fight
    it. B is spare.

    Channel layout of the field texture, in one place:
        R  height of the highest surface in this column   (world units, along field_axis)
        G  2D distance to the nearest occupied texel      (filled by the jump flood; 0 until then)
        B  unused
        A  height of the lowest surface in this column

    An empty column keeps the clear value - R very low, A very high - which makes the slab test
    in CalcFieldShadow report "nothing here" without needing a separate occupancy flag.
*/

layout (location = 0) in float v_height;

out vec4 frag_field;

void main(){
    frag_field = vec4(v_height,0.0,0.0,v_height);
}

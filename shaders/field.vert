#version 460 core

/*
    The occluder field: one top-down pass that answers, for every column of the world, "what is
    the highest and the lowest surface in it?".

    This is NOT a depth pass and deliberately does not share one. A depth attachment holds a
    single value with a fixed meaning, written by the depth test; this pass wants two values per
    texel, written by MIN/MAX blending with the depth test off entirely, and later a third
    written by a compute shader. See Renderer::RenderFieldPass for the state it runs under and
    default.frag's CalcFieldShadow for what reads the result.

    Height is measured along `field_axis` in WORLD units rather than as a projected depth, so
    everything downstream - the slab test, the normal bias, eventually the sphere-tracing step
    length - works in one space and needs no conversion. It also means a screenshot of the field
    map has readable numbers in it, which is most of how this got debugged.

    Only the instance transform is needed, so this drops everything default.vert carries for
    lighting. The morph targets are kept: a morphed vertex really is somewhere else, and a height
    map that disagreed with the drawn geometry would shadow from the wrong place.
*/

#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS	4

layout (location = 0) in vec3 position;

//Must match instancedata_t in Renderer.h, in full - this reads one field out of the middle of it.
struct InstanceData{
	mat4 mat_transformscale;
	int material_slot[NUM_MATERIAL_SLOTS];
	float morph_factors[MAX_MORPH_TARGETS];
	int objectid;
	int num_bones;
	int vertex_count;
	int num_morph_targets;
};

struct morph_vertex{
	vec3 position;
	float pad1;
	vec3 normal;
	float pad2;
};

layout (std430, binding = 0) buffer InstanceDataBuffer{
	InstanceData instance_data[];
};

layout (std430, binding = 5) buffer MorphBuffer{
	morph_vertex morph_vertices[];
};

//World -> the field's orthographic clip space. Its own matrix, fitted to the area worth having
//shadows in rather than to the scene or to any light.
uniform mat4 mat_field;

//Which way is "up" for this field, in world space. The engine has no fixed up axis and the apps
//disagree: the Tetris board is the XY plane with Z up, most others are XZ with Y up.
uniform vec3 field_axis = vec3(0,0,1);

layout (location = 0) out float v_height;

void main(){
	vec3 pos = position;
	int voffset = instance_data[gl_InstanceID].vertex_count;
	for (int i=0;i<instance_data[gl_InstanceID].num_morph_targets;i++){
		pos += (morph_vertices[(i*voffset) + gl_VertexID].position * instance_data[gl_InstanceID].morph_factors[i]);
	}

	vec4 world_position = instance_data[gl_InstanceID].mat_transformscale * vec4(pos,1);

	v_height = dot(world_position.xyz,field_axis);
	gl_Position = mat_field * world_position;
}

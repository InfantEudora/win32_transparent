#version 460 core
/*
    Renderer::OutlinePass, step 1: an outlined object drawn again into the outline mask. Position
    only - whatever material or custom shader the object is drawn with, its SHAPE is the vertex
    positions and the transform, and that is all an outline needs. One object a draw, its world
    matrix in instance 0 of the same instance SSBO the mesh shaders read.
*/
#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS   4

layout (location = 0) in vec3 position;

struct InstanceData{
    mat4 mat_transformscale;
    int material_slot[NUM_MATERIAL_SLOTS];
    float morph_factors[MAX_MORPH_TARGETS];
    int objectid;
    int num_bones;
    int vertex_count;
    int num_morph_targets;
};

layout (std430, binding = 0) buffer InstanceDataBuffer{
    InstanceData instance_data[];
};

layout (location = 0) uniform mat4 mat_worldcam = mat4(1.0);

layout (location = 0) out vec3 world_position;

void main(){
    vec4 world = instance_data[gl_InstanceID].mat_transformscale * vec4(position,1.0);
    world_position = world.xyz;
    gl_Position = mat_worldcam * world;
}

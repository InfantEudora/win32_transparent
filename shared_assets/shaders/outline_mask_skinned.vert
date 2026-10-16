#version 450 core
/*
    Renderer::OutlinePass, step 1, for a SKINNED mesh: outline_mask.vert's job - an outlined object
    drawn again into the mask, position only - posed by its skeleton the way default_skinned.vert
    poses it, so the outline follows the figure as it walks. One object a draw: its bones are the
    first num_bones of the bone SSBO, laid out by joint index (Renderer::AppendBoneData). Morph
    targets are not applied, as in the plain mask.
*/
#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS   4

layout (location = 0) in vec3 position;
layout (location = 6) in vec3 weights;

struct InstanceData{
    mat4 mat_transformscale;
    int material_slot[NUM_MATERIAL_SLOTS];
    float morph_factors[MAX_MORPH_TARGETS];
    int objectid;
    int num_bones;
    int vertex_count;
    int num_morph_targets;
};

struct Bone{
    mat4 mat_transformscale;
    mat4 inv_bindmatrix;
};

layout (std430, binding = 0) buffer InstanceDataBuffer{
    InstanceData instance_data[];
};

layout (std430, binding = 4) buffer BoneDataBuffer{
    Bone bone_data[];
};

//The bone indices are read from the vertex buffer itself, as default_skinned.vert reads them - see
//SSBO_VERTEX_PULL in core/Mesh.h. skinned_vertex is 18 words; the three bone ids are words 12..14.
layout (std430, binding = 6) readonly buffer VertexPull{
    uint vertex_words[];
};

layout (location = 0) uniform mat4 mat_worldcam = mat4(1.0);

layout (location = 0) out vec3 world_position;

void main(){
    uint vrow = uint(gl_VertexID) * 18u;
    ivec3 bones = ivec3(int(vertex_words[vrow + 12u]),int(vertex_words[vrow + 13u]),int(vertex_words[vrow + 14u]));
    int base = gl_InstanceID * instance_data[gl_InstanceID].num_bones;
    mat4 skin = weights.x * bone_data[base + bones.x].mat_transformscale * bone_data[base + bones.x].inv_bindmatrix +
                weights.y * bone_data[base + bones.y].mat_transformscale * bone_data[base + bones.y].inv_bindmatrix +
                weights.z * bone_data[base + bones.z].mat_transformscale * bone_data[base + bones.z].inv_bindmatrix;
    vec4 world = skin * vec4(position,1.0);
    world_position = world.xyz;
    gl_Position = mat_worldcam * world;
}

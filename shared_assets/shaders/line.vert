#version 460 core
/*
    Vertex stage for MESH_MODE_LINE meshes - the physics debug lines and anything else built
    from line_vertex (core/type_vertex.h): a position and a packed 32-bit colour, 16 bytes.

    Lines used to be drawn with default.vert, whose material-index input then read the colour
    word through ATTRIB_MATINDEX - two VAOs with different layouts for the same attribute index
    behind one program, and a colour treated as a material slot. That was suspected of being the
    Intel material bug on 2026-09-16; it was not (see SSBO_VERTEX_PULL in core/Mesh.h for what
    was), but a program whose inputs are exactly the line VAO's attributes is the defined
    spelling on every driver, and it lets a debug line have its colour instead of being white.

    Transform comes from the same instance SSBO the mesh shaders read, so a line mesh is placed
    like any other object.
*/
#define NUM_MATERIAL_SLOTS  4
#define MAX_MORPH_TARGETS   4

layout (location = 0) in vec3 position;
layout (location = 4) in uint color;     //Packed 0xAARRGGBB / 0x00RRGGBB, see line_vertex

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

layout (location = 0) out vec4 vcolor;

void main(){
    vec4 world_position = instance_data[gl_InstanceID].mat_transformscale * vec4(position,1.0);
    //Byte order of the packed colour as reactphysics3d's debug renderer writes it: red in the
    //high byte of the low 24 bits. The top byte is ignored - debug lines are always opaque.
    vcolor = vec4(float((color >> 16) & 0xFFu),
                  float((color >>  8) & 0xFFu),
                  float( color        & 0xFFu),
                  255.0) / 255.0;
    gl_Position = mat_worldcam * world_position;
}

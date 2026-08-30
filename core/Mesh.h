#ifndef _MESH_H_
#define _MESH_H_
class Mesh;
#include <vector>
#include "glad.h"
#include "stdint.h"
#include "type_vertex.h"
//Basic
#define ATTRIB_VERTEX   0
#define ATTRIB_NORMAL   1
#define ATTRIB_TANGENT  2
//UVs
#define ATTRIB_UVCOORD  3
#define ATTRIB_MATINDEX 4
//Skinning
#define ATTRIB_BONES    5
#define ATTRIB_WEIGHTS  6
//More nonsense

#define MESHID_INVALID 0xFFFFFFFF

//Mode is set based on vertex type which the renderer may choose to render differently.
#define MESH_MODE_INVALID -1
#define MESH_MODE_NORMAL  0
#define MESH_MODE_SKINNED 1
#define MESH_MODE_LINE    2
#define MESH_MODE_SHADER  3

typedef uint32_t meshid_t;

class Mesh{
public:
    Mesh();
    bool InitVBOVAO();
    bool InitLineVBOVAO();
    bool InitSkinnedVBOVAO();

    bool InitSSBO();

    GLuint vbo = 0;    //Vertex Buffer
    GLuint vao = 0;    //Attribute Buffer
    GLuint ssbo = 0;   //Shader Storage used for Morph Targets

    void RenderInstances(int num_instances);
    void GenerateUniqueID();
    meshid_t GetID();

    void SetMeshData(vertex* verts, int vertex_count);
    void SetLineMeshData(line_vertex* verts, int vertex_count);
    void SetSkinnedMeshData(skinned_vertex* verts, int vertex_count);
    void SetMorphMeshData(morph_vertex* verts, int vertex_count);

    const std::vector<vertex>& GetVertices() const {return vertices;};

    bool IsNormalMesh();
    bool IsSkinnedMesh();
    bool IsLineMesh();

    vec3 GetExtents();

    uint32_t num_vertices = 0;
    int     num_materials = 0;
    int     num_references = 0; //Or... maybe use shared_ptr?
    int     num_morph_targets = 0;
    int     mesh_mode = MESH_MODE_INVALID;

    int32_t batch_index = -1;
    int32_t batch_num_instances = 0;

private:
    std::vector<vertex>vertices;
    std::vector<skinned_vertex>skinned_vertices;
    std::vector<line_vertex>line_vertices;
    std::vector<morph_vertex>morph_vertices;
    static meshid_t mesh_ids;   //Total amount of different meshes.
    meshid_t id = MESHID_INVALID;
    vec3    extents; //The size an AABB should be to encompass the mesh
};

#endif
#ifndef _SKINNED_MESH_H_
#define _SKINNED_MESH_H_
class SkinnedMesh;
#include <vector>
#include "glad.h"
#include "stdint.h"
#include "type_vertex.h"

#define SKINNED_ATTRIB_VERTEX   0
#define SKINNED_ATTRIB_NORMAL   1
#define SKINNED_ATTRIB_TANGENT  2
#define SKINNED_ATTRIB_UVCOORD  3
#define SKINNED_ATTRIB_BONES    4
#define SKINNED_ATTRIB_WEIGHTS  5
#define SKINNED_ATTRIB_MATINDEX 6

#define SKINNED_MESHID_INVALID 0xFFFFFFFF

typedef uint32_t meshid_t;

class SkinnedMesh{
public:
    SkinnedMesh();
    bool InitVBOVAO();

    GLuint vbo = -1;
    GLuint vao = -1;

    void RenderInstances(int num_instances);
    void GenerateUniqueID();
    meshid_t GetID();

    void LoadUnitCube();
    void SetMeshData(skinned_vertex* verts, int vertex_count);

    uint32_t num_vertices = 0;
    int     num_materials = 0;
    int     num_references = 0; //Or... maybe use shared_ptr?

    int32_t batch_index = -1;
    int32_t batch_num_instances = 0;
private:
    std::vector<skinned_vertex>vertices;
    static meshid_t mesh_ids;   //Total amount of different meshes.
    meshid_t id = SKINNED_MESHID_INVALID;
};

#endif
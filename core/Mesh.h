#ifndef _MESH_H_
#define _MESH_H_
class Mesh;
#include <vector>
#include "glad.h"
#include "stdint.h"
#include "type_vertex.h"

#define ATTRIB_VERTEX   0
#define ATTRIB_NORMAL   1
#define ATTRIB_UVCOORD  2
#define ATTRIB_MATINDEX 3

#define MESHID_INVALID 0xFFFFFFFF

typedef uint32_t meshid_t;

class Mesh{
public:
    Mesh();
    bool InitVBOVAO();

    GLuint vbo = -1;
    GLuint vao = -1;

    void RenderInstances(int num_instances);
    void GenerateUniqueID();
    meshid_t GetID();

    void LoadUnitCube();
    void SetMeshData(vertex* verts, int vertex_count);

    uint32_t num_vertices = 0;
    int     num_materials = 0;
    int     num_references = 0; //Or... maybe use shared_ptr?

    int32_t batch_index = -1;
    int32_t batch_num_instances = 0;
private:
    std::vector<vertex>vertices;
    static meshid_t mesh_ids;   //Total amount of different meshes.
    meshid_t id = MESHID_INVALID;
};

#endif
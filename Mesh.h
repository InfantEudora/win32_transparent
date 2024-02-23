#ifndef _MESH_H_
#define _MESH_H_
class Mesh;
#include "glad.h"
#include "stdint.h"

struct Vertex{
    float pos[3];
    float normal[3];
    float texcoord[2];
};

#define ATTRIB_VERTEX   0
#define ATTRIB_NORMAL   1
#define ATTRIB_UVCOORD  2

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

    int32_t batch_index = -1;
    int32_t batch_num_instances = 0;

    private:
    static meshid_t mesh_ids;   //Total amount of different meshes.
    meshid_t id = MESHID_INVALID;
};

#endif
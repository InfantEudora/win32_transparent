#ifndef _OBJECT_H_
#define _OBJECT_H_
class Object;
#include "Mesh.h"
#include "type_fmat3.h"
#include "type_fmat4.h"
#include "type_vec3.h"
#include <string>

typedef uint32_t objectid_t;
#define OBJECTID_INVALID 0xFFFFFFFF

class Object{
    public:
    Object();
    void GenerateUniqueID();
    void Rotate();
    meshid_t GetMeshID();
    objectid_t GetID();
    Mesh* GetMesh();
    void SetMesh(Mesh* mesh);
    void SetMeshBatchIndex(int32_t batch_index);
    int32_t GetMeshBatchIndex();
    bool WouldRender();
    void MarkForRender();

    void UpdateTransformMatrix();
    fmat4& GetWorldTransformScaleMatrix();

    void MoveBy(const vec3& delta);
    void SetPosition(const vec3& newpos);

    std::string name;
    fmat4 local_transform_scale_matrix;
    fmat4 world_transform_scale_matrix;
    fmat3 mat_rotation;
    vec3 position;
    float rotation = 0.0f;

    bool f_was_transformed = false;

    private:
    Mesh* mesh = NULL;
    objectid_t id = OBJECTID_INVALID;
    static objectid_t object_ids;
};

#endif
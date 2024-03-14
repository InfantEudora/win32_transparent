#ifndef _OBJECT_H_
#define _OBJECT_H_
class Object;
#include "Mesh.h"
#include "type_fmat3.h"
#include "type_fmat4.h"
#include "type_vec3.h"
#include "type_quat.h"
#include <string>
#include <atomic>

typedef uint32_t objectid_t;
#define OBJECTID_INVALID 0xFFFFFFFF

struct ObjectState{
    bool f_was_transformed = false;
    vec3 position = vec3(0,0,0);
    quat rotation;
    float rot_speed = 0.0f;
    fmat3 mat_rotation;
};

//All Set functions may only be set from physics
//All Get functions may be read from DrawThread and get the current state
class Object{
    public:
    Object();
    void GenerateUniqueID();

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
    void SetScale(const vec3& newscale);
    void SetPosition(const vec3& newpos);
    void SetLookat(const vec3& newpos);

    void RotateAroundAxis(const vec3& target_axis,float by, bool f_lookat=false);

    void SetRotationSpeed(float newspeed);

    vec3& GetPosition();
    bool IsHovered();

    void UpdateState(); //Called from render thread before rendering
    void UpdatePhysicsState();

    std::string name;
    fmat4 local_transform_scale_matrix;
    fmat4 world_transform_scale_matrix;

    vec3 scale = vec3(1,1,1);
    vec3 lookat = vec3(0,0,0);
    vec3 up = vec3(0,1,0);

    int material_slot[4] = {};

    //For checking if the state_physics_prev is complete
    bool PhysicsCompleted();

    //Maybe we want some place for the current object transforms, that may be rendered.
    //And some place where the new ones are calculated.
    //They can be moved to a 'front' buffer, so the next frame may get them.
    // While that is taking place, the new one's can already be calculated.
    // A thread will be iterating over objects, and updating random things in random order.
    // So, one render function will iterate over all objects... see if all their last physics states are completed.
    // And copy them over. During this time, physics will have to wait.
protected:
    bool f_mouse_over = false;      //This is set after frame complete.

    ObjectState state;              //<- State that may be rendered this frame
    ObjectState state_physics;      //<- State physics may update.
    ObjectState state_physics_prev; //<- Last complete state the physics has calculated.

    std::atomic<int>state_completed = 0;  //If this state is completed.
    std::atomic<int>state_physics_completed = 0;
    std::atomic<int>state_physics_prev_completed = 0;

    Mesh* mesh = NULL;
    objectid_t id = OBJECTID_INVALID;
    static objectid_t object_ids;
};

#endif
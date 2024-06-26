#ifndef _OBJECT_H_
#define _OBJECT_H_
class Object;
#include <string>
#include <atomic>
#include <list>

#include "Mesh.h"
#include "type_fmat3.h"
#include "type_fmat4.h"
#include "type_vec3.h"
#include "type_quat.h"
#include "Material.h"

typedef uint32_t objectid_t;
#define OBJECTID_INVALID    0xFFFFFFFF
#define NUM_MATERIAL_SLOTS  4

//All variables that affect the object's appearance, which are modified/read by different threads
struct ObjectState{
    bool f_was_transformed = false;
    vec3 position = vec3(0,0,0);
    quat rotation;
    vec3 scale = vec3(1,1,1);
};

//All Set/Get functions may only be set from physics. And update the state_physics.
//Render thread will only read ObjectState state.
class Object{
    public:
    Object();
    virtual ~Object();
    void GenerateUniqueID();

    meshid_t GetMeshID();
    objectid_t GetID();

    //Mesh
    void SetMesh(Mesh* mesh);
    Mesh* GetMesh();
    void DeleteMesh();

    void SetMeshBatchIndex(int32_t batch_index);
    int32_t GetMeshBatchIndex();

    bool WouldRender();
    void MarkForRender();

    void UpdateTransformMatrix();
    fmat4& GetWorldTransformScaleMatrix();

    //Modify postition
    void SetPosition(const vec3& newpos);
    void MoveBy(const vec3& delta);
    vec3 MoveForwardBy(float delta);
    vec3 MoveUpBy(float delta);
    vec3 MoveSidewaysBy(float delta);

    //Modify size
    void SetScale(const vec3& newscale);
    vec3 GetScale();

    //Modify rotation
    void SetLookAt(const vec3& newpos, vec3* optional_up = NULL);
    void SetRotation(const quat& q);
    void RotateAroundAxis(const vec3& target_axis,float by);
    void RotateBy(const quat& q);
    void RollBy(float by);

    bool IsHovered();

    void UpdateState(); //Called from render thread before rendering
    void UpdatePhysicsState();

    std::string name;
    fmat4 local_transform_scale_matrix;
    fmat4 world_transform_scale_matrix;

    //The reference vectors for our coordinate system.
    static vec3 ref_up;
    static vec3 ref_left;
    static vec3 ref_forward;

    vec3 GetPosition();
    vec3 GetUp();       //Returns the local vector pointing up.
    vec3 GetForward();  //Returns the forward or normalized lookat direction
    vec3 GetLeft();     //Return the vector pointing left

    void PickMaterials(std::vector<Material>& list, std::vector<Material>& global_list); //Picks materials and assigns them to material slots. Pick list from global_list
    int material_slot[NUM_MATERIAL_SLOTS] = {};

    //For checking if the state_physics_prev is complete
    bool PhysicsCompleted();

    //Maybe we want some place for the current object transforms, that may be rendered.
    //And some place where the new ones are calculated.
    //They can be moved to a 'front' buffer, so the next frame may get them.
    // While that is taking place, the new one's can already be calculated.
    // A thread will be iterating over objects, and updating random things in random order.
    // So, one render function will iterate over all objects... see if all their last physics states are completed.
    // And copy them over. During this time, physics will have to wait.

    //Hierarchy
    Object* parent = NULL;              //Object we are a child of.
    std::list<Object*>children;

    bool AttachChild(Object* newchild); //Attaches an object as a child.
    void DetachChild(Object* targetchild);
    void GetAllSubObjects(std::vector<Object*>*objects); //Add's all objects attached to this object into a vector.

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
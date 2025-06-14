#ifndef _OBJECT_H_
#define _OBJECT_H_
class Object;
#include <string>
#include <atomic>
#include <list>
#include <array>

#include "Mesh.h"
#include "type_fmat3.h"
#include "type_fmat4.h"
#include "type_vec3.h"
#include "type_quat.h"
#include "Material.h"
#include "ObjectAnimation.h"

typedef uint32_t objectid_t;
#define OBJECTID_INVALID    0xFFFFFFFF
#define NUM_MATERIAL_SLOTS  4
#define NUM_MORPH_FACTOR_SLOTS  4

#define ANIMATION_STATE_PAUSED      0
#define ANIMATION_STATE_LOOPING     1
#define ANIMATION_STATE_TRANSITION  2

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
    Object(Object* object);
    virtual ~Object();
    void GenerateUniqueID();
    void Destroy();
    bool IsDestroyed(){return f_is_destroyed;};
    void Hide();
    void Show();
    void SetVisibility(bool flag);
    void SetPickability(bool flag);
    bool IsVisible(){return f_visible;};
    bool IsPickable(){return f_pickable;};

    meshid_t GetMeshID();
    objectid_t GetID();

    //Mesh
    void SetMesh(Mesh* mesh);
    Mesh* GetMesh();
    void DeleteMesh();
    void SetMeshBatchIndex(int32_t batch_index);
    int32_t GetMeshBatchIndex();
    //Mesh Animation
    void SetShapekey(int index, float factor);

    //Rendering
    void MarkForRender();
    void UpdateTransformMatrix();
    fmat4& GetLocalTransformScaleMatrix();
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
    void SetLookAt(const vec3& newpos, const vec3* optional_up = NULL);
    void SetWorldLookat(const vec3& target,const vec3& world_up);

    void SetRotation(const quat& q);
    void RotateAroundAxis(const vec3& target_axis,float by);
    void RotateBy(const quat& q);

    void RollBy(float by);
    void PitchBy(float by);
    void YawBy(float by);

    //Physics/state
    void UpdateState(); //Called from render thread before rendering
    virtual void UpdatePhysicsState();

    std::string name;
    fmat4 local_transform_scale_matrix;
    fmat4 world_transform_scale_matrix;

    //The reference vectors for our coordinate system.
    static vec3 ref_up;
    static vec3 ref_left;
    static vec3 ref_forward;

    vec3 GetPosition();
    vec3 GetWorldPosition();

    vec3 GetUp();               // Returns the local vector pointing up.
    vec3 GetWorldUp();          //
    vec3 GetForward();          // Returns the forward or normalized lookat direction
    vec3 GetLeft();             // Return the vector pointing left
    quat GetRotation();         // Returns a copy of the rotation
    quat GetWorldRotation();    // Calculates and returns world rotation

    //Materials
    void PickMaterials(std::vector<Material>& list, std::vector<Material>& global_list); //Picks materials and assigns them to material slots. Pick list from global_list
    int material_slot[NUM_MATERIAL_SLOTS] = {};
    void TakeMaterialNames(std::vector<Material>& list);
    std::array<std::string,NUM_MATERIAL_SLOTS>material_names; // List of material names the object should pick into it's material slots.
    bool f_update_materials = false; //In this render cycle, lookup materials from names and place them in slots.
    void UpdateMaterials(std::vector<Material>& global_list);

    //For checking if the state_physics_prev is complete
    bool PhysicsCompleted();

    //Lighting properties

    //Animation
    float morph_factors[NUM_MORPH_FACTOR_SLOTS] = {};
    std::vector<Animation*>animations;
    Animation* previous_animation = NULL;
    Animation* current_animation = NULL;
    Animation* next_animation = NULL;
    int animation_state = ANIMATION_STATE_LOOPING;
    bool f_animation_override = false;
    float animation_time_delta = 0.02f;
    void AddAnimation(Animation* animation);
    //void SetAnimation(Animation* animation);
    Animation* FindAnimation(const std::string& name); //Finds it by name
    void SetNextAnimation(Animation* animation);
    void SetNextAnimation(const std::string& name); //Set next one to wait until this one is completed.
    virtual void ApplyAnimation(float time_delta);

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

    bool    AttachChild(Object* newchild); //Attaches an object as a child.
    void    DetachChild(Object* targetchild);
    void    GetAllSubObjects(std::vector<Object*>& objects); //Add's all objects attached to this object into a vector.
    Object* GetChild(int index);
protected:
    bool f_visible = true;          // If the mesh should be rendered or not
    bool f_pickable = true;         // If the mesh should output it's id and is thus pickable
    bool f_is_destroyed = false;    // Someone should clean it up.

    ObjectState state;              //<- State that may be rendered this frame
    ObjectState state_physics;      //<- State physics may update.
    ObjectState state_physics_prev; //<- Last complete state the physics has calculated.

    std::atomic<int>state_completed = {0};  //If this state is completed.
    std::atomic<int>state_physics_completed = {0};
    std::atomic<int>state_physics_prev_completed = {0};

    Mesh*           mesh            = NULL;
    objectid_t      id              = OBJECTID_INVALID;
    static objectid_t object_ids;
};

#endif
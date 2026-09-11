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
#include "Physics.h"


typedef uint32_t objectid_t;
#define OBJECTID_INVALID    0xFFFFFFFF
#define NUM_MATERIAL_SLOTS  4
#define NUM_MORPH_FACTOR_SLOTS  4

#define ANIMATION_STATE_INVALID             -1
#define ANIMATION_STATE_PAUSED              0
#define ANIMATION_STATE_LOOPING             1
#define ANIMATION_STATE_TRANSITION_START    2
#define ANIMATION_STATE_TRANSITION          3
#define ANIMATION_STATE_TRANSITION_BACK     4
#define ANIMATION_STATE_LOAD_DEFAULT_POSE   5

//Everything about an object that decides where and how it is drawn. There is exactly ONE of
//these per object: the render thread and the physics thread are already mutually exclusive
//through Renderer::physics_mutex - the physics tick in Application::PhysicsThreadFunction and
//Renderer::DrawFrame both hold it - so neither can ever observe a half-written state, and there
//is nothing left for a second copy to protect. This used to be a triple buffer
//(state/state_physics/state_physics_prev) behind an atomic completion handshake, from a design
//where one object's next state was computed while its previous one was still being drawn. Both
//the physics solver and the render batcher need every object at once, though, so a per-object
//handshake never bought anything - it had already been commented out of Renderer::UpdateState.
struct ObjectState{
    //Dirty flag for local_transform_scale_matrix. Set by every setter below, cleared when
    //UpdateTransformMatrix rebuilds the matrix. Starts true so the first read always builds one,
    //which also covers state written without a setter (the duplication constructor's scale).
    bool f_was_transformed = true;
    bool f_visible = true;
    vec3 position = vec3(0,0,0);
    quat rotation;
    vec3 scale = vec3(1,1,1);
};

//Setters are for the physics thread, or for the render thread while it holds physics_mutex
//(which is the whole of Application::DrawImGuiUI) - see Renderer::physics_mutex.
//
//--- HANGING YOUR OWN DATA ON AN OBJECT --------------------------------------------------------
//There is deliberately no `void* user_data` here. SUBCLASS INSTEAD - that is the supported way,
//and it is not the awkward option it might look like, because an asset can be loaded straight
//INTO an object you already made:
//
//    class TetrisCell : public virtual Object{
//    public:
//        int2 board_coordinate;
//    };
//
//    TetrisCell* cell = new TetrisCell();
//    assetmanager->GetObjectFromAsset("block",cell);   //fills in mesh + material names
//    main_scene->AddObject(cell);
//
//and the reverse lookup - "the user clicked an object, what is it?" - is then a cast rather than
//a search through a parallel array:
//
//    TetrisCell* cell = dynamic_cast<TetrisCell*>(hovered_object);
//
//This is already how the engine's own gameplay types work: IsoCell (which carries exactly this
//"which cell am I" coordinate), IsoWall, ShipCharacter, Asteroid, HingedDoor and Pickup all pass
//`this` to GetObjectFromAsset. Look at any of them for a worked example.
//
//WHY NOT a user-data pointer: destruction here is deferred. Destroy() only MARKS an object, and
//the actual delete happens later inside Renderer::DeleteDestroyedObjects - so an app that put an
//owning pointer in a user_data field would get no hook at which to free what it pointed at, and
//the duplicate constructor below would leave two objects owning one allocation. A subclass has
//none of those problems: its members are destroyed by its own destructor, which the existing
//delete already runs. If a raw field is ever genuinely wanted here, it should be an opaque
//integer tag, never a pointer.
class Object{
    public:
    Object();
    Object(Object* object);
    virtual ~Object();
    void DeleteDestroyedChildren();
    void GenerateUniqueID();
    void Destroy(); //Marks it for deletion at a later time.
    bool IsDestroyed(){return f_is_destroyed;};
    void Hide();
    void Show();
    void SetVisibility(bool flag);
    void SetPickability(bool flag);
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
    void MarkForRenderBatch();
    void ClearRenderBatch();
    void UpdateTransformMatrix();
    fmat4& GetLocalTransformScaleMatrix();
    fmat4& GetWorldTransformScaleMatrix();

    //Modify postition
    void SetPosition(const vec3& newpos,bool f_write_physics=true); //If the position change needs to be written to the physics engine
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

    void SetRotation(const quat& q,bool f_write_physics=true);
    void RotateAroundAxis(const vec3& target_axis,float by);
    void RotateBy(const quat& q);

    void RollBy(float by);
    void PitchBy(float by);
    void YawBy(float by);

    //Physics/state
    //Syncs this object, and its children, from its rigid body once per simulation tick.
    virtual void UpdatePhysicsState();

    std::string name;
    std::string dbg_desired_animation_name; //What the character state wants for animation.
    fmat4 local_transform_scale_matrix;
    fmat4 world_transform_scale_matrix;

    //The reference vectors for our coordinate system.
    static vec3 ref_up;
    static vec3 ref_left;
    static vec3 ref_forward;

    bool IsVisible();
    vec3 GetCenterofMass();
    vec3 GetPosition();         // Local position, within parent
    vec3 GetWorldPosition();    // Position in world space, off the transform chain
    vec3 GetForward();          // Returns the forward or normalized lookat direction
    vec3 GetWorldForward();
    vec3 GetUp();               // Returns the local vector pointing up.
    vec3 GetWorldUp();

    vec3 GetLeft();             // Return the vector pointing left
    quat GetRotation();         // Returns a copy of the rotation
    quat GetWorldRotation();    // Calculates and returns world rotation
    quat WorldRotationToLocal(const quat& world_rotation); // Converts a world rotation to a local rotation for this object.

    /*
        --- MATERIALS -------------------------------------------------------------------------
        An object says which material each of its NUM_MATERIAL_SLOTS slots holds in one of two
        ways, and they are alternatives, not layers:

          BY NAME  - "slot 0 is whatever material is called Bricks". This is what a loader
                     produces: a GLTF/OBJ file names its materials, several files may name the
                     same one, and the renderer's global list is not complete until everything has
                     been loaded. So the name is recorded now and turned into an index later, once
                     there IS a global list to look in. SetMaterialName raises the "still needs
                     resolving" flag; Renderer::UpdateObjectMaterials calls ResolveMaterialNames
                     on the next frame, which does the lookup once and lowers it.

          BY INDEX - "slot 0 is global material 7", or -1 for no material at all. This is what
                     gameplay code does when it already knows the index (usually from
                     Renderer::FindMaterialIndex). SetMaterialSlot lowers the resolve flag,
                     because an index you supplied is the answer - there is nothing left to work
                     out, and a name lookup must not come along afterwards and overwrite it.

        Setting either one cancels the other, which is the whole invariant. It is enforced here
        rather than documented because it used to be documented: both arrays were public, most
        code assigned them directly, and an index written into a slot on an asset-loaded object
        was silently replaced by the name lookup on the next frame. That is only invisible when
        the name happens NOT to resolve, which is a terrible thing to depend on.
    */
    //Resolve every slot whose name matches something in global_list. No-op unless a name has been
    //set since the last resolve. Renderer::UpdateObjectMaterials calls this once per frame.
    void ResolveMaterialNames(std::vector<Material>& global_list);

    //By index. Lowers the resolve flag - see above.
    void SetMaterialSlot(int slot, int material_id);
    int GetMaterialSlot(int slot) const;
    //The whole array, for the renderer's per-instance batch fill. A raw pointer rather than
    //NUM_MATERIAL_SLOTS bounds-checked calls per object per frame.
    const int* GetMaterialSlots() const { return material_slot; }

    //By name. Raises the resolve flag - see above.
    void SetMaterialName(int slot, const std::string& name);
    void SetMaterialNames(const std::array<std::string,NUM_MATERIAL_SLOTS>& names);
    const std::string& GetMaterialName(int slot) const;
    const std::array<std::string,NUM_MATERIAL_SLOTS>& GetMaterialNames() const { return material_names; }

    //Records the names of a loader's material list, in order, one per slot. Raises the resolve
    //flag, so the names are turned into indices on the next frame even if nothing else happens.
    void TakeMaterialNames(std::vector<Material>& list);
    //Resolves `list` (a loader's own materials, in its own order) against global_list and assigns
    //the results to slots by position. Lowers the resolve flag: this IS a resolve, just against a
    //list the caller supplied rather than against the names stored on the object.
    void PickMaterials(std::vector<Material>& list, std::vector<Material>& global_list);

    //Lighting properties

    //Animation
    float morph_factors[NUM_MORPH_FACTOR_SLOTS] = {};
    std::vector<Animation*>animations;

    Animation* current_animation = NULL;
    Animation* transition_to = NULL;    // The animation we are transitioning towards (NULL when not transitioning)

    int animation_state = ANIMATION_STATE_LOOPING;

    bool f_animation_override = false;  //If we should manually step through animation with ticks
    int animation_override_ticks = 0;

    //These override applying animation
    float animation_mask = 1.0f; // 0.0 = no animation, 1.0 = full animation
    float position_mask = 1.0f;

    //Seconds of animation to advance per simulation tick. Refreshed from the simulation timestep
    //every tick by Scene::UpdateAnimations, so it tracks the physics rate instead of assuming
    //50Hz - animation timing is simulation state now that root motion drives character movement.
    //The initialiser is only what holds until the first tick sets it.
    float animation_time_delta = 0.02f;
    //Set when the debug UI slider is dragged, to stop the per-tick refresh above from immediately
    //overwriting the hand-picked value. Distinct from f_animation_override, which is about
    //stepping animation manually rather than about the size of the step.
    bool f_animation_time_delta_override = false;

    //This tick's simulation timestep in seconds, pushed in by Scene::UpdatePhysics before
    //UpdatePhysicsState() runs, so per-tick logic never has to hardcode a rate.
    float physics_timestep = 0.02f;

    float animation_transition_time = 0.0f;
    float animation_transition_time_max = 0.25f;    //Default blend time, used when no override matches
    float animation_transition_blend_time = 0.25f;  //Blend time resolved for the CURRENT transition (set once when it starts)
    float animation_transition_factor = 0.0f;   //Computed factor of animation_transition_time / animation_transition_blend_time
    void AddAnimation(Animation* animation);
    //void SetAnimation(Animation* animation);
    Animation* FindAnimation(const std::string& name); //Finds it by name
    const char* CurrentAnimationName();
    const char* NextAnimationName();

    //Sparse per-object table of (from,to) -> blend time overrides. An empty 'from' matches any
    //current animation (a wildcard "->to" default). Anything not listed here just uses
    //animation_transition_time_max.
    struct AnimationBlendOverride{
        std::string from;
        std::string to;
        float blend_time = 0.0f;
    };
    std::vector<AnimationBlendOverride> animation_blend_overrides;
    void SetBlendTime(const std::string& from, const std::string& to, float blend_time);
    float LookupBlendTime(const std::string& from, const std::string& to);

    virtual void ApplyAnimation(float time_delta);
    void TransitionToAnimation(const std::string& name);
    void TransitionToAnimation(Animation* animation);  // Flags that we can blend into the next animation
    void SwitchToAnimation(const std::string& name);                   // Does not need a animation transistion
    void SwitchToAnimation(Animation* animation);                      // Instantly switches to the next animation, without blending

    //Physics & Collision
    Physics*            GetPhysics();
    Physics*            AddPhysics(PhysicsWorld* world);
    void                ResetPhysics();
    rp3d::RigidBody*    GetRigidBody();
    void                SetCollisionCategoryBits(uint32_t bits);
    void                SetCollideWithMaskBits(uint32_t bits);

    void                SetMass(float mass);
    float               GetMass();
    vec3                GetVelocity();
    void                SetVelocity(const vec3& newvel);
    uint32_t collide_with_bits = 0; //Bits for the whole object that get applied to each collider.
    uint32_t collision_category_bits = 0;

    //Hierarchy
    //
    //A PHYSICS OBJECT CANNOT BE A CHILD. UpdatePhysicsState writes the body's WORLD transform
    //into this object's LOCAL state, and AddPhysics seeds the body from the local one as though
    //it were world - so a parent transform gets applied on top of a position that is already
    //final, and the object ends up where neither the solver nor the scene tree thinks it is.
    //Both AttachChild and AddPhysics call debug->Fatal rather than let that happen silently.
    //Give the body to the PARENT and leave children visual-only, or use DetachChildToWorld.
    std::list<Object*>children;
    bool    AttachChild(Object* newchild); //Attaches an object as a child.
    void    DetachChild(Object* targetchild);
    //Detaches a child and leaves it exactly where it was being drawn, by baking the world
    //transform it had inside the parent into its own local state. This is what "take this letter
    //off the word and let it fall" needs: plain DetachChild keeps the local transform, so the
    //child jumps to wherever that means once the parent is no longer contributing.
    bool    DetachChildToWorld(Object* targetchild);
    void    GetAllSubObjects(std::vector<Object*>& objects); //Add's all objects attached to this object into a vector.
    Object* GetLastChild();
    Object* GetParent();
    Object* GetChild(int index);
    Object* FindChild(std::string child_name);
    int     GetNumChildren(){return (int)children.size();};
protected:
    //Hierarchy
    Object* parent = NULL;              //Object we are a child of.

    //Materials. PRIVATE TO THE PAIR OF SETTERS ABOVE - including for subclasses, which is why
    //these are down here rather than in the protected block a subclass can reach. The invariant
    //is "an index and a pending name lookup cannot both be live", and it is only worth anything
    //if there is no way to write one of them without the other being cleared.
    int material_slot[NUM_MATERIAL_SLOTS] = {};
    std::array<std::string,NUM_MATERIAL_SLOTS>material_names;
    //A name has been set that has not been looked up yet. Named for what it means: the SLOTS are
    //stale with respect to the NAMES. (It was f_update_materials, which read like "my materials
    //changed, re-upload them" - a different operation entirely, and one the renderer does.)
    bool f_resolve_material_names = true;

    //Flags
    bool f_pickable = true;         // If the mesh should output it's id and is thus pickable
    bool f_is_destroyed = false;    // Someone should clean it up.

    Physics* physics = NULL;

    ObjectState state;  //Where this object is. Guarded by Renderer::physics_mutex.

    Mesh*           mesh            = NULL;
    objectid_t      id              = OBJECTID_INVALID;
    static objectid_t object_ids;
};

#endif
#include "Object.h"
#include "Debug.h"
//For LoadDefaultPose: only a Bone has a reference pose to be put back to. Object.h cannot include
//this (Bone.h includes Object.h), so the dependency lives here in the implementation.
#include "Bone.h"

static Debugger* debug = new Debugger("Object",DEBUG_INFO);

objectid_t Object::object_ids = 0;
vec3 Object::ref_up = vec3(0,1,0);
vec3 Object::ref_left = vec3(1,0,0);
vec3 Object::ref_forward = vec3(0,0,-1);

//Constructor
Object::Object(){
    GenerateUniqueID();
    world_transform_scale_matrix.identity();
    local_transform_scale_matrix.identity();
    state.rotation.identity();
}

//Object object object object object ... I mean it makes sense to me: Duplication constructor
Object::Object(Object* object):Object(){
    debug->Info("Duplicating object Object %p into this %p\n",object,this);
    SetMesh(object->GetMesh());
    name = object->name;
    //Direct assignment, not SetScale(): the collider cloned below is already the source's
    //scaled size, and SetScale would rescale it by this factor a second time.
    state.scale = object->GetScale();
#ifdef USE_PHYSICS
    Physics* p = object->GetPhysics();
    if (p){
        AddPhysics(p->world);

        //Own copy of a primitive shape rather than sharing the source's, so SetScale on either
        //object rescales only its own collider (mesh shapes still get shared - see CloneShape).
        rp3d::CollisionShape* shape = Physics::CloneShape(p->body->last_collider->getCollisionShape());
        reactphysics3d::Transform t = p->body->last_collider->getLocalToBodyTransform();
        physics->body->last_collider = physics->body->rigidbody->addCollider(shape,t);
        physics->body->rigidbody->updateMassPropertiesFromColliders();
        physics->SetGravityEnabled(p->IsGravityEnabled());
        physics->SetStatic(p->IsStatic());
        SetMass(object->GetMass());
        physics->body->rigidbody->setUserData(this);
        physics->SetFrictionCoefficient(object->GetPhysics()->GetFrictionCoefficient());
        physics->SetBounciness(object->GetPhysics()->GetBounciness());

    }
#endif
    SetCollisionCategoryBits(object->collision_category_bits);
    SetCollideWithMaskBits(object->collide_with_bits);

    material_names[0] = object->material_names[0];
    material_names[1] = object->material_names[1];
    material_names[2] = object->material_names[2];
    material_names[3] = object->material_names[3];
    material_slot[0] = object->material_slot[0];
    material_slot[1] = object->material_slot[1];
    material_slot[2] = object->material_slot[2];
    material_slot[3] = object->material_slot[3];
    SetPosition(object->GetPosition());
}

Object::~Object(){
    //debug->Info("Destroyed Object %p\n",this);
    DeleteMesh();
    //Hands the whole thing to ~Physics rather than reaching past it to destroy the rigid body and
    //leaving the wrapper, the PhysicsBody and every collision shape behind - which is what this
    //did, at 372 bytes an object. See core/physics/Physics.cpp.
    //
    //Guarded rather than left to the `if`: with USE_PHYSICS off `physics` is a void*, and
    //deleting one of those is ill-formed however unreachable the branch is. It is always NULL
    //in that build anyway.
#ifdef USE_PHYSICS
    if (physics){
        delete physics;
        physics = NULL;
    }
#endif

    //Delete all the child objects and their children
    std::list<Object*>::iterator it = children.begin();
    for ( ; it != children.end(); ) {
        Object* child = *it;
        delete child;
        it = children.erase(it);
    }
}

void Object::DeleteMesh(){
    if (mesh){
        //debug->Info("DeleteMesh: num_references=%i\n",mesh->num_references);
        mesh->num_references--;
        if (mesh->num_references == 0){
            delete mesh;
        }
    }
    mesh = NULL;
}

void Object::Destroy(){
    f_is_destroyed = true;
}

void Object::DeleteDestroyedChildren(){
    //This is now responsible for destroying objects...
    std::list<Object*>::iterator it = children.begin();
    for ( ; it != children.end(); ) {
        Object* child = *it;
        if (child->IsDestroyed()){
            //We should destroy it.
            it = children.erase(it);
            //Destroy object
            debug->Info("Child object %lu is about to be destroyed\n",child->GetID());
            delete child;
        }else{
            child->DeleteDestroyedChildren();
            ++it;
        }
    }
}

void Object::SetVisibility(bool flag){
    //No f_was_transformed here: that flag only guards local_transform_scale_matrix, which does
    //not depend on visibility. It was set when this wrote a separate physics-side state, back
    //when the flag doubled as "this copy has pending changes".
    state.f_visible = flag;
}

void Object::Hide(){
    SetVisibility(false);
#ifdef USE_PHYSICS
    if (physics){
        physics->SetActive(false);
    }
#endif
}

void Object::Show(){
    SetVisibility(true);
#ifdef USE_PHYSICS
    if (physics){
        physics->SetActive(true);
    }
#endif
}

bool Object::IsVisible(){
    return state.f_visible;
}

void Object::SetPickability(bool flag){
    f_pickable = flag;
}

void Object::GenerateUniqueID(){
    id = object_ids++;
}

objectid_t Object::GetID(){
    return id;
}

meshid_t Object::GetMeshID(){
    if (mesh){
        return mesh->GetID();
    }
    return MESHID_INVALID;
}

Mesh* Object::GetMesh(){
    return mesh;
}

#ifdef USE_PHYSICS
Physics* Object::AddPhysics(PhysicsWorld* world){
    if (!world){
        return NULL;
    }
    if (!physics){
        physics = new Physics(world);
        //We set the world position AND orientation in the physics engine from the local ones.
        //Only copying the position meant any SetRotation done before AddPhysics was silently
        //thrown away on the first UpdatePhysicsState (which syncs body -> object) - and worse,
        //any joint created in between had its anchors computed against an unrotated body.
        physics->SetBodyWorldPosition(GetPosition());
        physics->SetBodyWorldOrientation(GetRotation());
        physics->SetStatic(true);
        physics->SetGravityEnabled(false);
        //Carry over any collision filter set on this object before it had a body. Both default to
        //rp3d's own values, so for an object that never set them this changes nothing.
        physics->SetCollisionCategoryBits(collision_category_bits);
        physics->SetCollideWithMaskBits(collide_with_bits);
        //Stamp the body with the Object that owns it. Every collision/trigger callback in the
        //codebase already casts getUserData() straight back to an Object* (ApplicationDozer and
        //ApplicationShip's onContact, ApplicationTileset's onTrigger, CraneCharacter's magnet) -
        //but until now only the copy path above and Particle actually set it, so everyone else
        //was reading NULL and silently doing nothing. Set it here and the invariant is simply
        //true: any body in a world built through AddPhysics knows its Object.
        if (physics->body && physics->body->rigidbody){
            physics->body->rigidbody->setUserData(this);
        }
        //Catches the "parent first, body second" ordering. Note the two SetBody* calls above
        //already assume this object is a root: they seed the body from the LOCAL transform.
        if (parent){
            debug->Err("Object '%s' (id=%i) has a rigid body AND a parent '%s' (id=%i). leave "
                 "this child visual-only, or detach it with DetachChildToWorld.\n",
                 name.c_str(),id,parent->name.c_str(),parent->id);
        }
        return physics;
    }
    return NULL;
}

Physics* Object::GetPhysics(){
    return physics;
}

rp3d::RigidBody* Object::GetRigidBody(){
    if (physics && physics->body){
        return physics->body->rigidbody;
    }
    return NULL;
}
#endif

//Unguarded on purpose - see the declaration. Always false in a no-physics build, because
//nothing can assign `physics` there.
bool Object::HasPhysics(){
    return physics != NULL;
}

//Not guarded - it names no rp3d type, so calling code keeps working in both builds. With
//physics off there is never a body to reset and this is a no-op.
void Object::ResetPhysics(){
#ifdef USE_PHYSICS
    if (physics){
        physics->SetVelocity(vec3());
        physics->SetAngularVelocity(vec3());
        physics->SetBodyWorldOrientation(quat().identity());
    }
#endif
}

int32_t Object::GetMeshBatchIndex(){
    if (mesh){
        return mesh->batch_index;
    }
    return -1;
}

void Object::SetMesh(Mesh* _mesh){
    if (!_mesh){
        return;
    }
    if (mesh){
        mesh->num_references--;
    }
    mesh = _mesh;
    mesh->num_references++;
}

//Set's this object's mesh index when batched
void Object::SetMeshBatchIndex(int32_t index){
    if (mesh){
        mesh->batch_index = index;
    }
}

void Object::SetShapekey(int index, float factor){
    if ((index >= 0) && (index < NUM_MORPH_FACTOR_SLOTS)){
        morph_factors[index] = factor;
    }
}

void Object::RotateAroundAxis(const vec3& target_axis,float by){
    RotateBy(quat(target_axis,by).normalize());
}

//Update objects rotation with supplied quaternion.
void Object::SetRotation(const quat& q, bool f_write_physics){
    state.f_was_transformed = true;
    state.rotation = q;
#ifdef USE_PHYSICS
    if (f_write_physics && physics){
        physics->SetBodyWorldOrientation(q);
    }
#endif
}

void Object::RotateBy(const quat& r){
    quat nq = r * state.rotation;
    SetRotation(nq);
}

void Object::SetPosition(const vec3& newpos,bool f_write_physics){
    state.f_was_transformed = true;
    state.position = newpos;
#ifdef USE_PHYSICS
    if (f_write_physics && physics){
        physics->SetBodyWorldPosition(GetPosition());
    }
#endif
}

//TODO: This should set worldposition
/*
void Object::SetWorldPosition();
*/

//Look at target from current position. Optional up can be supplied, otherwise will use ref_up.
//Target is in local space.
void Object::SetLookAt(const vec3& target, const vec3* optional_up){
    vec3 up;
    if (optional_up){
        up = *optional_up;
    }else{
        up = ref_up;
    }
    quat lq = quat::getquat(target,state.position,up);
    lq.normalize();
    SetRotation(lq);
}

//Look at position in world space
void Object::SetWorldLookat(const vec3& target,const vec3& world_up){
    if (!parent){
        SetLookAt(target,&world_up);
        return;
    }
     //Compute the target in world coordinates.
    vec3 delta = GetWorldPosition() - target ;

    //Rotate by the inverse of our current world rotation.
    //
    //Through a COPY, deliberately: GetWorldTransformScaleMatrix hands back a reference to the
    //parent's cached world matrix, and inverse_transform mutates in place - so inverting it
    //straight off the getter left the PARENT itself holding an inverted world transform, until
    //whatever next marked it transformed rebuilt the cache. Everything that read it in between
    //(the renderer included) got the inverse.
    fmat4 parent_world = parent->GetWorldTransformScaleMatrix();
    fmat4 r = parent_world.inverse_transform().rotationmatrix();
    delta = r * delta;
    vec3 rotated_up = r * world_up;

    SetLookAt(delta,&rotated_up);
}

//Move object by a vector
void Object::MoveBy(const vec3& delta){
    SetPosition(state.position + delta);
}

//Returns the vector by which is was moved.
vec3 Object::MoveForwardBy(float delta){
    vec3 d = GetForward() * delta;
    MoveBy(d);
    return d;
}

vec3 Object::MoveSidewaysBy(float delta){
    vec3 d = GetLeft() * delta;
    MoveBy(d);
    return d;
}

vec3 Object::MoveUpBy(float delta){
    vec3 d = GetUp() * delta;
    MoveBy(d);
    return d;
}

//Rotate on forward axis argument in radians
void Object::RollBy(float by){
    RotateAroundAxis(GetForward(),by);
}

//Rotate on up axis argument in radians
void Object::YawBy(float by){
    RotateAroundAxis(GetUp(),by);
}

//Rotate on left axis argument in radians
void Object::PitchBy(float by){
    RotateAroundAxis(GetLeft(),by);
}

//The size of the object in 3 dimensions
void Object::SetScale(const vec3& newscale){
    vec3 oldscale = state.scale;
    state.scale = newscale;
    state.f_was_transformed = true;
    //Colliders follow the visual: rescaled by the RATIO to the previous scale, so it doesn't
    //matter what size they were created at (colliders added after a SetScale are sized to the
    //already-scaled object by their callers, and stay right when the scale changes again).
#ifdef USE_PHYSICS
    if (physics && physics->body && physics->body->rigidbody){
        vec3 ratio(
            fabsf(oldscale.x) > 0.0001f ? newscale.x / oldscale.x : 1.0f,
            fabsf(oldscale.y) > 0.0001f ? newscale.y / oldscale.y : 1.0f,
            fabsf(oldscale.z) > 0.0001f ? newscale.z / oldscale.z : 1.0f);
        if (ratio.x != 1.0f || ratio.y != 1.0f || ratio.z != 1.0f){
            physics->ScaleColliders(ratio);
        }
    }
#else
    (void)oldscale;
#endif
}

vec3 Object::GetScale(){
    return state.scale;
}

//Turns this object's material NAMES into slot indices, once. See the block in Object.h.
void Object::ResolveMaterialNames(std::vector<Material>& global_list){
    if (!f_resolve_material_names){
        return;
    }
    f_resolve_material_names = false;
    int index = 0;
    for (std::string& mat_name:material_names){
        if (index >= NUM_MATERIAL_SLOTS){
            return;
        }
        //An empty name means "this slot was never set by name", so leave whatever index it has.
        //Skipped explicitly rather than relying on no material being called "" - which is what
        //this used to rely on, and is why setting a slot by index on an object whose names DID
        //resolve was silently undone while the same code on an object whose names did not resolve
        //worked perfectly.
        if (mat_name.empty()){
            index++;
            continue;
        }
        for (int global_index=0;global_index<global_list.size();global_index++){
            Material& global_mat = global_list.at(global_index);
            if (mat_name.compare(global_mat.name) == 0){
                material_slot[index] = global_index;
                //debug->Info("Picking material %s %i -> %i\n",mat.name.c_str(),index,global_index);
                break;
            }
        }
        index++;
    }
}

/*//TODO: Some kind of list thing, event.. whatever... that tells all object about the destruction of
another object.
//Could also maybe use ... smart pointers?
void Object::HandleObjectDestruction(){

}
*/

//Called by Physics
void Object::UpdatePhysicsState(){
    //Massages all the physics things.

    //If physics from colliders etc. was updated:
#ifdef USE_PHYSICS
    if (physics){

        vec3 physics_wp = physics->GetBodyWorldPosition();
        //We set the local position
        SetPosition(physics_wp,false);
        quat physics_q = physics->GetBodyWorldOrientation();
        //We set the local position
        SetRotation(physics_q,false);
    }
#endif

    //ApplyAnimation(animation_time_delta);

    for (Object* child:children) {
        child->UpdatePhysicsState();
    }
}

vec3 Object::GetCenterofMass(){
#ifdef USE_PHYSICS
    if (!physics){
        return vec3();
    }
    return physics->GetCenterofMass();
#else
    return vec3();
#endif
}

//Returns local position (within parent)
vec3 Object::GetPosition(){
    return state.position;
}

//Computes and gets the world position, by walking the transform chain.
//This used to have a second implementation for the physics thread, which composed
//parent_world_rotation * local_position + parent_world_position instead. That existed only
//because world_transform_scale_matrix was refreshed once per frame by the now-deleted
//Object::UpdateState, so it was stale everywhere except inside DrawFrame. The matrix is rebuilt
//on demand from `state` now, so it is live on either thread and one path serves both.
//Note the two were never equivalent: the composed version dropped any ancestor SCALE, so a
//child of a scaled object reported a world position the renderer did not draw it at. The
//matrix is what the renderer draws with, so it is the one that survives.
vec3 Object::GetWorldPosition(){
    return GetWorldTransformScaleMatrix().vertex[3].xyz();
}

vec3 Object::GetForward(){
    return state.rotation * ref_forward;
}

vec3 Object::GetWorldForward(){
    return GetWorldRotation() * ref_forward;
}

vec3 Object::GetUp(){
    return state.rotation * ref_up;
}

vec3 Object::GetWorldUp(){
    return GetWorldRotation() * ref_up;
}

vec3 Object::GetLeft(){
    return state.rotation * ref_left;
}

//Returns the local rotation
quat Object::GetRotation(){
    return state.rotation;
}

quat Object::WorldRotationToLocal(const quat& world_rotation_in){
    if (!parent){
        return world_rotation_in;
    }

    //We have a parent. Get it's rotation and apply in reverse order
    quat parent_rotation = parent->GetWorldRotation();
    quat local_rotation = parent_rotation.inverse() * world_rotation_in;

    return local_rotation;
}

//Calculate the single transformation matrix for rendering
void Object::UpdateTransformMatrix(){
    //We do in order:
    //scale, rotate, translate
    float size = 1.0;

    local_transform_scale_matrix.identity();
    local_transform_scale_matrix.vertex[0].x *= size * state.scale.x;
    local_transform_scale_matrix.vertex[1].y *= size * state.scale.y;
    local_transform_scale_matrix.vertex[2].z *= size * state.scale.z;

    fmat4 rotation_matrix;
    //Compute the rotation matrix from the rotation quaternion
    rotation_matrix = state.rotation.tofmat4();

    local_transform_scale_matrix = local_transform_scale_matrix * rotation_matrix;

    local_transform_scale_matrix.set_position(state.position);

    state.f_was_transformed = false;
}

fmat4& Object::GetLocalTransformScaleMatrix(){
    if (state.f_was_transformed){
        //Update local transform matrices
        UpdateTransformMatrix();
    }
    return local_transform_scale_matrix;
}

//Returns the total transformation matrix in world space for this frame
fmat4& Object::GetWorldTransformScaleMatrix(){
    if (state.f_was_transformed){
        //Update local transform matrices
        UpdateTransformMatrix();
    }
    if (parent){
        //We take the parents transform matrix, and we need to apply that.. in reverse order:
        fmat4 pwtsm = parent->GetWorldTransformScaleMatrix();
        world_transform_scale_matrix = local_transform_scale_matrix * pwtsm;
    }else{
        //The top parent in the chain will return it's own transform_scale_matrix
        world_transform_scale_matrix = local_transform_scale_matrix;
    }
	return world_transform_scale_matrix;
}

//Same as with matrices, rotate in reverse order
quat Object::GetWorldRotation(){
    if (!parent){
        return GetRotation();
    }

    //We have a parent. Get it's rotation and apply in reverse order
    //TODO CHECK
    quat parent_rotation = parent->GetWorldRotation();
    quat world_rotation =  parent_rotation * GetRotation();
    return world_rotation;
}

//Used by renderer to create batches for objects with same meshes
void Object::MarkForRenderBatch(){
    if (mesh){
        mesh->batch_num_instances++;
    }
}

void Object::ClearRenderBatch(){
    if (mesh){
        mesh->batch_num_instances = 0;
    }
}

//Put's all children and it's childrens children etc into a list
void Object::GetAllSubObjects(std::vector<Object*>& objects){
    objects.push_back(this);
    for (Object* child:children){
        child->GetAllSubObjects(objects);
    }
}

Object* Object::GetLastChild(){
    if (children.empty())
        return NULL;
    return children.back();
}

Object* Object::FindChild(std::string child_name){
    std::list<Object*>::iterator it = children.begin();

    for ( ; it != children.end(); ) {
        Object* child = *it;
        if (child->name.compare(child_name) == 0){
            return child;
        }
        if (Object* s = child->FindChild(child_name)){
            return s;
        }
        ++it;
    }
    return NULL;
}

Object* Object::GetParent(){
    return parent;
}

//Returns the specified child if there is one.
Object* Object::GetChild(int index){
    std::list<Object*>::iterator it = children.begin();
    int cnt = 0;
    for ( ; it != children.end(); ) {
        Object* child = *it;
        if (cnt == index){
            return child;
        }
        cnt++;
        ++it;
    }
    return NULL;
}

bool Object::AttachChild(Object* newchild){
    debug->Trace("Attaching child %p\n",newchild);
    if (!newchild){
        debug->Err("Unable to attach NULL as child.\n");
        return false;
    }
    debug->Trace("Attaching child newchild->parent %p\n",newchild->parent);
    //If the child had a parent before, detach it.
    if (newchild->parent){
        newchild->parent->DetachChild(newchild);
    }
    debug->Trace("Attaching child children.size()=%i\n",children.size());
    //newchild->child_index = children.size();
    children.push_back(newchild);
    newchild->parent = this;

    //Catches the "body first, parent second" ordering.
    if (newchild->physics){
        debug->Err("Object '%s' (id=%i) has a rigid body AND a parent '%s' (id=%i). "
                "Put the body on the parent and leave "
             "this child visual-only, or detach it with DetachChildToWorld.\n",
             newchild->name.c_str(),newchild->id,name.c_str(),id);
    }


    //Either we alway need to traverse a tree to find renderable objects from root.
    //Has the benefit of auto rendering if you add siblings
    //Or we add them here to objrenderer, where we need to also seperately delete them
    //We'll do the tree
    debug->Trace("Done Attaching child. children.size()=%i\n",children.size());
    return true;
}

//Removes child from array.
void Object::DetachChild(Object* targetchild){
    debug->Trace("Detaching child\n");

    std::list<Object*>::iterator it;
    for (it = children.begin();it != children.end();it++){
        if (*it == targetchild) {
            it = children.erase(it);
            //Clearing the back-pointer is the whole of the detach. Without it the child is gone
            //from this list but still names us as its parent, so GetWorldTransformScaleMatrix
            //keeps composing our transform into it forever and GetParent() lies. That went
            //unnoticed because the only caller was AttachChild, which overwrites parent on the
            //very next line - a standalone detach, which DetachChildToWorld does, needs this.
            targetchild->parent = NULL;
            debug->Trace("Detached child\n");
            return;
        }
    }
    debug->Fatal("Unable to detach child object id=%i from parent. %p from %p\n",targetchild->id, this, parent);
}

//Detaches a child and leaves it exactly where it was being drawn. See the header.
bool Object::DetachChildToWorld(Object* targetchild){
    if (!targetchild){
        debug->Err("DetachChildToWorld: unable to detach NULL.\n");
        return false;
    }
    if (targetchild->parent != this){
        debug->Err("DetachChildToWorld: '%s' is not a child of '%s'.\n",
                   targetchild->name.c_str(),name.c_str());
        return false;
    }

    //Both of these walk the parent chain, so they have to be read BEFORE it is cut.
    vec3 world_position = targetchild->GetWorldPosition();
    quat world_rotation = targetchild->GetWorldRotation();

    DetachChild(targetchild);

    //Now that parent is NULL these ARE the local transform. f_write_physics is left at its
    //default: if the child has a body it must be moved too, or the next UpdatePhysicsState would
    //overwrite both of these with the body's stale transform.
    targetchild->SetPosition(world_position);
    targetchild->SetRotation(world_rotation);

    //Ancestor SCALE is deliberately not baked. state.scale is a per-axis vec3, and a rotated
    //ancestor scale is not expressible as one, so there is no honest value to write - callers
    //that need it have to scale the child themselves.
    return true;
}

//--- Materials. See the block in Object.h for the invariant these maintain. -------------------

//By index: this IS the answer, so nothing may look a name up over it afterwards.
void Object::SetMaterialSlot(int slot, int material_id){
    if ((slot >= 0) && (slot < NUM_MATERIAL_SLOTS)){
        material_slot[slot] = material_id;
        f_resolve_material_names = false;
    }
}

int Object::GetMaterialSlot(int slot) const{
    if ((slot >= 0) && (slot < NUM_MATERIAL_SLOTS)){
        return material_slot[slot];
    }
    return -1;
}

//By name: the index is not known yet, so ask for a lookup on the next frame.
void Object::SetMaterialName(int slot, const std::string& name){
    if ((slot >= 0) && (slot < NUM_MATERIAL_SLOTS)){
        material_names[slot] = name;
        f_resolve_material_names = true;
    }
}

void Object::SetMaterialNames(const std::array<std::string,NUM_MATERIAL_SLOTS>& names){
    material_names = names;
    f_resolve_material_names = true;
}

const std::string& Object::GetMaterialName(int slot) const{
    static const std::string empty;
    if ((slot >= 0) && (slot < NUM_MATERIAL_SLOTS)){
        return material_names[slot];
    }
    return empty;
}

//Find materials from list in global list, and assign them to the material slots as they are
//ordered in the list. This is a resolve in its own right - just against the caller's list rather
//than against the names stored here - so it settles the slots and lowers the flag. Callers pair it
//with TakeMaterialNames(same list), which raises the flag; doing both leaves the object resolved
//and the stored names still there for anyone who wants to read them.
void Object::PickMaterials(std::vector<Material>& list, std::vector<Material>& global_list){
    for (int index=0;index<min((size_t)NUM_MATERIAL_SLOTS,list.size());index++){
        Material& mat = list.at(index);
        for (int global_index=0;global_index<global_list.size();global_index++){
            Material& global_mat = global_list.at(global_index);
            if (mat.name.compare(global_mat.name) == 0){
                material_slot[index] = global_index;
                debug->Info("Picking material %s %i -> %i\n",mat.name.c_str(),index,global_index);
                break;
            }
        }
    }
    f_resolve_material_names = false;
}

//Stores names of a supplied list of materials, one per slot in list order.
void Object::TakeMaterialNames(std::vector<Material>& list){
    int index = 0;
    for (Material& newmat:list){
        if (index >= NUM_MATERIAL_SLOTS){
            break;
        }
        material_names[index] = newmat.name;
        index++;
    }
    //Names were set, so they need looking up. This used NOT to raise the flag, which is why nine
    //places in IsoCell.cpp had to remember to raise it by hand after writing a name.
    f_resolve_material_names = true;
}

void Object::AddAnimation(Animation* animation){
    if (animation){
        animations.push_back(animation);
        animation->LinkObjects(this);
    }
}

Animation* Object::FindAnimation(const std::string& name){
    for (Animation* animation:animations){
        if (animation->name.compare(name) == 0){
            return animation;
        }
    }
    return NULL;
}

const char* Object::CurrentAnimationName(){
    return current_animation ? current_animation->name.c_str() : "None";
}

const char* Object::PreviousAnimationName(){
    if (!previous_animation){
        return "None";
    }
    return previous_animation->name.c_str();
}

void Object::SetBlendTime(const std::string& from, const std::string& to, float blend_time){
    for (AnimationBlendOverride& o : animation_blend_overrides){
        if (o.from == from && o.to == to){
            o.blend_time = blend_time;
            return;
        }
    }
    AnimationBlendOverride o;
    o.from = from;
    o.to = to;
    o.blend_time = blend_time;
    animation_blend_overrides.push_back(o);
}

float Object::LookupBlendTime(const std::string& from, const std::string& to){
    for (AnimationBlendOverride& o : animation_blend_overrides){
        if ((o.from.empty() || o.from == from) && o.to == to){
            return o.blend_time;
        }
    }
    return animation_transition_time_max;
}

void Object::SwitchToAnimation(const std::string& name){
    SwitchToAnimation(FindAnimation(name));
}

//Forcesfull switches to the specified animation. If NULL, will switch to default pose.
/*
    Which way, and how fast, the running clip goes. See the note at the declaration.

    Two lines, and the second is the one worth having: without it, every caller that reversed a
    finished one-shot would have to know that it had finished, and would have to write
    `animation_state` from outside to say so.
*/
void Object::SetAnimationRate(float rate){
    animation_rate = rate;
    if (current_animation && rate != 0.0f && animation_state == ANIMATION_STATE_PAUSED){
        animation_state = ANIMATION_STATE_PLAYING;
    }
}

void Object::SwitchToAnimation(Animation* animation){
    //No blend, so nothing is being faded out - and any blend that WAS running is abandoned here
    //rather than finished. That goes for a SUSTAINED blend too: asking for one clip outright is
    //not an answer that can be half-given.
    previous_animation = NULL;
    ClearBlend();
    if (!animation){
        current_animation = NULL;
        animation_state = ANIMATION_STATE_LOAD_DEFAULT_POSE;
        return;
    }
    current_animation = animation;
    current_animation->time_index = 0.0f;
    animation_state = ANIMATION_STATE_PLAYING;
}

bool Object::TransitionToAnimation(const std::string& name){
    dbg_desired_animation_name = name;
    Animation* animation = FindAnimation(name);
    if (!animation){
        debug->Warn("TransitionToAnimation: Animation %s not found\n",name.c_str());
        return false;
    }
    return TransitionToAnimation(animation);
}

//From whereever the current animation is, we attempt transition into the next animation.
bool Object::TransitionToAnimation(Animation* animation){
    if (!animation){
        previous_animation = NULL;
        ClearBlend();
        animation_state = ANIMATION_STATE_LOAD_DEFAULT_POSE;
        return true;
    }
    //A crossfade and a sustained blend are two different answers to "what is playing", and
    //running both would mean three clips mixed by two unrelated weights. The crossfade wins,
    //because it is the one that was just asked for.
    ClearBlend();

    /*
        Already blending. `current_animation` is where it is HEADED and `previous_animation` is what
        it is leaving, so both questions below are asked of the right slot - which is the whole
        point of naming them this way round.
    */
    if (animation_state == ANIMATION_STATE_TRANSITION ||
        animation_state == ANIMATION_STATE_TRANSITION_START){
        if (animation == current_animation){
            return true;    //already on its way there; asking again is not an error
        }
        if (animation == previous_animation){
            //Asked to go back where it came from. Rewind the blend rather than start a second one.
            debug->Trace("Rewinding transition back to %s\n",animation->name.c_str());
            animation_state = ANIMATION_STATE_TRANSITION_BACK;
            return true;
        }
        /*
            A THIRD clip, mid-blend. RETARGET: drop whichever of the two clips the pose is
            currently FURTHEST from, and blend from the other one to the new destination.

            This used to be refused, and refusing turned out to be the wrong answer for a game.
            The rules can change their mind faster than a crossfade lasts - the archer decelerates
            at 120 u/s^2, which is 2 units per tick, so a stop from a run crosses the entire
            walk band in ONE tick and asks for walk and then idle on consecutive ticks. A refusal
            there leaves her crossfading into a walk she no longer wants and looping it on the
            spot for ever, because nothing asks a second time for a clip it believes is playing.

            A one-deep QUEUE - the obvious alternative, and what the note on the fields used to
            point at - is worse for exactly the same reason. It would honour the stale walk in
            full and only then start the idle, so the character keeps walking for two blend
            lengths after coming to rest. Latency is not better than a small pop; it is a
            character that visibly disagrees with the game.

            The cost of retargeting is that the blend already applied toward the abandoned clip
            is thrown away, so the pose jumps by `factor` (or 1 - `factor`) of the difference
            between the two clips. Keeping the NEARER side is what makes that small: one tick
            into a nine-tick fade the pose is 11% of the way over, so 11% is the error. The
            worst case is a request landing exactly halfway, and the real fix for that is step 4,
            inertialization - blend the POSE DELTA out rather than the clips, and there is no
            abandoned blend to pay for because the source is a snapshot rather than a clip.
        */
        debug->Trace("Retargeting %s -> %s to %s at %.2f\n",
                     previous_animation ? previous_animation->name.c_str() : "NULL",
                     current_animation->name.c_str(),
                     animation->name.c_str(),animation_transition_factor);
        if (animation_transition_factor < 0.5f && previous_animation){
            //Mostly still the clip being left: keep it as the source and swap the destination.
            //The abandoned one rewinds, so entering it again later starts at its beginning -
            //the same courtesy a completed transition does its previous_animation.
            current_animation->time_index = 0.0f;
        }else{
            //Mostly the destination already: land the blend here and fade on from it.
            if (previous_animation){
                previous_animation->time_index = 0.0f;
            }
            previous_animation = current_animation;
        }
        current_animation = animation;
        animation_transition_blend_time = LookupBlendTime(previous_animation->name,
                                                          animation->name);
        animation_state = ANIMATION_STATE_TRANSITION_START;
        animation_transition_time = 0.0f;
        animation_transition_factor = 0.0f;
        return true;
    }

    if (current_animation == animation){
        return true;    //already playing it
    }

    if (current_animation && !current_animation->interruptible &&
        !current_animation->HasFinished() && animation_state == ANIMATION_STATE_PLAYING){
        debug->Trace("Cannot interrupt %s before it finishes\n",current_animation->name.c_str());
        return false;
    }

    if (!current_animation){
        //Nothing to fade out of, so there is nothing to blend - start it outright rather than
        //running a crossfade against an empty slot.
        SwitchToAnimation(animation);
        return true;
    }

    debug->Trace("Transitioning from %s to %s\n",current_animation->name.c_str(),
                 animation->name.c_str());

    //THE FLIP: the destination becomes current immediately, and what was playing becomes previous.
    previous_animation = current_animation;
    current_animation = animation;
    animation_transition_blend_time = LookupBlendTime(previous_animation->name,animation->name);
    animation_state = ANIMATION_STATE_TRANSITION_START;
    animation_transition_time = 0.0f;
    animation_transition_factor = 0.0f;
    return true;
}

//TODO: This is finetuned in PlayerCharacter. Could be fixed for normal objects
void Object::ApplyRootMotion(const RootMotionDelta& delta){
    //Nothing. See the note on the declaration: the delta has already done its other job, which is
    //posing the root bone, and a plain object does not own its own position.
    (void)delta;
}

void Object::LoadDefaultPose(){
    std::vector<Object*> objects;
    GetAllSubObjects(objects);
    for (Object* object:objects){
        //Only bones have a reference pose to go back to. dynamic_cast rather than a flag because
        //this runs once per state change, not per frame.
        Bone* bone = dynamic_cast<Bone*>(object);
        if (bone){
            bone->SetRotation(bone->reference_rotation);
            bone->SetPosition(bone->reference_position);
        }
    }
}

void Object::SetBlend(Animation* target, float factor, float phase_offset){
    if (!target || target == current_animation){
        ClearBlend();
        return;
    }
    /*
        Seed the shared phase from where the current clip already is, but only when ENTERING a
        blend. Re-seeding on every call would restart the cycle each time the weight moved, which
        is every tick in a blend space - the feet would stutter in place. Once the blend is
        running, its phase is the thing that persists and the clips follow it.
    */
    if (!blend_animation){
        blend_phase = 0.0f;
        if (current_animation && current_animation->duration > 0.0f){
            blend_phase = current_animation->time_index / current_animation->duration;
            blend_phase -= floorf(blend_phase);
        }
    }
    blend_animation = target;
    blend_factor = (factor < 0.0f) ? 0.0f : ((factor > 1.0f) ? 1.0f : factor);
    blend_phase_offset = phase_offset;
}

/*
    Set BOTH sides of a sustained blend at once, keeping the shared phase continuous.

    This exists because a blend space does not hold one pair for ever: as the speed rises past a
    rung the pair slides up - (walk, slow run) becomes (slow run, fast run) - and the clip that
    was the follower becomes the leader. Doing that through SetBlend would mean changing
    `current_animation`, which means a crossfade, which means the blend is torn down and rebuilt
    and the shared phase restarts. At exactly the moment the weight is 0 or 1 and the pose is
    entirely one clip, the feet would jump. Twice, on the way up.

    So the phase is re-based onto whichever clip SURVIVES the change:

      - the pair slid UP, and the new leader is the old follower: it was running at
        phase + offset, so that is the phase now.
      - the pair slid DOWN, and the new follower is the old leader: the new leader has to be
        placed so the old one lands where it already was, which is phase - offset.
      - the pair is being ENTERED from a single clip that becomes the follower: the same sum
        read the other way, phase - offset. This is the ladder's top end on the way back down.
      - nothing in common: seed from wherever the new leader's own playhead had got to.

    `follow` may be NULL, which leaves a single clip playing from the phase it had reached - that
    is the ladder's outer ends, where there is nothing left to blend with.
*/
void Object::SetBlendPair(Animation* lead, Animation* follow, float factor, float phase_offset){
    if (!lead){
        ClearBlend();
        return;
    }
    float phase = 0.0f;
    if (blend_animation && current_animation){
        if (lead == blend_animation){
            phase = blend_phase + blend_phase_offset;       //slid up
        }else if (lead == current_animation){
            phase = blend_phase;                            //same leader, weight moved
        }else{
            phase = (lead->duration > 0.0f) ? (lead->time_index / lead->duration) : 0.0f;
        }
        if (follow && follow == current_animation){
            phase = blend_phase - phase_offset;             //slid down: keep the old leader put
        }
    }else if (current_animation && follow == current_animation &&
              current_animation->duration > 0.0f){
        /*
            Coming from a SINGLE clip that is about to become the FOLLOWER.

            That is the ladder's ends read backwards: above the fastest gait one clip plays alone,
            and dropping back into the blend space makes it the follower carrying most of the
            weight. Seeding from the new LEADER's playhead - which is what the fallback below
            does - would leave the dominant clip jumping to wherever it happened to have been
            left, so solve for the phase that keeps IT still instead. The follower is posed at
            phase + offset, so that phase is where it is now, less the offset.
        */
        phase = (current_animation->time_index / current_animation->duration) - phase_offset;
    }else if (lead->duration > 0.0f){
        phase = lead->time_index / lead->duration;
    }
    phase -= floorf(phase);

    //Any crossfade in progress is abandoned rather than finished - see TransitionToAnimation.
    previous_animation = NULL;
    current_animation = lead;
    blend_animation = (follow == lead) ? NULL : follow;
    blend_factor = (factor < 0.0f) ? 0.0f : ((factor > 1.0f) ? 1.0f : factor);
    blend_phase_offset = phase_offset;
    blend_phase = phase;
    animation_state = ANIMATION_STATE_PLAYING;
    //With no follower the single-clip path takes over, and it advances time_index rather than
    //reading the phase - so hand it the time the phase says it should be at.
    if (!blend_animation){
        current_animation->time_index = phase * current_animation->duration;
    }
}

void Object::ClearBlend(){
    /*
        The clip that was current keeps the time the blend left it at, so dropping out of a blend
        carries straight on rather than restarting - which is what makes "blend space until the
        speed leaves the ladder, then a single clip" not visibly pop at the boundary.
    */
    blend_animation = NULL;
    blend_factor = 0.0f;
    blend_phase_offset = 0.0f;
}

/*
    One tick of a sustained blend. See the block on blend_animation in the header for what this is
    and why it is not a crossfade.

    The two clips are driven from ONE normalised phase so their footfalls stay in step, and the
    group advances at the INTERPOLATED duration so the stride rate eases between the two rather
    than snapping to whichever clip is nominally current. Each side's sampling window is worked out
    separately, because with a phase offset the follower wraps at a different moment than the
    leader - and a window that spans a wrap reports the wrap itself as an enormous root-motion
    delta, which is a character teleporting once per cycle.
*/
void Object::ApplyBlendedAnimation(float time_delta){
    Animation* lead = current_animation;
    Animation* follow = blend_animation;
    if (!lead || !follow){
        return;
    }
    float factor = blend_factor;

    float duration = lead->duration + (follow->duration - lead->duration) * factor;
    if (duration < 0.0001f){
        duration = 0.0001f;
    }

    float lead_was = blend_phase;
    blend_phase += (time_delta * animation_rate) / duration;
    blend_phase -= floorf(blend_phase);

    float follow_was = lead_was + blend_phase_offset;
    follow_was -= floorf(follow_was);
    float follow_now = blend_phase + blend_phase_offset;
    follow_now -= floorf(follow_now);

    //A phase that went BACKWARDS wrapped. Sample a zero-width window across it, exactly as the
    //single-clip path does on a loop, so the wrap is not reported as motion.
    float lead_from = (blend_phase < lead_was) ? blend_phase : lead_was;
    float follow_from = (follow_now < follow_was) ? follow_now : follow_was;

    float lead_new = blend_phase * lead->duration;
    float lead_old = lead_from * lead->duration;
    float follow_new = follow_now * follow->duration;
    float follow_old = follow_from * follow->duration;

    lead->time_index = lead_new;
    follow->time_index = follow_new;

    RootMotionDelta delta = lead->LerpRootMotion(follow,lead_old,lead_new,
                                                 follow_old,follow_new,factor);
    lead->Lerp(follow,lead_new,follow_new,factor);
    ApplyRootMotion(delta);
}

void Object::ApplyAnimation(float time_delta){
    if (f_animation_override){
        //Stepping through by hand, a tick at a time, from the debug UI. Anything left over after
        //the requested ticks simply does not advance.
        if (animation_override_ticks > 0){
            animation_override_ticks--;
        }else{
            return;
        }
    }

    if (!current_animation){
        animation_state = ANIMATION_STATE_INVALID;
    }
    if (animation_state == ANIMATION_STATE_LOAD_DEFAULT_POSE){
        LoadDefaultPose();
        animation_state = ANIMATION_STATE_INVALID;
        current_animation = NULL;
        previous_animation = NULL;
    }

    if (!current_animation){
        return;
    }

    if (animation_state == ANIMATION_STATE_PLAYING){
        //A sustained blend replaces the single-clip path entirely rather than layering on top of
        //it - both clips are sampled, and the one that happens to be `current` has no special
        //status beyond leading the shared phase.
        if (blend_animation){
            ApplyBlendedAnimation(time_delta);
            return;
        }
        bool f_did_rewind = false;
        float last_time_index = current_animation->time_index;
        //The rate is applied HERE and nowhere else - see the note on animation_rate. A rate of 0
        //leaves the index alone and still falls through to ApplyInterval below, which is what
        //makes "parked on a frame" a pose that is held rather than a pose that is merely left.
        current_animation->time_index += time_delta * animation_rate;
        if (current_animation->time_index > current_animation->duration){
            if (!current_animation->looped){
                /*
                    A clip that is not looped STOPS ON ITS LAST FRAME rather than snapping back.

                    This is what makes "play it once and let it finish" work at all - a death, a
                    door, a one-shot swing. Where a clip says what should follow it, that
                    transition is started here instead of pausing, which is how a chain of
                    connector clips runs without anything driving it per frame.
                */
                current_animation->time_index = current_animation->duration;
                if (!current_animation->auto_continue_to){
                    animation_state = ANIMATION_STATE_PAUSED;
                }else{
                    TransitionToAnimation(current_animation->auto_continue_to);
                }
            }else{
                current_animation->time_index -= current_animation->duration;
                f_did_rewind = true;
            }
        }else if (current_animation->time_index < 0.0f){
            /*
                The same two rules at the other end, for a clip running backwards.

                A one-shot stops on its FIRST frame and pauses - a door that has finished shutting
                is shut, and nothing should carry it past that. A looping clip wraps round to the
                end instead, so a walk played at -1 is a walk backwards rather than one step and a
                stop. `auto_continue_to` is not consulted: a chain of connector clips is a forward
                idea, and running one in reverse would be following it the wrong way.
            */
            if (!current_animation->looped){
                current_animation->time_index = 0.0f;
                animation_state = ANIMATION_STATE_PAUSED;
            }else{
                current_animation->time_index += current_animation->duration;
                f_did_rewind = true;
            }
        }

        /*
            Extract this tick's world-motion delta from the clip's root track, and write the
            corrected pose onto the root bone for the new time index.

            On a loop wraparound we sample a ZERO-WIDTH window, so the bone is still posed for the
            new (wrapped) time without the wrap itself being reported as a huge delta - which is
            what a character would otherwise do once per cycle: teleport back to the start.
        */
        float sample_prev_time = f_did_rewind ? current_animation->time_index : last_time_index;
        RootMotionDelta delta = current_animation->SampleRootMotion(sample_prev_time,
                                                                   current_animation->time_index);
        current_animation->ApplyInterval(current_animation->time_index);
        ApplyRootMotion(delta);

    }else if (animation_state == ANIMATION_STATE_TRANSITION_START){
        //One tick of bookkeeping, kept as its own state so that anything wanting to know a blend
        //has just begun has somewhere to hook in.
        debug->Trace("Transition start from %s to %s\n",
                     previous_animation ? previous_animation->name.c_str() : "NULL",
                     current_animation->name.c_str());
        animation_state = ANIMATION_STATE_TRANSITION;

    }else if (animation_state == ANIMATION_STATE_TRANSITION){
        if (!previous_animation){
            //Nothing to fade out of. Not an error - SwitchToAnimation clears it, and starting from
            //nothing takes this path - so just play what is current.
            animation_state = ANIMATION_STATE_PLAYING;
            return;
        }

        /*
            Both clips run while the blend does, and a blend can outlast either of them.

            The one being LEFT is clamped if it does not loop (a death should not restart under a
            crossfade) and wrapped if it does; the one being ENTERED always wraps. That last part is
            inherited behaviour and is arguably wrong for a one-shot destination - noted rather than
            changed, because this pass is about names.
        */
        float prev_last_time_index = previous_animation->time_index;
        bool f_prev_did_rewind = false;
        previous_animation->time_index += time_delta;
        if (previous_animation->time_index > previous_animation->duration){
            if (!previous_animation->looped){
                previous_animation->time_index = previous_animation->duration;
            }else{
                previous_animation->time_index -= previous_animation->duration;
                f_prev_did_rewind = true;
            }
        }

        float cur_last_time_index = current_animation->time_index;
        bool f_cur_did_rewind = false;
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            current_animation->time_index -= current_animation->duration;
            f_cur_did_rewind = true;
        }

        //0 is all previous, 1 is all current - which is how it reads now that the slots are named
        //for what they hold rather than for the order they were assigned in.
        animation_transition_factor = animation_transition_time / animation_transition_blend_time;
        if (animation_transition_blend_time == 0){
            animation_transition_factor = 1.0f;
        }

        RootMotionDelta delta = previous_animation->LerpRootMotion(current_animation,
            f_prev_did_rewind ? previous_animation->time_index : prev_last_time_index,
            previous_animation->time_index,
            f_cur_did_rewind ? current_animation->time_index : cur_last_time_index,
            current_animation->time_index,
            animation_transition_factor);
        previous_animation->Lerp(current_animation,previous_animation->time_index,
                                 current_animation->time_index,animation_transition_factor);
        ApplyRootMotion(delta);

        animation_transition_time += time_delta;
        if (animation_transition_time >= animation_transition_blend_time){
            animation_transition_time = animation_transition_blend_time;
            //Rewind the clip being left, so entering it again next time starts at its beginning.
            previous_animation->time_index = 0;
            previous_animation = NULL;
            animation_state = ANIMATION_STATE_PLAYING;
            debug->Trace("Transition complete. Now at %s\n",current_animation->name.c_str());
        }

    }else if (animation_state == ANIMATION_STATE_TRANSITION_BACK){
        /*
            Rewinding a blend that was asked to go back where it came from.

            NO ROOT MOTION IS APPLIED while rewinding, deliberately: running the extracted motion
            backwards would drag the character back across ground it already covered. The pose
            blends back, the world transform stays where it got to.
        */
        if (!previous_animation){
            debug->Warn("No transition to rewind\n");
            animation_state = ANIMATION_STATE_PAUSED;
            return;
        }

        previous_animation->time_index -= time_delta;
        if (previous_animation->time_index < 0){
            if (!previous_animation->looped){
                previous_animation->time_index = 0;
            }else{
                previous_animation->time_index += previous_animation->duration;
            }
        }
        current_animation->time_index -= time_delta;
        if (current_animation->time_index < 0){
            current_animation->time_index += current_animation->duration;
        }

        animation_transition_time -= time_delta;
        if (animation_transition_time <= 0){
            animation_transition_time = 0;
            /*
                THE REWIND ENDS BACK AT `previous_animation`, so that is what has to become current
                again - this swap is the one place the naming flip is not purely cosmetic.

                Before the flip, `current_animation` had never moved off the clip being left, so
                aborting a blend was just a matter of dropping the destination. Now the destination
                IS current from the moment the blend starts, so abandoning it means putting the
                clip we came back to where it belongs, and winding the abandoned one back to zero
                so that entering it again later starts at its beginning.
            */
            current_animation->time_index = 0;
            current_animation = previous_animation;
            previous_animation = NULL;
            animation_state = ANIMATION_STATE_PLAYING;
            debug->Trace("Transition rewind complete. Now at %s\n",current_animation->name.c_str());
        }else{
            animation_transition_factor = animation_transition_time / animation_transition_blend_time;
            if (animation_transition_blend_time == 0){
                animation_transition_factor = 0.0f;
            }
            previous_animation->Lerp(current_animation,previous_animation->time_index,
                                     current_animation->time_index,animation_transition_factor);
        }
    }
}

/*
    Collision filtering, remembered on the Object and delegated to Physics, which is what actually
    owns the colliders - see the block above the setters in core/physics/Physics.h.

    Call these whenever you like. Before AddPhysics, before the colliders, after them, twice: the
    Object keeps the value and AddPhysics hands it on, and Physics re-applies it to every collider
    made from then on. They used to return silently unless a body with colliders already existed,
    so "set the filter, then build the shape" - which is the order anyone writes - did nothing.
*/
/*
    The six below all keep their DECLARATIONS in both builds - they name only engine types, so
    guarding them would push an #ifdef into every caller for no gain. Each one already returns
    early when there is no body, and with USE_PHYSICS off there never is one, so the no-physics
    build gets the same behaviour an object without a rigid body has always had. Only the bodies
    that reach into rp3d are compiled out.
*/
void Object::SetCollisionCategoryBits(uint32_t bits){
    collision_category_bits = bits;
#ifdef USE_PHYSICS
    if (physics){
        physics->SetCollisionCategoryBits(bits);
    }
#endif
}

//This sets all categories that this object can collide with
void Object::SetCollideWithMaskBits(uint32_t bits){
    collide_with_bits = bits;
#ifdef USE_PHYSICS
    if (physics){
        physics->SetCollideWithMaskBits(bits);
    }
#endif
}

void Object::SetMass(float mass){
#ifdef USE_PHYSICS
    if (!physics){
        return;
    }
    physics->SetMass(mass); //also rescales the inertia tensor - see Physics::SetMass
#else
    (void)mass;
#endif
}

float Object::GetMass(){
#ifdef USE_PHYSICS
    if (!physics){
        return 0.0f;
    }
    return physics->body->rigidbody->getMass();
#else
    return 0.0f;
#endif
}

vec3 Object::GetVelocity(){
#ifdef USE_PHYSICS
    if (!physics){
        return vec3();
    }
	rp3d::Vector3 v = physics->body->rigidbody->getLinearVelocity();
	return vec3(v.x,v.y,v.z);
#else
    return vec3();
#endif
}

void Object::SetVelocity(const vec3& newvel){
#ifdef USE_PHYSICS
    if (!physics){
        return;
    }
    rp3d::Vector3 v = (rp3d::Vector3&)newvel;
	physics->body->rigidbody->setLinearVelocity(v);
#else
    (void)newvel;
#endif
}
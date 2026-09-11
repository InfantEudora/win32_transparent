#include "Object.h"
#include "Debug.h"

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
    if (physics){
        physics->world->rp_world->destroyRigidBody(physics->body->rigidbody);
    }

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
    if (physics){
        physics->SetActive(false);
    }
}

void Object::Show(){
    SetVisibility(true);
    if (physics){
        physics->SetActive(true);
    }
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

void Object::ResetPhysics(){
    if (physics){
        physics->SetVelocity(vec3());
        physics->SetAngularVelocity(vec3());
        physics->SetBodyWorldOrientation(quat().identity());
    }
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
    if (f_write_physics && physics){
        physics->SetBodyWorldOrientation(q);
    }
}

void Object::RotateBy(const quat& r){
    quat nq = r * state.rotation;
    SetRotation(nq);
}

void Object::SetPosition(const vec3& newpos,bool f_write_physics){
    state.f_was_transformed = true;
    state.position = newpos;
    if (f_write_physics && physics){
        physics->SetBodyWorldPosition(GetPosition());
    }
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
    if (physics && physics->body && physics->body->rigidbody){
        vec3 ratio(
            fabsf(oldscale.x) > 0.0001f ? newscale.x / oldscale.x : 1.0f,
            fabsf(oldscale.y) > 0.0001f ? newscale.y / oldscale.y : 1.0f,
            fabsf(oldscale.z) > 0.0001f ? newscale.z / oldscale.z : 1.0f);
        if (ratio.x != 1.0f || ratio.y != 1.0f || ratio.z != 1.0f){
            physics->ScaleColliders(ratio);
        }
    }
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
    if (physics){

        vec3 physics_wp = physics->GetBodyWorldPosition();
        //We set the local position
        SetPosition(physics_wp,false);
        quat physics_q = physics->GetBodyWorldOrientation();
        //We set the local position
        SetRotation(physics_q,false);
    }

    //ApplyAnimation(animation_time_delta);

    for (Object* child:children) {
        child->UpdatePhysicsState();
    }
}

vec3 Object::GetCenterofMass(){
    if (!physics){
        return vec3();
    }
    return physics->GetCenterofMass();
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

const char* Object::NextAnimationName(){
    if (!transition_to){
        return "None";
    }
    return transition_to->name.c_str();
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
void Object::SwitchToAnimation(Animation* animation){
    transition_to = NULL;
    if (!animation){
        current_animation = NULL;
        animation_state = ANIMATION_STATE_LOAD_DEFAULT_POSE;
        return;
    }
    current_animation = animation;
    current_animation->time_index = 0.0f;
    animation_state = ANIMATION_STATE_LOOPING;

}

void Object::TransitionToAnimation(const std::string& name){
    dbg_desired_animation_name = name;
    Animation* animation = FindAnimation(name);
    if (!animation){
        debug->Warn("TransitionToAnimation: Animation %s not found\n",name.c_str());
        return;
    }
    TransitionToAnimation(animation);
}

//From whereever the current animation is, we attempt transition into the next animation.
void Object::TransitionToAnimation(Animation* animation){
    if (!animation){
        transition_to = NULL;
        animation_state = ANIMATION_STATE_LOAD_DEFAULT_POSE;
        return;
    }

    Animation* target_animation = current_animation; //The animation we are in, or are transitioning to

    if (animation_state == ANIMATION_STATE_TRANSITION){
        if (animation == transition_to){
            debug->Info("Already transitioning to this animation\n");
            return;
        }
        if (transition_to && !transition_to->interruptible){
            debug->Info("Cannot interrupt transition to %s before it finishes\n",transition_to->name.c_str());
            return;
        }
        debug->Info("Currently Transition from %s to %s. Request Transition back to %s\n",current_animation ? current_animation->name.c_str() : "NULL",transition_to ? transition_to->name.c_str() : "NULL",animation->name.c_str());
        if (current_animation == animation){
            debug->Info("Rewinding transition back to %s\n",animation->name.c_str());
            animation_state = ANIMATION_STATE_TRANSITION_BACK;
        }else{
            debug->Warn("Cannot transition back to %s.\n",animation->name.c_str());
            animation_state = ANIMATION_STATE_PAUSED;
        }
        return;
        //We are in the middle of a transition. We can either continue to the current target animation,
        //rewind it, or (not yet handled) retarget it to a third animation.
    }

    if (target_animation == animation){
        //Already playing (or transitioning to) this animation - no-op.
        return;
    }

    if (target_animation && !target_animation->interruptible && !target_animation->HasFinished() && animation_state == ANIMATION_STATE_LOOPING){
        debug->Info("Cannot interrupt %s before it finishes\n",target_animation->name.c_str());
        return;
    }

    debug->Info("Transitioning from %s to %s\n",target_animation ? target_animation->name.c_str() : "NULL",animation->name.c_str());

    transition_to = animation;
    animation_transition_blend_time = LookupBlendTime(target_animation ? target_animation->name : "", animation->name);
    animation_state = ANIMATION_STATE_TRANSITION_START;
    animation_transition_time = 0.0f;
    animation_transition_factor = 0.0f;
}

//TODO: This is finetuned in PlayerCharacter. Could be fixed for normal objects
void Object::ApplyAnimation(float time_delta){
    //TODO check all the things
    if (f_animation_override){
        return;
    }

    if (!current_animation){
        //current_animation = next_animation;
    }
    if (!current_animation){
        return;
    }

    if (animation_state == ANIMATION_STATE_LOOPING){
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            current_animation->time_index -= current_animation->duration;
            //TODO: We the animation ends, we need to keep the last position... either by nesting object.. or something sinister
        }
        current_animation->ApplyInterval(current_animation->time_index);
    }else if (animation_state == ANIMATION_STATE_TRANSITION){
        animation_transition_time += time_delta;
        if (animation_transition_time > animation_transition_time_max){
            animation_transition_time = animation_transition_time;
            //current_animation = next_animation;
            animation_state = ANIMATION_STATE_LOOPING;
        }
    }
}

//This sets the category that this object belongs to.
void Object::SetCollisionCategoryBits(uint32_t bits){
    if (!physics){
        return;
    }
    bool f_active = physics->body->rigidbody->isActive();
	physics->body->rigidbody->setIsActive(false);
    for (uint32_t i=0;i<physics->body->rigidbody->getNbColliders();i++){
        physics->body->rigidbody->getCollider(i)->setCollisionCategoryBits(bits);
    }
    collision_category_bits = bits;
    physics->body->rigidbody->setIsActive(f_active);
}

//This sets all categories that this object can collide with
void Object::SetCollideWithMaskBits(uint32_t bits){
    if (!physics){
        return;
    }
    bool f_active = physics->body->rigidbody->isActive();
	physics->body->rigidbody->setIsActive(false);
    for (uint32_t i=0;i<physics->body->rigidbody->getNbColliders();i++){
        physics->body->rigidbody->getCollider(i)->setCollideWithMaskBits(bits);
    }
    collide_with_bits = bits;
    physics->body->rigidbody->setIsActive(f_active);
}

void Object::SetMass(float mass){
    if (!physics){
        return;
    }
    physics->SetMass(mass); //also rescales the inertia tensor - see Physics::SetMass
}

float Object::GetMass(){
    if (!physics){
        return 0.0f;
    }
    return physics->body->rigidbody->getMass();
}

vec3 Object::GetVelocity(){
    if (!physics){
        return vec3();
    }
	rp3d::Vector3 v = physics->body->rigidbody->getLinearVelocity();
	return vec3(v.x,v.y,v.z);
}

void Object::SetVelocity(const vec3& newvel){
    if (!physics){
        return;
    }
    rp3d::Vector3 v = (rp3d::Vector3&)newvel;
	physics->body->rigidbody->setLinearVelocity(v);
}
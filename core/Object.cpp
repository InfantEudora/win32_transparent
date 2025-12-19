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
    state_physics.rotation.identity();
    state.rotation.identity();
}

//Object object object object object ... I mean it makes sense to me: Duplication constructor
Object::Object(Object* object):Object(){
    debug->Info("Duplicating object Object %p into this %p\n",object,this);
    SetMesh(object->GetMesh());
    name = object->name;
    Physics* p = object->GetPhysics();
    if (p){
        AddPhysics(p->world);
        rp3d::CollisionShape* shape =  p->body->collider->getCollisionShape();
        reactphysics3d::Transform t = p->body->collider->getLocalToBodyTransform();
        physics->body->collider = physics->body->rigidbody->addCollider(shape,t);
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
    state_physics.f_visible = flag;
    state_physics.f_was_transformed = true;
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
        //We set the worldposition in the physics engine from local position
        physics->SetBodyWorldPosition(GetPosition());
        physics->SetStatic(true);
        physics->SetGravityEnabled(false);
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
    state_physics.f_was_transformed = true;
    state_physics.rotation = q;
    if (f_write_physics && physics){
        physics->SetBodyWorldOrientation(q);
    }
}

void Object::RotateBy(const quat& r){
    quat nq = r * state_physics.rotation;
    SetRotation(nq);
}

void Object::SetPosition(const vec3& newpos,bool f_write_physics){
    state_physics.f_was_transformed = true;
    state_physics.position = newpos;
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
    quat lq = quat::getquat(target,state_physics.position,up);
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

    //Rotate by the inverse of our current world rotation
    fmat4 r = parent->GetWorldTransformScaleMatrix().inverse_transform().rotationmatrix();
    delta = r * delta;
    vec3 rotated_up = r * world_up;

    SetLookAt(delta,&rotated_up);
}

//Move object by a vector
void Object::MoveBy(const vec3& delta){
    SetPosition(state_physics.position + delta);
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
    state_physics.scale = newscale;
    state_physics.f_was_transformed = true;
    //Maybe we should also scale the collider
    if (physics){
        //TODO
    }
}

vec3 Object::GetScale(){
    return state_physics.scale;
}

//Looks up material_names in a list and updates material_slots
void Object::UpdateMaterials(std::vector<Material>& global_list){
    if (!f_update_materials){
        return;
    }
    f_update_materials = false;
    int index = 0;
    for (std::string& mat_name:material_names){
        if (index >= NUM_MATERIAL_SLOTS){
            return;
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

//Copies physics state over to this state.
//Called by rendering
void Object::UpdateState(){
    state = state_physics_prev;

    state_completed = true;
    state_physics_prev_completed = 0;

    for (Object* child:children) {
        child->UpdateState();
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

    if (!f_animation_override){
        ApplyAnimation(animation_time_delta);
    }

    for (Object* child:children) {
        child->UpdatePhysicsState();
    }

    //Done
    state_physics_completed++;

    //Checks to see if we can swap state
    if (state_physics_prev_completed == 0){
        state_physics_prev = state_physics;
        state_physics_prev_completed = true;
        state_physics_completed = 0;
        state_physics.f_was_transformed = false;
    }

    //Dont know where to put this one yet.
    state_physics.f_was_transformed = true;
}

bool Object::PhysicsCompleted(){
    return !!state_physics_prev_completed;
}

//Returns local position (within parent)
vec3 Object::GetPosition(ObjectStateAccessType t){
    if (t == STATE_ACCESS_PHYSICS)
        return state_physics.position;
    //if (t == STATE_ACCESS_RENDERER)
    return state.position;
}

//Computes and gets the world position. Currently always reads from render state
vec3 Object::GetWorldPosition(ObjectStateAccessType t){
    fmat4 wt = GetWorldTransformScaleMatrix();
    return world_transform_scale_matrix.vertex[3].xyz();
}

vec3 Object::GetForward(){
    return state_physics.rotation * ref_forward;
}

vec3 Object::GetUp(){
    return state_physics.rotation * ref_up;
}

vec3 Object::GetWorldUp(){
    //TODO
    return vec3();
}

vec3 Object::GetLeft(){
    return state_physics.rotation * ref_left;
}

//Returns the local rotation
quat Object::GetRotation(){
    return state_physics.rotation;
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

    //state.f_was_transformed = false;
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
    quat world_rotation = GetRotation() * parent_rotation;
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

    //Either we alway need to traverse a tree to find renderable objects from root.
    //Has the benefit of auto rendering if you add siblings
    //Or we add them here to objrenderer, where we need to also seperately delete them
    //We'll do the tree
    debug->Trace("Done Attaching child. children.size()=%i\n",children.size());
    return true;
}

//Removes child from array.
void Object::DetachChild(Object* targetchild){
    debug->Trace("Child already has a parent, detaching\n");

    std::list<Object*>::iterator it;
    for (it = children.begin();it != children.end();it++){
        if (*it == targetchild) {
            it = children.erase(it);
            debug->Trace("Detached child\n");
            return;
        }
    }
    debug->Fatal("Unable to detach child object id=%i from parent. %p from %p\n",targetchild->id, this, parent);
}

//Find materials from list in global list, and assign them to the material slots as they are ordered in the list
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
}

//Stores names of a supplied list of materials.
void Object::TakeMaterialNames(std::vector<Material>& list){
    int index = 0;
    for (Material& newmat:list){
        if (index >= NUM_MATERIAL_SLOTS){
            return;
        }
        material_names[index] = newmat.name;
        index++;
    }
}

void Object::AddAnimation(Animation* animation){
    if (animation){
        animations.push_back(animation);
        animation->LinkObjects(this);
    }
}

void Object::SetNextAnimation(const std::string& name){
    SetNextAnimation(FindAnimation(name));
}

void Object::SetNextAnimation(Animation* animation){
    if (!animation){
        next_animation = NULL;
        return;
    }
    //If this is a new one, we reset it to 0. Otherwise, leave it.
    if (next_animation != animation){
        next_animation = animation;
        next_animation->time_index = 0;
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

void Object::ProceedToNextAnimation(){
    if (next_animation == current_animation){
        return;
    }
    if (animation_state != ANIMATION_STATE_TRANSITION){
        animation_state = ANIMATION_STATE_TRANSITION;
        animation_transition_time = 0.0f;
    }
}

void Object::ApplyAnimation(float time_delta){
    if (!current_animation){
        current_animation = next_animation;

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
            current_animation = next_animation;
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
    physics->body->rigidbody->setMass(mass);
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
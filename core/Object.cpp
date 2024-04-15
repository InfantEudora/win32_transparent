#include "Object.h"
#include "Debug.h"

static Debugger* debug = new Debugger("Object",DEBUG_INFO);

objectid_t Object::object_ids = 0;
vec3 Object::ref_up = vec3(0,1,0);
vec3 Object::ref_left = vec3(1,0,0);
vec3 Object::ref_forward = vec3(0,0,1);

Object::Object(){
    GenerateUniqueID();
    world_transform_scale_matrix.identity();
    state_physics.rotation.identity();
}

Object::~Object(){
    debug->Info("Destroyed Object %p\n",this);
    DeleteMesh();
}

void Object::DeleteMesh(){
    if (mesh){
        debug->Info("DeleteMesh: num_references=%i\n",mesh->num_references);
        mesh->num_references--;
        if (mesh->num_references == 0){
            delete mesh;
        }
    }
    mesh = NULL;
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

void Object::SetMesh(Mesh* _mesh){
    mesh = _mesh;
    mesh->num_references++;
}

int32_t Object::GetMeshBatchIndex(){
    if (mesh){
        return mesh->batch_index;
    }
    return -1;
}

//Set's this object's mesh index when batched
void Object::SetMeshBatchIndex(int32_t index){
    if (mesh){
        mesh->batch_index = index;
    }
}

void Object::RotateAroundAxis(const vec3& target_axis,float by){
    RotateBy(quat(target_axis,by));
}

//Update objects rotation with supplied quaternion.
void Object::SetRotation(const quat& q){
    state_physics.f_was_transformed = true;
    state_physics.rotation = q;
}

void Object::RotateBy(const quat& r){
    quat nq = r * state_physics.rotation;
    SetRotation(nq);
}

//Move to new position. This preserves rotation. When f_keep_lookat=true, the rotation will update to
//look at the old location
void Object::SetPosition(const vec3& newpos){
    state_physics.f_was_transformed = true;
    vec3 delta = state_physics.position - newpos;
    state_physics.position = newpos;
}

//Look at target from current position. Optional up can be supplied, otherwise will use ref_up.
//Target is in local space.
void Object::SetLookAt(const vec3& target, vec3* optional_up){
    vec3 up;
    if (optional_up){
        up = *optional_up;
    }else{
        up = ref_up;
    }
    state_physics.rotation = quat::getquat(target,state_physics.position,up);
}

//Move object by a vector
void Object::MoveBy(const vec3& delta){
    SetPosition(state_physics.position + delta);
}

void Object::MoveForwardBy(float delta){
    vec3 d = GetForward() * delta;
    MoveBy(d);
}

void Object::MoveSidewaysBy(float delta){
    vec3 d = GetLeft() * delta;
    MoveBy(d);
}

void Object::MoveUpBy(float delta){
    vec3 d = GetUp() * delta;
    MoveBy(d);
}

//Rotate on forward axis
void Object::RollBy(float by){
    RotateAroundAxis(GetForward(),by);
}

//The size of the object in 3 dimensions
void Object::SetScale(const vec3& newscale){
    state_physics.scale = newscale;
    state_physics.f_was_transformed = true;
}

vec3 Object::GetScale(){
    return state_physics.scale;
}

//Copies physics state over to this state.
void Object::UpdateState(){
    state = state_physics_prev;

    state_completed = true;
    state_physics_prev_completed = 0;

    for (Object* child:children) {
        child->UpdateState();
    }
}

void Object::UpdatePhysicsState(){
    //Massages all the physics things.
    //HERE

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
    //state_physics.f_was_transformed = false;
}

bool Object::PhysicsCompleted(){
    return !!state_physics_prev_completed;
}

vec3 Object::GetPosition(){
    return state_physics.position;
}

vec3 Object::GetForward(){
    return state_physics.rotation * ref_forward;
}

vec3 Object::GetUp(){
    return state_physics.rotation * ref_up;
}

vec3 Object::GetLeft(){
    return state_physics.rotation * ref_left;
}

bool Object::IsHovered(){
    return f_mouse_over;
}

//Calculate the single transformation matrix for rendering
void Object::UpdateTransformMatrix(){
    //We do in order:
    //scale, rotate, translate
    float size = 1.0;

    world_transform_scale_matrix.identity();
    world_transform_scale_matrix.vertex[0].x *= size * state.scale.x;
    world_transform_scale_matrix.vertex[1].y *= size * state.scale.y;
    world_transform_scale_matrix.vertex[2].z *= size * state.scale.z;

    fmat4 rotation_matrix;
    //Compute the rotation matrix from the rotation quaternion
    rotation_matrix = state.rotation.tofmat4();

    world_transform_scale_matrix = world_transform_scale_matrix * rotation_matrix;

    world_transform_scale_matrix.set_position(state.position);
}

//Returns the total transformation matrix in world space for this frame
fmat4& Object::GetWorldTransformScaleMatrix(){
    if (state.f_was_transformed){
        //Update local transform matrices
        UpdateTransformMatrix();
    }
	return world_transform_scale_matrix;
}

//Returns if this object would render in a certain state.
//Empty objects don't render.
bool Object::WouldRender(){
    if (!mesh){
        return false;
    }
    return true;
}

void Object::MarkForRender(){
    if (mesh){
        mesh->batch_num_instances++;
    }
}

//Put's all children and it's childrens children etc into a list
void Object::GetAllSubObjects(std::vector<Object*>*objects){
    for (Object* child:children){
        objects->push_back(child);
        child->GetAllSubObjects(objects);
    }
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
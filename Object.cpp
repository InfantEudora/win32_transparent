#include "Object.h"

objectid_t Object::object_ids = 0;

Object::Object(){
    GenerateUniqueID();
    world_transform_scale_matrix.identity();
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

void Object::Rotate(){
    state_physics.f_was_transformed = true;
    //Modify the cube
    state_physics.rotation += state_physics.rot_speed;
    MoveBy(vec3(0.005 * sin(state_physics.rotation),0.005 * cos(state_physics.rotation),0));
    vec3 target_axis = vec3(0,1,0);
    state_physics.mat_rotation.set_rotation(target_axis,state_physics.rotation);
}

//Move to new position, optionally change lookat as well
void Object::SetPosition(const vec3& newpos){
    state_physics.f_was_transformed = true;
    vec3 delta = state_physics.position - newpos;
    state_physics.position = newpos;
    SetLookat(lookat-delta);
}

//Set the new lookat position
void Object::SetLookat(const vec3& target){
    lookat = target;
}

//Move object by a vector
void Object::MoveBy(const vec3& delta){
    SetPosition(state_physics.position + delta);
}

//The size of the object in 3 dimensions
void Object::SetScale(const vec3& newscale){
    scale = newscale;
    state_physics.f_was_transformed = true;
}


void Object::SetRotation(float newrot){
    state_physics.rotation = newrot;
}

void Object::SetRotationSpeed(float newspeed){
    state_physics.rot_speed = newspeed;
}

void Object::SetMouseOver(bool state){
    f_mouse_over = state;
}

//Copies physics state over to this state.
void Object::UpdateState(){
    state.position          = state_physics_prev.position;
    state.f_was_transformed = state_physics_prev.f_was_transformed;
    state.rotation          = state_physics_prev.rotation;
    state.rot_speed         = state_physics_prev.rot_speed;
    state.mat_rotation      = state_physics_prev.mat_rotation;

    state.completed = true;
    state_physics_prev.completed = 0;
}

void Object::UpdatePhysicsState(){
    //Massages all the physics things.
    //HERE

    //Done
    state_physics.completed++;

    //Checks to see if we can swap state
    if (state_physics_prev.completed == 0){
        state_physics_prev.position             = state_physics.position;
        state_physics_prev.f_was_transformed    = state_physics.f_was_transformed;
        state_physics_prev.rotation             = state_physics.rotation;
        state_physics_prev.rot_speed            = state_physics.rot_speed;
        state_physics_prev.mat_rotation         = state_physics.mat_rotation;
        state_physics_prev.completed = true;
        state_physics.completed = 0;
    }

    //Dont know where to put this one yet.
    //state_physics.f_was_transformed = false;
}

bool Object::PhysicsCompleted(){
    return !!state_physics_prev.completed;
}

vec3& Object::GetPosition(){
    return state.position;
}

bool Object::IsHovered(){
    return f_mouse_over;
}

//Calculate the single transformation matrix for rendering
void Object::UpdateTransformMatrix(){
    //We do in order:
    //scale, rotate, translate
    float size = 1.0 + 0.1 * sin(state.rotation);

    world_transform_scale_matrix.identity();
    world_transform_scale_matrix.vertex[0].x *= size * scale.x;
    world_transform_scale_matrix.vertex[1].y *= size * scale.y;
    world_transform_scale_matrix.vertex[2].z *= size * scale.z;

    fmat4 rotation_matrix;
    rotation_matrix.rotationmatrix(state.mat_rotation);

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

#include "Object.h"

objectid_t Object::object_ids = 0;

Object::Object(){
    GenerateUniqueID();
    world_transform_scale_matrix.identity();
    state_physics.rotation.identity();
    state_physics.mat_rotation.identity();
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

//TODO: Remove me. We need a quaternion to represent object orientation....?
void Object::RotateAroundAxis(const vec3& target_axis,float by,bool f_lookat){
    state_physics.f_was_transformed = true;

    //We get the quaternion representing this rotation:
    quat r;
    r.set_rotation(target_axis,by);

    //We multiply by the current object rotation
    quat nq = r * state_physics.rotation;
    state_physics.rotation = nq;

    //state_physics.mat_rotation.set_rotation(target_axis,state_physics.rotation);
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



void Object::SetRotationSpeed(float newspeed){
    state_physics.rot_speed = newspeed;
}

//Copies physics state over to this state.
void Object::UpdateState(){
    state = state_physics_prev;

    state_completed = true;
    state_physics_prev_completed = 0;
}

void Object::UpdatePhysicsState(){
    //Massages all the physics things.
    //HERE

    //Done
    state_physics_completed++;

    //Checks to see if we can swap state
    if (state_physics_prev_completed == 0){
        state_physics_prev = state_physics;
        state_physics_prev_completed = true;
        state_physics_completed = 0;
    }

    //Dont know where to put this one yet.
    //state_physics.f_was_transformed = false;
}

bool Object::PhysicsCompleted(){
    return !!state_physics_prev_completed;
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
    float size = 1.0;




    world_transform_scale_matrix.identity();
    world_transform_scale_matrix.vertex[0].x *= size * scale.x;
    world_transform_scale_matrix.vertex[1].y *= size * scale.y;
    world_transform_scale_matrix.vertex[2].z *= size * scale.z;

    fmat4 rotation_matrix;
    //Compute the rotation matrix from the rotation quaternion
    //rotation_matrix.rotationmatrix(state.mat_rotation);
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

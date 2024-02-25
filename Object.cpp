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
    f_was_transformed = true;
    //Modify the cube
    rotation += rot_speed;
    MoveBy(vec3(0.005 * sin(rotation),0.005 * cos(rotation),0));
    vec3 target_axis = vec3(0,1,0);
    mat_rotation.set_rotation(target_axis,rotation);
}

//Move to new position, optionally change lookat as well
void Object::SetPosition(const vec3& newpos){
    f_was_transformed = true;
    vec3 delta = position - newpos;
    position = newpos;
    SetLookat(lookat-delta);
}

//Set the new lookat position
void Object::SetLookat(const vec3& target){
    lookat = target;
}

//Move object by a vector
void Object::MoveBy(const vec3& delta){
    SetPosition(position + delta);
}

//The size of the object in 3 dimensions
void Object::SetScale(const vec3& newscale){
    scale = newscale;
    f_was_transformed = true;
}

//Calculate the single transformation matrix
void Object::UpdateTransformMatrix(){
    //We do in order:
    //scale, rotate, translate
    float size = 1.0 + 0.1 * sin(rotation);

    world_transform_scale_matrix.identity();
    world_transform_scale_matrix.vertex[0].x *= size * scale.x;
    world_transform_scale_matrix.vertex[1].y *= size * scale.y;
    world_transform_scale_matrix.vertex[2].z *= size * scale.z;

    fmat4 rotation_matrix;
    rotation_matrix.rotationmatrix(mat_rotation);

    world_transform_scale_matrix = world_transform_scale_matrix * rotation_matrix;

    world_transform_scale_matrix.set_position(position);

    //The object is now static. This get's reset upon moving or rotating the object.
    f_was_transformed = false;
}

//Returns the total transformation matrix in world space for this frame
fmat4& Object::GetWorldTransformScaleMatrix(){
    if (f_was_transformed){
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

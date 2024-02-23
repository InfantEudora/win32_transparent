#include "Object.h"

objectid_t Object::object_ids = 0;

Object::Object(){
    GenerateUniqueID();
    world_transform_scale_matrix.identity();
    up = vec3(0,1,0);
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
    rotation += 0.01f;
    MoveBy(vec3(0.001,0,0));
    vec3 target_axis = vec3(0,1,0);
    mat_rotation.set_rotation(target_axis,rotation);
}

//Move to new position, optionally change lookat as well
void Object::SetPosition(const vec3& newpos){
    f_was_transformed = true;
    position = newpos;
}

//Move object by a vector
void Object::MoveBy(const vec3& delta){
    SetPosition(position + delta);
}

//Calculate the single transformation matrix
void Object::UpdateTransformMatrix(){
    //We do in order:
    //scale, rotate, translate
    world_transform_scale_matrix.rotationmatrix(mat_rotation);
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
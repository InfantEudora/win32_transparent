#ifndef _BONE_H_
#define _BONE_H_

#include "Object.h"
/*
    Bone to make up  a skeleton that will animate skinned meshes.
    Might as well be an object that we can render. All of the children may only be bones.
*/
class Bone : public virtual Object{
public:
    Bone();
    int bone_index = -1;
    int bone_unpacked_index = -1;
    int node_index = -1;
    fmat4 inverse_bind_matrix;

    // The mesh of a bone is only to visualise the bone when editing poses.
};

#endif
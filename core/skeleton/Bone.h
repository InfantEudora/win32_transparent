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
    float initial_length = 0.0f;
    float length = 0.0f;

    vec3 GetHeadWorldPosition();    // Where the bone starts and attaches to parent
    vec3 GetTailWorldPosition();    // Where the bone ends and children sit

    Bone* parent_bone = NULL;
    Bone* child_bone = NULL;

    //Chould be called when placed in skeleton to store bone references and set inital values
    void SetReferences();
    void SetInitialLength();

    // The mesh of a bone is only to visualise the bone when editing poses.
    // And there may be some more debug visualisations
    // Like a constraint thing.

    /*
        IKExtend puts the bone end position at target (world position)
        Depth specifies the number of bones that should follow it.
        Decay: each subsequent bone will use decay factor as influence.
    */
    void IKExtend(const vec3& taget,int depth,float decay);

    //Tunable UI parameters per bone

};

#endif
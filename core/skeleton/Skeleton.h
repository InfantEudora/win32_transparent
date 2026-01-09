#ifndef _SKELETON_H_
#define _SKELETON_H_

#include "Bone.h"
/*
    A collection of bones as children
*/
class Skeleton : public virtual Object{
public:
    Skeleton();
    int num_bones = 0;  // The expected number of bones to be found in children.

    std::string root_bone_name;

    // The skinnedmesh of a skeleton is the mesh that will be deformed.
    // The bones are the children

    void GetAllBones(Object* child, std::vector<Bone*>&bones);
    Bone* FindBone(const std::string name);
};

#endif
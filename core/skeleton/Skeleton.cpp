#include "Skeleton.h"

#include "Debug.h"
static Debugger *debug = new Debugger("Skeleton", DEBUG_INFO);

Skeleton::Skeleton():Object(){

}

void Skeleton::GetAllBones(Object* object, std::vector<Bone*>&bones){
    //On first iteration, child may be the skeleton itself, or a root bone.
    //Or there can be multiple loose bones... who knows

    Bone* bone = dynamic_cast<Bone*>(object);
    if (bone){
        bones.push_back(bone);
    }

    //Check all the children
    for (Object* child:object->children){
        GetAllBones(child,bones);
    }
}

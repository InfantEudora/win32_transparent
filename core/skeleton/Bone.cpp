#include "Bone.h"

#include "Debug.h"
static Debugger *debug = new Debugger("Bone", DEBUG_ALL);

Bone::Bone():Object(){

}

//When are you reorganising bones?
void Bone::SetReferences(){
    parent_bone = dynamic_cast<Bone*>(parent);
    Object* child = GetChild(0);
    child_bone = dynamic_cast<Bone*>(child);
    SetInitialLength();
}

//
void Bone::SetInitialLength(){
    //If we have a child bone, that defines our legth.
    //And that is also our head position.
    if (child_bone){
        initial_length = child_bone->GetPosition().length();
        length = initial_length;
    }
}

void Bone::IKExtend(const vec3& target,int depth,float decay){
    debug->Info("Extending Bone %s len=%.2f by factor %.1f\n",name.c_str(),length, decay);
    debug->Info(" Target wp : %.2f %.2f %.2f\n",target.x,target.y,target.z);
    vec3 tail_wp = GetTailWorldPosition();
    debug->Info(" Tail wp   : %.2f %.2f %.2f\n",tail_wp.x,tail_wp.y,tail_wp.z);

    //The bone now points somewhere in space
    quat q = GetWorldRotation();



    if (parent_bone && (depth > 0)){
        vec3 new_target;
        parent_bone->IKExtend(new_target,depth-1,decay);
    }
}

vec3 Bone::GetHeadWorldPosition(){
    return GetWorldPosition();
}

vec3 Bone::GetTailWorldPosition(){
    if (child_bone){
        return child_bone->GetWorldPosition();
    }
    return vec3();
}
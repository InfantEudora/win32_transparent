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

void Bone::SetReferenceRotation(const quat& q){
    reference_rotation = q;
}

void Bone::IKExtend(const vec3& target,int depth,float decay, std::vector<Bone*>* chain){
    if (!chain){
        //First call, create chain vector
        chain = new std::vector<Bone*>();
    }
    chain->push_back(this);

    //We start at the tail, and work our way up the chain, and remember which child we came from.
    if (parent_bone && (depth > 0)){
        debug->Info(" Bone %s passing IK extend to parent %s\n",name.c_str(),parent_bone->name.c_str());
        parent_bone->IKExtend(target,depth-1,decay,chain);
        return;
    }

    //We are at the top of the chain now.
    if (chain){
        int decay_steps = chain->size();
        for (std::reverse_iterator<std::vector<Bone*>::iterator> it = chain->rbegin(); it != chain->rend(); ++it) {
            decay_steps--;
            float resulting_decay = pow(decay, decay_steps);
            Bone* b = *it;
            debug->Info(" Chain bone: %s\n",b->name.c_str());
            debug->Info("Extending Bone %s len=%.2f by factor %.1f\n",b->name.c_str(),length, resulting_decay);
            debug->Info(" Target wp : %.2f %.2f %.2f\n",target.x,target.y,target.z);
            vec3 tail_wp = b->GetTailWorldPosition(STATE_ACCESS_PHYSICS);
            debug->Info(" Tail wp   : %.2f %.2f %.2f\n",tail_wp.x,tail_wp.y,tail_wp.z);

            //The bone now points somewhere in space
            //quat q = b->GetWorldRotation();

            //And it should rotate to point at target
            quat q_target = quat::getquat(b->GetWorldPosition(STATE_ACCESS_PHYSICS),target,vec3(0,1,0));
            //We now have the target rotation in world space.
            //Convert to local space
            quat local_rotation = b->WorldRotationToLocal(q_target);

            //This target quaternion makes the bone look at the target, with the side.
            //But we need it to point with it's head towards the target.
            quat pitch = quat(vec3(1,0,0),TYPE_PI/2);
            local_rotation = local_rotation * pitch;
            local_rotation.normalize();

            b->animation_mask = 0.0f;
            b->SetRotation(local_rotation);
        }
    }
}

vec3 Bone::GetHeadWorldPosition(ObjectStateAccessType t){
    return GetWorldPosition(t);
}

vec3 Bone::GetTailWorldPosition(ObjectStateAccessType t){
    if (child_bone){
        return child_bone->GetWorldPosition(t);
    }
    return vec3();
}
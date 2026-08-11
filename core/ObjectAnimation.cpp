#include "ObjectAnimation.h"


#include "Debug.h"
static Debugger *debug = new Debugger("ObjectAnimation", DEBUG_INFO);

ObjectAnimationKeyFrame::ObjectAnimationKeyFrame(){

}

ObjectAnimationKeyFrame::ObjectAnimationKeyFrame(ObjectAnimationKeyFrame* target){
    time = target->time;
    position = target->position;
    rotation = target->rotation;
    scale = target->scale;

    f_position = target->f_position;
    f_rotation = target->f_rotation;
    f_scale = target->f_scale;
}

ObjectAnimation::ObjectAnimation(){

}

Animation::Animation(){

}

void Animation::Play(float time_delta){
    time_index += time_delta;
    if (looped){
        while (time_index > duration){
            time_index -= duration;
        }
        while (time_index < 0.0f){
            time_index += duration;
        }
    }else{
        if (time_index > duration){
            time_index = duration;
        }
        if (time_index < 0.0f){
            time_index = 0.0f;
        }
    }
    ApplyInterval(time_index);
}

bool Animation::HasFinished(){
    if (looped){
        return false;
    }
    if (time_index >= duration){
        return true;
    }
    return false;
}

//Animation contains object names. We look them up and store references.
void Animation::LinkObjects(Object* root){
    std::vector<Object*>objects;
    root->GetAllSubObjects(objects);
    int count = 0;
    for (ObjectAnimation* object_animation:object_animations){
        if (root->name.compare(object_animation->target_name) == 0){
            //modifies_root_object = true;
            //???
        }
        for (Object* object:objects){
            if (object->name.compare(object_animation->target_name) == 0){
                //debug->Info("Animation: Linking target %s to animation %s\n",object->name.c_str(),name.c_str());
                object_animation->target = object;
                count++;
                break;
            }
        }
    }
    debug->Info("Animation: Linked %i objects from %s to animation %s\n",count,root->name.c_str(),name.c_str());
}

//Note that animation can now have a different target object.
void Animation::ApplyIntervalOnto(ObjectAnimation* object_animation, Object* target, float interval){
    if (!target){
        return;
    }
    if (!object_animation){
        return;
    }
    //debug->Info("Animation: Applying target %s at interval %.3f\n",target->name.c_str(),interval);

    ObjectAnimationKeyFrame* keyframe = object_animation->GetClosestKeyframe(interval);
    if (!keyframe){
        return;
    }
    if (keyframe->f_rotation){
        //Set rotation forces the bone into a specific rotation, ignoring existing rotation.
        if (target->animation_mask < 1.0f){
            quat rot = quat::slerp(target->GetRotation(),keyframe->rotation,target->animation_mask);
            target->SetRotation(rot);
        }else{
            target->SetRotation(keyframe->rotation);
        }
    }
    if (keyframe->f_position){
        if ((target->animation_mask > 0.0f) && (target->position_mask > 0.0f)){
            target->SetPosition(keyframe->position);
        }
    }
    if (keyframe->f_scale){
        debug->Fatal("TODO: Implement animation scaling\n");
    }
    if (keyframe->f_shapekeys){
        int index = 0;
        for (float weight:keyframe->shapekey_weights){
            target->SetShapekey(index,weight);
            index++;
        }
    }
}

void Animation::Lerp(Animation* target,float this_interval, float target_interval, float factor){
    if (!target){
        return;
    }

    if (target->object_animations.size() != object_animations.size()){
        //We can Lerp if we find the animation with the least amount of objects,
        //and map those to the other animation.
        //TODO

        debug->Err("Lerp on these animations are incompatible (%s -> %s)\n",name.c_str(),target->name.c_str());
        return;
    }

    for (int i=0;i<object_animations.size();i++){
        ObjectAnimation* this_object_animation = object_animations.at(i);
        ObjectAnimation* target_object_animation = target->object_animations.at(i);

        ObjectAnimationKeyFrame* start_keyframe = this_object_animation->GetClosestKeyframe(this_interval);
        ObjectAnimationKeyFrame* end_keyframe = target_object_animation->GetClosestKeyframe(target_interval);

        if (!start_keyframe){
            debug->Err("Failed to get start_keyframe for %s at %.3f\n",name.c_str(),this_interval);
            continue;
        }
        if (!end_keyframe){
            debug->Err("Failed to get end_keyframe for %s\n",target->name.c_str(),target_interval);
            continue;
        }

        //If we are lerping hips towards something more off zero, we do that.

        //Apply the Lerp value.
        if (start_keyframe->f_position && end_keyframe->f_position){
            if (modifies_root_object && target->modifies_root_object){
                //Unhandled.
                debug->Err("No Lerping between two root modifying animations yet.\n");
            }else if ((target->modifies_root_object || modifies_root_object)  && target_object_animation->target_name.compare("mixamorig:Hips") == 0){
                //Root motion for the Hips bone is applied manually by the caller, so we don't set its position here.
            }else{
                vec3 pos = start_keyframe->position.lerp(end_keyframe->position,factor);
                if (this_object_animation->target){
                    this_object_animation->target->SetPosition(pos);
                }
            }
        }
        if (start_keyframe->f_rotation && end_keyframe->f_rotation){
            if (this_object_animation->target){
                quat merged_rot = quat::slerp(start_keyframe->rotation,end_keyframe->rotation,factor);

                //Set rotation forces the bone into a specific rotation, ignoring existing rotation.
                if (this_object_animation->target->animation_mask < 1.0f){
                    quat rot = quat::slerp(this_object_animation->target->GetRotation(),merged_rot,this_object_animation->target->animation_mask);
                    this_object_animation->target->SetRotation(rot);
                }else{
                    this_object_animation->target->SetRotation(merged_rot);
                }
            }
        }
    }
}

//Apply complete animation to all objects in chain at interval
void Animation::ApplyInterval(float interval){
    for (ObjectAnimation* object_animation:object_animations){
        Object* target = object_animation->target;
        if (!object_animation->target){
            continue;
        }
        bool mask_position = false;
        float target_mask = target->position_mask;
        if (modifies_root_object && object_animation->target_name.compare("mixamorig:Hips") == 0){
            mask_position = true;
            target->position_mask = 0.0f;
        }
        ApplyIntervalOnto(object_animation,target,interval);
        //Reset
        if (mask_position){
            target->position_mask = target_mask;
        }
    }
}

//Set flags on all keyframes
void Animation::SetPositionUpdates(ObjectAnimation* object_animation, bool flag){
    if (!object_animation){
        return;
    }
    debug->Info("Animation: Setting f_position on %s to %hhu\n",object_animation->target_name.c_str(),flag);
    for (ObjectAnimationKeyFrame* keyframe : object_animation->keyframes){
        keyframe->f_position = flag;
    }
}

ObjectAnimation* Animation::FindObjectAnimation(const std::string& target_name){
    for (int index=0;index<object_animations.size();index++){
        if (target_name.compare(object_animations.at(index)->target_name) == 0){
            return object_animations.at(index);
        }
    }
    return NULL;
}

ObjectAnimation* Animation::FindObjectAnimation(Object* target_object){
    for (int index=0;index<object_animations.size();index++){
        if (target_object == object_animations.at(index)->target){
            return object_animations.at(index);
        }
    }
    return NULL;
}

//Returns a keyframe at the exact specified time
ObjectAnimationKeyFrame* ObjectAnimation::FindKeyframeAtTime(float time){
    std::list<ObjectAnimationKeyFrame*>::iterator it = keyframes.begin();
    for ( ; it != keyframes.end(); ) {
        ObjectAnimationKeyFrame* keyframe = *it;
        if (keyframe->time == time){
            return keyframe;
        }
        ++it;
    }
    return NULL;
}

//Todo, look back and find closest
ObjectAnimationKeyFrame* ObjectAnimation::GetClosestKeyframe(float time){
    //Keyframes are stored in order.
    for (ObjectAnimationKeyFrame* keyframe : keyframes){
        if (keyframe->time >= time){
            return keyframe;
        }
    }
    //Nothing? Return the last one.
    return keyframes.back();
}

ObjectAnimationKeyFrame* ObjectAnimation::GetFirstKeyframe(){
    if (keyframes.size() > 0)
        return keyframes.front();
    return NULL;
}

ObjectAnimationKeyFrame* ObjectAnimation::GetLastKeyframe(){
    if (keyframes.size() > 0)
        return keyframes.back();
    return NULL;
}

void Animation::AddObjectAnimation(ObjectAnimation* object_animation){
    if (!object_animation){
        return;
    }
    object_animations.push_back(object_animation);
}

//Add's the keyframe in the correct order in the list.
void ObjectAnimation::AddKeyframe(ObjectAnimationKeyFrame* new_keyframe){
    if (!new_keyframe){
        return;
    }

    std::list<ObjectAnimationKeyFrame*>::iterator it = keyframes.begin();
    for ( ; it != keyframes.end(); ) {
        ObjectAnimationKeyFrame* keyframe = *it;
        if (keyframe->time > new_keyframe->time){
            //Insert before this one.
            keyframes.insert(it,new_keyframe);
            return;
        }
        ++it;
    }

    //Nothing, insert this as last.
    keyframes.push_back(new_keyframe);
}

//TODO: Make it majestic
void Animation::Retarget(Object* target){
    //We iterate over the object animations and add mixamo
    for (ObjectAnimation* objectanimation:object_animations){
        objectanimation->target_name = "mixamorig:" + objectanimation->target_name;
    }
    LinkObjects(target);
}

Animation* AnimationGraph::LookupAnimation(const std::string& name){
    if (!animations){
        debug->Warn("AddTransition with no animations to look up\n");
        return NULL;
    }
    for (Animation* animation:*animations){
        if (animation->name.compare(name) == 0){
            return animation;
        }
    }
    return NULL;
}

AnimationTransition* AnimationGraph::AddTransition(const std::string& from, const std::string& to){
    //If to and from are the same, we need to only make sure the animation is looping
    if (!from.empty() && (from.compare(to) == 0)){
        Animation* animation = LookupAnimation(from);
        if (animation){
            animation->looped = true;
        }
        return NULL;
    }

    // Check this transition does not already exist.
    for (AnimationTransition* t : transitions){
        bool from_match = from.empty() ? (t->from == NULL) : (t->from && t->from->name == from);
        bool to_match   = t->to && t->to->name == to;
        if (from_match && to_match){
            debug->Warn("AddTransition: transition %s -> %s already exists\n", from.c_str(), to.c_str());
            return t;
        }
    }

    Animation* anim_from = from.empty() ? NULL : LookupAnimation(from);
    Animation* anim_to   = LookupAnimation(to);

    if (!from.empty() && !anim_from){
        debug->Warn("AddTransition: animation '%s' not found\n", from.c_str());
        return NULL;
    }
    if (!anim_to){
        debug->Warn("AddTransition: animation '%s' not found\n", to.c_str());
        return NULL;
    }

    AnimationTransition* transition = new AnimationTransition();
    transition->from       = anim_from;
    transition->to         = anim_to;
    transition->blend_time = 0.3f;
    transitions.push_back(transition);

    debug->Info("AnimationGraph: Added transition %s -> %s %p %p\n", from.c_str(), to.c_str());
    return transition;
}

//This just looks for the first transition from the specified animation. We might want to have multiple transitions.
AnimationTransition* AnimationGraph::FindTransitionFrom(Animation* from){
    for (AnimationTransition* t : transitions){
        if (t->from == from){
            return t;
        }
    }
    return NULL;
}

AnimationTransition* AnimationGraph::FindTransition(Animation* from, Animation* to){
    for (AnimationTransition* t : transitions){
        if (t->from == from && t->to == to){
            return t;
        }
    }
    return NULL;
}
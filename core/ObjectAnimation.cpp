#include "ObjectAnimation.h"


#include "Debug.h"
static Debugger *debug = new Debugger("ObjectAnimation", DEBUG_INFO);

ObjectAnimationKeyFrame::ObjectAnimationKeyFrame(){

}


ObjectAnimation::ObjectAnimation(){

}

Animation::Animation(){

}

//Animation contains object names. We look them up and store references.
void Animation::LinkObjects(Object* root){
    std::vector<Object*>objects;
    root->GetAllSubObjects(objects);
    int count = 0;
    for (ObjectAnimation* object_animation:object_animations){
        for (Object* object:objects){
            if (object->name.compare(object_animation->target_name) == 0){
                object_animation->target = object;
                count++;
                break;
            }
        }
    }
    debug->Info("Animation: Linked %i objects to animation\n",count);
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

    //We find the firstkeyframe. They are stored in order.
    for (ObjectAnimationKeyFrame* keyframe : object_animation->keyframes){
        if (keyframe->time > interval){
            if (keyframe->f_rotation){
                target->SetRotation(keyframe->rotation);
            }
            if (keyframe->f_position){
                target->SetPosition(keyframe->position);
            }
            break;
        }
    }
}

void Animation::Lerp(Animation* target,float this_interval, float target_interval, float factor){
    if (!target){
        return;
    }
    //What we do is for each object we make a new animation, with start and end keyframe.
    if (target->object_animations.size() != object_animations.size()){
        debug->Err("Lerp on these animations are incompatible (%s - %s)\n",name.c_str(),target->name.c_str());
    }

}

//Apply complete animation to all objects in chain at interval
void Animation::ApplyInterval(float interval){
    for (ObjectAnimation* object_animation:object_animations){
        if (!object_animation->target){
            continue;
        }
        Object* target = object_animation->target;
        ApplyIntervalOnto(object_animation,target,interval);
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
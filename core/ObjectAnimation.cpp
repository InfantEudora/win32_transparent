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

    ObjectAnimationKeyFrame* keyframe = object_animation->GetClosestKeyframe(interval);
    if (!keyframe){
        return;
    }
    if (keyframe->f_rotation){
        target->SetRotation(keyframe->rotation);
    }
    if (keyframe->f_position){
        target->SetPosition(keyframe->position);
    }
}

void Animation::Lerp(Animation* target,float this_interval, float target_interval, float factor){
    if (!target){
        return;
    }

    if (target->object_animations.size() != object_animations.size()){
        debug->Err("Lerp on these animations are incompatible (%s -> %s)\n",name.c_str(),target->name.c_str());
        return;
    }

    std::vector<ObjectAnimation*>list;

    for (int i=0;i<object_animations.size();i++){
        ObjectAnimation* this_object_animation = object_animations.at(i);
        ObjectAnimation* target_object_animation = target->object_animations.at(i);

        ObjectAnimation* new_object_animation = new ObjectAnimation();
        list.push_back(new_object_animation);

        ObjectAnimationKeyFrame* start_keyframe = new ObjectAnimationKeyFrame(this_object_animation->GetClosestKeyframe(this_interval));
        ObjectAnimationKeyFrame* end_keyframe = new ObjectAnimationKeyFrame(target_object_animation->GetClosestKeyframe(target_interval));
        start_keyframe->time = 0;
        end_keyframe->time = 1;
        new_object_animation->AddKeyframe(start_keyframe);
        new_object_animation->AddKeyframe(end_keyframe);
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

//Todo, look back and find closest
ObjectAnimationKeyFrame* ObjectAnimation::GetClosestKeyframe(float time){
    //Keyframes are stored in order.
    for (ObjectAnimationKeyFrame* keyframe : keyframes){
        if (keyframe->time > time){
            return keyframe;
        }
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
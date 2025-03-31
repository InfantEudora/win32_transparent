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

void Animation::ApplyInterval(float interval){
    for (ObjectAnimation* object_animation:object_animations){
        if (!object_animation->target){
            continue;
        }
        Object* target = object_animation->target;
        debug->Info("Animation: Applying target %s\n",target->name.c_str());

        ObjectAnimationKeyFrame* keyframe = object_animation->keyframes.at(0);
        if (keyframe->f_rotation){
            target->SetRotation(keyframe->rotation);
        }
    }
}

ObjectAnimation* Animation::FindObjectAnimation(std::string& target_name){
    for (int index=0;index<object_animations.size();index++){
        if (target_name.compare(object_animations.at(index)->target_name) == 0){
            return object_animations.at(index);
        }
    }
    return NULL;
}

ObjectAnimationKeyFrame* ObjectAnimation::FindKeyframeAtTime(float time){
    for (int index=0;index<keyframes.size();index++){
        if (keyframes.at(index)->time == time){
            return keyframes.at(index);
        }
    }
    return NULL;
}

void Animation::AddObjectAnimation(ObjectAnimation* object_animation){
    if (!object_animation){
        return;
    }
    object_animations.push_back(object_animation);
}

void ObjectAnimation::AddKeyframe(ObjectAnimationKeyFrame* keyframe){
    if (!keyframe){
        return;
    }
    keyframes.push_back(keyframe);
}
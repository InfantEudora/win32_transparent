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
        debug->Info("Animation: Applying target %s at interval %.3f\n",target->name.c_str(),interval);

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
}

ObjectAnimation* Animation::FindObjectAnimation(std::string& target_name){
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
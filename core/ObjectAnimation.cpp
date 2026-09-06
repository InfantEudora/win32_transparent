#include "ObjectAnimation.h"
#include "type_helpers.h"
#include "skeleton/Bone.h"

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

        //The root bone is handled separately, uniformly, by LerpRootMotion - skip it here.
        if (this_object_animation == root_track || target_object_animation == target->root_track){
            continue;
        }

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

        //Apply the Lerp value.
        if (start_keyframe->f_position && end_keyframe->f_position){
            vec3 pos = start_keyframe->position.lerp(end_keyframe->position,factor);
            if (this_object_animation->target){
                this_object_animation->target->SetPosition(pos);
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
        //The root bone is handled separately, uniformly, by SampleRootMotion - skip it here.
        if (object_animation == root_track){
            continue;
        }
        Object* target = object_animation->target;
        if (!object_animation->target){
            continue;
        }
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

void Animation::CopyConfigFrom(Animation* source){
    if (!source){
        return;
    }
    looped = source->looped;
    interruptible = source->interruptible;
    extract_horizontal_root_motion = source->extract_horizontal_root_motion;
    extract_vertical_root_motion = source->extract_vertical_root_motion;
}

void Animation::SetRootBone(const std::string& name){
    root_bone_name = name;
    root_track = FindObjectAnimation(name);
}

//Splits a delta rotation 'relative' (already expressed relative to the bone's reference pose) into
//a pure twist about +Y (the character's facing direction) and everything else (swing - lean/tilt).
//See docs/animation_root_motion.md - this is the same swing-twist decomposition a cone-twist joint
//uses to split its cone limit (swing) from its axial limit (twist).
static void DecomposeSwingTwistY(const quat& relative, quat& out_swing, float& out_twist_angle){
    quat twist(0.0f, relative.y, 0.0f, relative.w);
    float len = sqrtf(twist.y*twist.y + twist.w*twist.w);
    if (len > 1e-6f){
        twist.y /= len;
        twist.w /= len;
    }else{
        twist.identity();
    }
    out_twist_angle = 2.0f * atan2f(twist.y, twist.w);

    quat twist_inverse = twist;
    twist_inverse.inverse();
    out_swing = relative * twist_inverse;
}

//Wraps an angle delta into (-PI, PI] so a twist angle crossing the +-PI seam doesn't register as a
//near-2*PI jump.
static float WrapAngleDelta(float delta){
    return fmodf(delta + 3.0f*TYPE_PI, 2.0f*TYPE_PI) - TYPE_PI;
}

RootPose Animation::ComputeRootPose(float time){
    RootPose out;
    if (!root_track || !root_track->target){
        return out;
    }
    Bone* bone = dynamic_cast<Bone*>(root_track->target);
    if (!bone){
        return out;
    }
    ObjectAnimationKeyFrame* keyframe = root_track->GetClosestKeyframe(time);
    if (!keyframe){
        return out;
    }

    out.authored_position = keyframe->f_position ? keyframe->position : bone->reference_position;

    if (keyframe->f_rotation){
        quat reference_inverse = bone->reference_rotation;
        reference_inverse.inverse();
        quat relative = keyframe->rotation * reference_inverse;
        DecomposeSwingTwistY(relative, out.swing, out.twist_angle);
    }
    return out;
}

//Builds the bone-local position to display for a root pose sampled from a clip with the given
//horizontal/vertical extraction flags: any axis NOT extracted keeps its authored value (so it still
//reads as cosmetic bone motion - sway, bob); any axis extracted is pinned to the reference position
//(so it isn't shown twice, once on the bone and once on the character's world transform).
static vec3 PinnedBonePosition(const RootPose& pose, Bone* bone, bool extract_horizontal, bool extract_vertical){
    vec3 out = bone->reference_position;
    if (!extract_horizontal){
        out.x = pose.authored_position.x;
        out.z = pose.authored_position.z;
    }
    if (!extract_vertical){
        out.y = pose.authored_position.y;
    }
    return out;
}

RootMotionDelta Animation::SampleRootMotion(float prev_time, float new_time){
    RootMotionDelta out;
    if (!root_track || !root_track->target){
        return out;
    }
    Bone* bone = dynamic_cast<Bone*>(root_track->target);
    if (!bone){
        return out;
    }

    RootPose prev = ComputeRootPose(prev_time);
    RootPose cur  = ComputeRootPose(new_time);

    if (extract_horizontal_root_motion){
        out.position.x = cur.authored_position.x - prev.authored_position.x;
        out.position.z = cur.authored_position.z - prev.authored_position.z;
    }
    if (extract_vertical_root_motion){
        out.position.y = cur.authored_position.y - prev.authored_position.y;
    }
    out.yaw = WrapAngleDelta(cur.twist_angle - prev.twist_angle);

    bone->SetPosition(PinnedBonePosition(cur, bone, extract_horizontal_root_motion, extract_vertical_root_motion));
    bone->SetRotation(cur.swing * bone->reference_rotation);

    return out;
}

RootMotionDelta Animation::LerpRootMotion(Animation* to, float from_prev, float from_new, float to_prev, float to_new, float factor){
    RootMotionDelta out;
    if (!to){
        return out;
    }
    ObjectAnimation* track = root_track ? root_track : to->root_track;
    if (!track || !track->target){
        return out;
    }
    Bone* bone = dynamic_cast<Bone*>(track->target);
    if (!bone){
        return out;
    }

    RootMotionDelta from_delta;
    RootPose from_cur;
    if (root_track){
        RootPose from_prev_pose = ComputeRootPose(from_prev);
        from_cur = ComputeRootPose(from_new);
        if (extract_horizontal_root_motion){
            from_delta.position.x = from_cur.authored_position.x - from_prev_pose.authored_position.x;
            from_delta.position.z = from_cur.authored_position.z - from_prev_pose.authored_position.z;
        }
        if (extract_vertical_root_motion){
            from_delta.position.y = from_cur.authored_position.y - from_prev_pose.authored_position.y;
        }
        from_delta.yaw = WrapAngleDelta(from_cur.twist_angle - from_prev_pose.twist_angle);
    }

    RootMotionDelta to_delta;
    RootPose to_cur;
    if (to->root_track){
        RootPose to_prev_pose = to->ComputeRootPose(to_prev);
        to_cur = to->ComputeRootPose(to_new);
        if (to->extract_horizontal_root_motion){
            to_delta.position.x = to_cur.authored_position.x - to_prev_pose.authored_position.x;
            to_delta.position.z = to_cur.authored_position.z - to_prev_pose.authored_position.z;
        }
        if (to->extract_vertical_root_motion){
            to_delta.position.y = to_cur.authored_position.y - to_prev_pose.authored_position.y;
        }
        to_delta.yaw = WrapAngleDelta(to_cur.twist_angle - to_prev_pose.twist_angle);
    }

    out.position = from_delta.position.lerp(to_delta.position, factor);
    out.yaw = from_delta.yaw + (to_delta.yaw - from_delta.yaw) * factor;

    //Blend the two clips' pinned/swing-corrected display pose for the shared bone.
    vec3 from_display = root_track ? PinnedBonePosition(from_cur, bone, extract_horizontal_root_motion, extract_vertical_root_motion) : bone->reference_position;
    vec3 to_display = to->root_track ? PinnedBonePosition(to_cur, bone, to->extract_horizontal_root_motion, to->extract_vertical_root_motion) : bone->reference_position;
    bone->SetPosition(from_display.lerp(to_display, factor));

    quat from_rot = from_cur.swing * bone->reference_rotation;
    quat to_rot = to_cur.swing * bone->reference_rotation;
    bone->SetRotation(quat::slerp(from_rot, to_rot, factor));

    return out;
}

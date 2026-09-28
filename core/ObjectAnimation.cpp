#include "ObjectAnimation.h"
#include "type_helpers.h"
#include "skeleton/Bone.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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

    ObjectAnimationKeyFrame sampled;
    if (!object_animation->Sample(interval,sampled)){
        return;
    }
    ObjectAnimationKeyFrame* keyframe = &sampled;
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

/*
    The pose a clip that does NOT animate `object` leaves it in, for blending against one that does:
    a bone's reference (bind) pose - what LoadDefaultPose puts back. False for anything that is not
    a bone, which has no pose to fall back to.
*/
static bool ReferenceKeyFrame(Object* object, ObjectAnimationKeyFrame& out){
    Bone* bone = dynamic_cast<Bone*>(object);
    if (!bone){
        return false;
    }
    out.rotation = bone->reference_rotation;
    out.position = bone->reference_position;
    out.f_rotation = true;
    out.f_position = true;
    out.f_scale = false;
    out.f_shapekeys = false;
    return true;
}

//One object's blend of two samples, `factor` of the way from `start` to `end`. Only the channels
//both carry are written; the animation mask is honoured as ApplyIntervalOnto honours it.
static void ApplyLerped(Object* object, const ObjectAnimationKeyFrame& start, const ObjectAnimationKeyFrame& end,
                        float factor){
    if (start.f_position && end.f_position){
        object->SetPosition(start.position.lerp(end.position,factor));
    }
    if (start.f_rotation && end.f_rotation){
        quat merged_rot = quat::slerp(start.rotation,end.rotation,factor);
        //Set rotation forces the bone into a specific rotation, ignoring existing rotation.
        if (object->animation_mask < 1.0f){
            object->SetRotation(quat::slerp(object->GetRotation(),merged_rot,object->animation_mask));
        }else{
            object->SetRotation(merged_rot);
        }
    }
}

/*
    TRACKS ARE PAIRED BY THE OBJECT THEY DRIVE, not by their place in the list.

    This used to take track i of one clip with track i of the other, and refused outright when the
    two clips had different numbers of tracks - so a clip exported before a rig grew a bone (the
    archer's hair bones, 2026-09-28) could not be blended with one exported after, or with anything
    the exporter had trimmed a constant channel from. Worse, two clips with the SAME count in a
    different order blended the wrong bones together without a word.

    A bone only one of the two animates is blended against its reference pose on the other side -
    what the clip that leaves it alone holds it at - so a clip without hair bones fades the hair
    back to its modelled shape rather than stopping the whole blend. Something that is not a bone
    has no such pose, and simply takes the one clip that has it, as ApplyInterval would.

    The root bone is LerpRootMotion's, in either clip, and skipped here.
*/
void Animation::Lerp(Animation* target,float this_interval, float target_interval, float factor){
    if (!target){
        return;
    }

    //The target clip's tracks by what they drive. A few dozen entries, rebuilt per call: cheaper
    //than keeping a cache honest across relinks.
    std::unordered_map<Object*,ObjectAnimation*> theirs;
    theirs.reserve(target->object_animations.size());
    for (ObjectAnimation* track : target->object_animations){
        if (track->target && track != target->root_track){
            theirs[track->target] = track;
        }
    }

    std::unordered_set<Object*> done;
    done.reserve(object_animations.size());
    for (ObjectAnimation* this_object_animation : object_animations){
        Object* object = this_object_animation->target;
        //The root bone is handled separately, uniformly, by LerpRootMotion - skip it here.
        if (!object || this_object_animation == root_track ||
            (target->root_track && object == target->root_track->target)){
            continue;
        }
        done.insert(object);

        ObjectAnimationKeyFrame start_sampled;
        if (!this_object_animation->Sample(this_interval,start_sampled)){
            debug->Err("Failed to get start_keyframe for %s at %.3f\n",name.c_str(),this_interval);
            continue;
        }
        ObjectAnimationKeyFrame end_sampled;
        std::unordered_map<Object*,ObjectAnimation*>::iterator match = theirs.find(object);
        if (match != theirs.end()){
            if (!match->second->Sample(target_interval,end_sampled)){
                debug->Err("Failed to get end_keyframe for %s at %.3f\n",target->name.c_str(),target_interval);
                continue;
            }
        }else if (!ReferenceKeyFrame(object,end_sampled)){
            //Not a bone and not in the target clip: this clip's own value, as the target would leave it.
            end_sampled = ObjectAnimationKeyFrame(&start_sampled);
        }
        ApplyLerped(object,start_sampled,end_sampled,factor);
    }

    //And what only the target clip animates, blended in from the reference pose.
    for (std::unordered_map<Object*,ObjectAnimation*>::iterator it = theirs.begin(); it != theirs.end(); ++it){
        Object* object = it->first;
        if (done.count(object) || (root_track && object == root_track->target)){
            continue;
        }
        ObjectAnimationKeyFrame end_sampled;
        if (!it->second->Sample(target_interval,end_sampled)){
            debug->Err("Failed to get end_keyframe for %s at %.3f\n",target->name.c_str(),target_interval);
            continue;
        }
        ObjectAnimationKeyFrame start_sampled;
        if (!ReferenceKeyFrame(object,start_sampled)){
            start_sampled = ObjectAnimationKeyFrame(&end_sampled);
        }
        ApplyLerped(object,start_sampled,end_sampled,factor);
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

bool ObjectAnimation::Sample(float time, ObjectAnimationKeyFrame& out){
    if (keyframes.empty()){
        return false;
    }
    ObjectAnimationKeyFrame* before = NULL;
    ObjectAnimationKeyFrame* after = NULL;
    for (ObjectAnimationKeyFrame* keyframe : keyframes){
        if (keyframe->time >= time){
            after = keyframe;
            break;
        }
        before = keyframe;
    }
    //Past either end: that end's key, as it is.
    if (!after || !before || after->time <= before->time){
        out = after ? *after : *before;
        return true;
    }
    float f = (time - before->time) / (after->time - before->time);
    //The flags and anything not blended are the later key's, which is what the ceiling returned.
    out = *after;
    out.time = time;
    if (before->f_rotation && after->f_rotation){
        out.rotation = quat::slerp(before->rotation,after->rotation,f);
        out.rotation.normalize();
    }
    if (before->f_position && after->f_position){
        out.position = before->position.lerp(after->position,f);
    }
    if (before->f_shapekeys && after->f_shapekeys &&
        before->shapekey_weights.size() == after->shapekey_weights.size()){
        for (size_t i = 0; i < out.shapekey_weights.size(); i++){
            out.shapekey_weights[i] = before->shapekey_weights[i] +
                                      (after->shapekey_weights[i] - before->shapekey_weights[i]) * f;
        }
    }
    return true;
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
    extract_yaw_root_motion = source->extract_yaw_root_motion;
    if (source->f_trimmed && !f_trimmed){
        Trim(source->trim_offset,source->trim_offset + source->duration);
    }
}

void Animation::Trim(float start, float end){
    if (start < 0.0f){
        start = 0.0f;
    }
    if (end <= 0.0f || end > duration){
        end = duration;
    }
    if (end - start <= 0.0f){
        debug->Err("Trim: %s has nothing between %.3f and %.3f (it is %.3fs long); left as it was\n",
                   name.c_str(),start,end,duration);
        return;
    }
    /*
        A little slack on both cuts, because the times come from the glTF as floats: a cut at
        10/30 of a second has to land ON the key at 0.3333 whichever side of it the two roundings
        fell, not skip to the next frame for being a ten-millionth late.
    */
    const float SLACK = 1e-4f;
    for (ObjectAnimation* track : object_animations){
        std::list<ObjectAnimationKeyFrame*>& keys = track->keyframes;
        if (keys.empty()){
            continue;
        }
        //From the key sampled at `start` through the key sampled at `end`, and nothing either side.
        std::list<ObjectAnimationKeyFrame*> kept;
        for (ObjectAnimationKeyFrame* key : keys){
            if (key->time < start - SLACK){
                continue;
            }
            kept.push_back(key);
            if (key->time >= end - SLACK){
                break;
            }
        }
        //A track that stopped before `start` - a bone keyed once and then left alone. Sampling
        //past its last key returns that key, so that is the pose the trimmed clip holds.
        if (kept.empty()){
            kept.push_back(keys.back());
        }
        for (ObjectAnimationKeyFrame* key : keys){
            if (std::find(kept.begin(),kept.end(),key) == kept.end()){
                delete key;
            }
        }
        for (ObjectAnimationKeyFrame* key : kept){
            key->time = (key->time > start) ? key->time - start : 0.0f;
        }
        keys.swap(kept);
    }
    duration = end - start;
    trim_offset += start;
    f_trimmed = true;
    if (time_index > duration){
        time_index = duration;
    }
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
    ObjectAnimationKeyFrame sampled;
    if (!root_track->Sample(time,sampled)){
        return out;
    }
    ObjectAnimationKeyFrame* keyframe = &sampled;

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

/*
    The same thing for the root bone's ROTATION, and it exists because for a long time it did not.

    Extracting: the twist is removed and the bone is posed swing-only, because the twist is about to
    be applied to the character's transform instead and showing it twice would double every turn.
    NOT extracting: the bone keeps its authored rotation, exactly as PinnedBonePosition keeps an
    un-extracted axis's authored position - the clip is simply played as animated.

    What used to happen was neither: swing-only unconditionally, so a clip's hip rotation was taken
    out of the pose whether or not anything downstream would put it back, and on every object that
    is not a PlayerCharacter nothing did. See the block on extract_yaw_root_motion.

    Recomposition relies on DecomposeSwingTwistY's ordering - it splits `relative` into
    swing * twist - so swing * twist * reference is the authored rotation back again.
*/
static quat PinnedBoneRotation(const RootPose& pose, Bone* bone, bool extract_yaw){
    if (extract_yaw){
        return pose.swing * bone->reference_rotation;
    }
    quat twist(0.0f,sinf(pose.twist_angle * 0.5f),0.0f,cosf(pose.twist_angle * 0.5f));
    return pose.swing * twist * bone->reference_rotation;
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
    if (extract_yaw_root_motion){
        out.yaw = WrapAngleDelta(cur.twist_angle - prev.twist_angle);
    }

    bone->SetPosition(PinnedBonePosition(cur, bone, extract_horizontal_root_motion, extract_vertical_root_motion));
    bone->SetRotation(PinnedBoneRotation(cur, bone, extract_yaw_root_motion));

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
        if (extract_yaw_root_motion){
            from_delta.yaw = WrapAngleDelta(from_cur.twist_angle - from_prev_pose.twist_angle);
        }
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
        if (to->extract_yaw_root_motion){
            to_delta.yaw = WrapAngleDelta(to_cur.twist_angle - to_prev_pose.twist_angle);
        }
    }

    out.position = from_delta.position.lerp(to_delta.position, factor);
    out.yaw = from_delta.yaw + (to_delta.yaw - from_delta.yaw) * factor;

    //Blend the two clips' pinned/swing-corrected display pose for the shared bone.
    vec3 from_display = root_track ? PinnedBonePosition(from_cur, bone, extract_horizontal_root_motion, extract_vertical_root_motion) : bone->reference_position;
    vec3 to_display = to->root_track ? PinnedBonePosition(to_cur, bone, to->extract_horizontal_root_motion, to->extract_vertical_root_motion) : bone->reference_position;
    bone->SetPosition(from_display.lerp(to_display, factor));

    //Each side keeps or gives up its own twist according to ITS own flag, exactly as each side's
    //position is pinned by its own pair - so blending a pivot into a run cycle does the right
    //thing at both ends rather than at whichever one happens to be leading.
    quat from_rot = PinnedBoneRotation(from_cur, bone, extract_yaw_root_motion);
    quat to_rot = PinnedBoneRotation(to_cur, bone, to->extract_yaw_root_motion);
    bone->SetRotation(quat::slerp(from_rot, to_rot, factor));

    return out;
}

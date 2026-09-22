#ifndef _OBJECT_ANIMATION_H_
#define _OBJECT_ANIMATION_H_


class Animation;
class ObjectAnimation;
class ObjectAnimationKeyFrame;

#include "Object.h"

//A single, absolute sample of the root/hip bone: its authored (bone-local) position, plus its
//rotation relative to the bone's reference pose split into swing (lean/tilt - stays on the bone)
//and twist (facing/yaw about +Y - gets extracted to the character's world transform).
struct RootPose{
    vec3 authored_position = vec3();
    quat swing = quat().identity();
    float twist_angle = 0.0f; // radians, relative to the bone's reference rotation
};

//The world-space motion a single animation tick contributes: how far the character should move
//and turn this frame, already filtered by which axes/components the clip opted to extract.
struct RootMotionDelta{
    vec3 position = vec3();
    float yaw = 0.0f;
};

class Animation{
    public:
    Animation();
    std::string name;

    //A list of ObjectAnimations that this animation will animate.
    std::vector<ObjectAnimation*>object_animations;

    float duration = 0.0f;      // Value of last keyframe.
    float time_index = 0.0f;    // When playing
    bool looped = false;

    //If false, a request to transition away from this animation is refused until it finishes playing
    //(or, if looped, is simply always interruptible). Used for connector clips like "StandToFreeHang"
    //that must be allowed to complete.
    bool interruptible = true;

    //Per-axis opt-in: does this clip's hip translation represent real character movement (extracted
    //to the world transform), or is it cosmetic motion that should stay local to the bone (idle sway,
    //footstep bob)? Both default to "stays on the bone" so a clip has to opt into moving the character.
    bool extract_horizontal_root_motion = false;   //X/Z - locomotion (walking, running, turning while moving)
    bool extract_vertical_root_motion = false;     //Y   - genuine height change (climbing, jumping)
    /*
        And the same question for the hip's YAW: is this clip TURNING the character, or is its hip
        rotation part of the performance?

        A pivot clip means the turn - drop it and the character slides round facing the wrong way.
        A run cycle's hips counter-rotate with the stride as a matter of gait - hand THAT to the
        world transform and the whole body wags from side to side (23 degrees of it, measured on
        apps/archer's Running_Fast).

        UNTIL 2026-09-22 THIS WAS NOT A CHOICE: the twist was taken off the bone unconditionally
        and handed to Object::ApplyRootMotion, whose base does nothing - so on anything that was
        not a PlayerCharacter the root bone's yaw was deleted from the POSE and then discarded.
        Not "the character does not turn": the hips did not rotate at all, measured at exactly
        0.00 degrees of twist across every clip in apps/archer. That is a third state nobody ever
        wants, and this flag is what removes it. Off, the authored rotation stays on the bone and
        the pose is exactly as animated; on, it moves to the character. Never neither.

        Defaults false with the other two, and for the same reason: a clip opts into moving the
        character rather than doing it by surprise.

        IT DOES NOT COMPOSE WITH A TRANSLATION LEFT ON THE BONE, and that is the one thing to know
        before setting it. The character's rotation is applied ABOVE the root bone, so
        R(yaw) * T(p) = T(R(yaw)*p) * R(yaw): any authored offset still sitting on the bone is
        ROTATED by the extracted yaw instead of translated, and a clip that walks while it turns
        orbits a point rather than walking. Measured on apps/archer's Twirl - a 0.879-unit step
        back while spinning - the hips traced a circle of that radius instead of a line.

        So a clip whose yaw is extracted should either turn ON THE SPOT, or extract its translation
        too. Counter-rotating the leftover offset here would need the twist at the clip's START,
        which this function does not have and cannot sensibly guess at, so the combination is left
        as something for the caller to avoid rather than something silently half-corrected.
    */
    bool extract_yaw_root_motion = false;          //Y rotation - pivots, turn-in-place, spins

    //If this (non-looping) animation finishes and nothing else was requested, automatically
    //transition to this animation instead of pausing on the last frame.
    Animation* auto_continue_to = NULL;

    //Copies the gameplay configuration (looped/interruptible/extract flags) from another Animation
    //of the same clip loaded onto a different skeleton - e.g. a hands/feet preview copy that was
    //loaded fresh from the glTF and needs the same settings as the main character's copy, without
    //redoing that setup by hand per skeleton. Deliberately does NOT touch object_animations/keyframes
    //(each skeleton's own linked bone data) or auto_continue_to (points at the SOURCE's own Animation*
    //instances, which wouldn't resolve correctly here, and preview-only skeletons don't chain anyway).
    void CopyConfigFrom(Animation* source);

    //The root/hip bone's animation track, resolved once via SetRootBone(). NULL if this clip has no
    //track for the character's root bone name.
    std::string root_bone_name;
    ObjectAnimation* root_track = NULL;

    void Play(float time_delta); //Plays animmtion forward (or backward if time_delta is negative) and loops if necessary.
    bool HasFinished();

    //Apply the animation at supplied interval
    void LinkObjects(Object* root);
    void ApplyInterval(float interval);
    void ApplyIntervalOnto(ObjectAnimation* object_animation, Object* object, float interval);
    void SetPositionUpdates(ObjectAnimation* object_animation, bool flag);
    void AddObjectAnimation(ObjectAnimation* object_animation);
    ObjectAnimation* FindObjectAnimation(const std::string& target_name);
    ObjectAnimation* FindObjectAnimation(Object* target_object);

    //Lerp this animation at specified interval towards target animation at target interval.
    //The intermediate state is applied as if called with ApplyInterval
    void Lerp(Animation* target,float this_interval, float target_interval, float factor);

    //Resolves root_track from object_animations by bone name. Call once, after LinkObjects.
    void SetRootBone(const std::string& name);

    //Samples the root bone at 'time'. Pure - no side effects.
    RootPose ComputeRootPose(float time);

    //Single-clip (looping) case: returns this tick's world-motion delta between prev_time and
    //new_time, and writes the corrected (pinned position / swing-only rotation) pose onto the root
    //bone for new_time. Returns a zero delta and does nothing if this clip has no root bone track.
    RootMotionDelta SampleRootMotion(float prev_time, float new_time);

    //Crossfade case: blends this clip ("from", sampled between from_prev/from_new) with 'to' ("to",
    //sampled between to_prev/to_new) by 'factor', writes the single blended pose onto the (shared)
    //root bone, and returns the blended world-motion delta for this tick.
    RootMotionDelta LerpRootMotion(Animation* to, float from_prev, float from_new, float to_prev, float to_new, float factor);

    //This will be majestic obviously. But currently only adds mixamo to the target... :)
    void Retarget(Object* target);
};

class ObjectAnimation{
    public:
    ObjectAnimation();
    Object* target = NULL;      // The object this is animating
    std::string target_name;    // The name of the target this was intended for.


    //The will be a list of keyframes, sorted by time
    //Each keyframe modifies an object (bone) at a certain time.

    std::list<ObjectAnimationKeyFrame*>keyframes;

    bool f_enabled = true;
    bool f_looping = false;

    ObjectAnimationKeyFrame* FindKeyframeAtTime(float time);
    ObjectAnimationKeyFrame* GetFirstKeyframe();
    ObjectAnimationKeyFrame* GetLastKeyframe();
    ObjectAnimationKeyFrame* GetClosestKeyframe(float time);
    void AddKeyframe(ObjectAnimationKeyFrame* keyframe);
};

/*
    Animation data from a GLTF file contains time delta's at which certain Nodes(Objects) should be in what orientation, size, position.
*/
class ObjectAnimationKeyFrame{
public:
    ObjectAnimationKeyFrame();
    ObjectAnimationKeyFrame(ObjectAnimationKeyFrame* target); //Copy constructor
    //A frame is stored within a parent animation, typically a skeleton.
    //A frame references a property that it can animate.
    float time = 0.0f;      //At what time within the animation
    vec3 position;
    quat rotation;
    vec3 scale;
    std::vector<float>shapekey_weights;
    int num_shapekeys = 0;                  //The amount of shapekeys referenced, and the stride of the weights vector

    //If these are enabled
    bool f_position = false;
    bool f_rotation = false;
    bool f_scale = false;
    bool f_shapekeys = false;
};

#endif

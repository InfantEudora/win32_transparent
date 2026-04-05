#ifndef _OBJECT_ANIMATION_H_
#define _OBJECT_ANIMATION_H_


class Animation;
class AnimationTransition;
class AnimationGraph;
class ObjectAnimation;
class ObjectAnimationKeyFrame;

#include "Object.h"

class Animation{
    public:
    Animation();
    std::string name;

    //A list of ObjectAnimations that this animation will animate.
    std::vector<ObjectAnimation*>object_animations;

    float duration = 0.0f;      // Value of last keyframe.
    float time_index = 0.0f;    // When playing
    bool looped = false;
    bool modifies_root_object = false; // If this animation modifies the root object, we need to be careful when transitioning to it, and maybe move the root position to parent.
    bool f_end_orientation_different = false; // If at the end of the animation, the orientation is different. This is used for blending to a new animation that starts with a different orientation.

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
    void Lerp(Animation* target,float this_interval, float target_interval, float factor, vec3 inital_hip_pos);

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


// A transition between two animations,
class AnimationTransition {
    public:
    Animation*  from;
    Animation*  to;
    float       blend_time = -1;     // blend time in seconds

    bool        f_hips_rotated = false;             // If at the end of the transition, the hips are rotated.
    bool        f_only_last_frame = false;          // Used for non looping animations, where we can only transition at the end of the animation.
    quat        hip_rotation = quat().identity();    // Rotation we apply at the end of the transition

    void Trigger();
};

// Defines a graph of animations, and rules to transition between them. This is used for characters, but could also be used for other things.
class AnimationGraph{
    //We should be able to see the current animation, and the allowed transitions.
    /*
    For instance:
    Idle -> StandingToSitting -> Sitting - >SittingToStanding -> Walking
                                                              -> Idle
    */
    public:
    std::vector<Animation*> *animations = NULL;     //A reference to where the animations are stored.
    std::vector<AnimationTransition*>transitions;
    Animation* LookupAnimation(const std::string& name);
    AnimationTransition* AddTransition(const std::string& from, const std::string& to);
    AnimationTransition* FindTransitionFrom(Animation* from);
    AnimationTransition* FindTransition(Animation* from, Animation* to);
};

#endif
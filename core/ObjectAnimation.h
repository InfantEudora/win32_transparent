#ifndef _OBJECT_ANIMATION_H_
#define _OBJECT_ANIMATION_H_
#include "Object.h"

class Animation;
class ObjectAnimation;
class ObjectAnimationKeyFrame;

class Animation{
    public:
    Animation();
    std::string name;

    //A list of ObjectAnimations that this animation will animate.
    std::vector<ObjectAnimation*>object_animations;

    float duration = 0.0f;      // Value of last keyframe.
    float time_index = 0.0f;    // When playing
    bool looped = true;

    //Apply the animation at supplied interval
    void LinkObjects(Object* root);
    void ApplyInterval(float interval);
    void ApplyIntervalOnto(ObjectAnimation* object_animation, Object* object, float interval);
    void SetPositionUpdates(ObjectAnimation* object_animation, bool flag);
    void AddObjectAnimation(ObjectAnimation* object_animation);
    ObjectAnimation* FindObjectAnimation(const std::string& target_name);

    //Lerp this animation at specified interval towards target animation at target interval.
    //The intermediate state is applied as if called with ApplyInterval
    void Lerp(Animation* target,float this_interval, float target_interval, float factor, vec3 inital_hip_pos);
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

    //If these are enabled
    bool f_position = false;
    bool f_rotation = false;
    bool f_scale = false;
};

#endif
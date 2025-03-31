#ifndef _OBJECT_ANIMATION_H_
#define _OBJECT_ANIMATION_H_

#include "Object.h"


/*

How to structure?
1: skeleton->animation

animation->skeleton


skeleton->aplyanimation(animation,start_frame);

*/

class Animation;
class ObjectAnimation;
class ObjectAnimationKeyFrame;

class Animation{
    public:
    Animation();
    std::string name;

    //A list of ObjectAnimations that this animation will animate.
    std::vector<ObjectAnimation*>object_animations;

    //Apply the animation at supplied interval
    void LinkObjects(Object* root);
    void ApplyInterval(float interval);
    void AddObjectAnimation(ObjectAnimation* object_animation);
    ObjectAnimation* FindObjectAnimation(std::string& target_name);
};


class ObjectAnimation{
    public:
    ObjectAnimation();
    Object* target = NULL;      // The object this is animating
    std::string target_name;    // The name of the target this was intended for.


    //The will be a list of keyframes.
    //Each keyframe modifies an object (bone) at a certain time.

    std::vector<ObjectAnimationKeyFrame*>keyframes;

    bool f_enabled = true;
    bool f_looping = false;

    ObjectAnimationKeyFrame* FindKeyframeAtTime(float time);
    void AddKeyframe(ObjectAnimationKeyFrame* keyframe);
};


/*
    Animation data from a GLTF file contains time delta's at which certain Nodes(Objects) should be in what orientation, size, position.
*/
class ObjectAnimationKeyFrame{
public:
    ObjectAnimationKeyFrame();
    //A frame is stored within a parent animation, typically a skeleton.
    //A frame references a property that it can animate.
    float time = 0.0f;      //At what time interface within the animation
    vec3 position;
    quat rotation;
    vec3 scale;

    //If these are enabled
    bool f_position = false;
    bool f_rotation = false;
    bool f_scale = false;


};

#endif
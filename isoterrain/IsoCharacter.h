#ifndef _ISO_CHARACTER_H_
#define _ISO_CHARACTER_H_
#include "ObjectAnimation.h"
#include "skeleton/Skeleton.h"
/*
    A character that you control.

    TODO:

    - Play an idle animation.
    - Goto a walking animation when pressing forward.
     The animation can be done in place, but it makes more sence to get the animation with position changes.

     When lerping to a different animation, that may be in place we'd need to move the root, or hip from local transform to parent.
*/

#define ANIMATION_STATE_PAUSED      0
#define ANIMATION_STATE_LOOPING     1
#define ANIMATION_STATE_TRANSITION  2


class IsoCharacter;

class IsoCharacter : public virtual Skeleton{
    public:
    IsoCharacter();
    ~IsoCharacter();

    void UpdatePhysicsState() override;

    //Does each character need a copy of all animations?
    //It does when you don't want to lookup the objects every single time.
    std::vector<Animation*>animations;
    std::string root_bone_name;

    Animation* previous_animation = NULL;
    Animation* current_animation = NULL;
    Animation* next_animation = NULL;

    float transition_time = 0.0f;
    float transition_time_max = 0.25f;

    int animation_state = ANIMATION_STATE_LOOPING;
    bool update_hippos = false;
    vec3 hippos_start = {};
    vec3 hipfwd_start = {};


    void ApplyAnimation(float delta);


    float idle_time = 0.0f;
    float idle_time_max = 3.0f;
    bool f_animation_override = false;
    float animation_time_delta = 0.02f;
    bool f_switch_now = false;
    bool f_rotation_animation = false;


    void AddAnimation(Animation* animation);
    Animation* FindAnimation(const std::string& name); //Finds it by name
    void ProceedToNextAnimation();
    void SetNextAnimation(Animation* animation);
    void SetNextAnimation(const std::string& name); //Set next one to wait until this one is completed.

    void SetAnimation(Animation* animation);
    void CheckSwitchAnimation(); //Whenever the current one is finished



    //There also needs to be a list, or a tree linked list thing with all allowed actions from a current one.
    //eg. When haning of a ledge, you can't transistion to a walking animation. You first need climb up

    //And maybe a list of animation names that can be picked for certain actions, like idle and moving.

    void MoveForward();
    void MoveBackward();
    void TurnLeft();
    void TurnRight();
};

#endif
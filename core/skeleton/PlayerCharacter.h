#ifndef _PLAYER_CHARACTER_H_
#define _PLAYER_CHARACTER_H_
#include "skeleton/Skeleton.h"
/*
    A character that you control.

    TODO:

    - Play an idle animation.
    - Goto a walking animation when pressing forward.
     The animation can be done in place, but it makes more sense to get the animation with position changes.

     When lerping to a different animation, that may be in place we'd need to move the root, or hip from local transform to parent.
*/

class PlayerCharacter;

class PlayerCharacter : public virtual Skeleton{
    public:
    PlayerCharacter();
    ~PlayerCharacter();

    //Does each character need a copy of all animations?
    //It does when you don't want to lookup the objects every single time.
    std::string root_bone_name;

    bool update_hip_position = false;
    vec3 hip_posistion_start = {};
    vec3 hip_fwd_start = {};

    bool update_hip_rotation = false;
    quat hip_rotation_start;
    quat hip_rotation_cummulative;

    void ApplyAnimation(float time_delta) override;

    float idle_time = 0.0f;
    float idle_time_max = 3.0f;

    bool f_switch_now = false;
    bool f_rotation_animation = false;
    bool f_movement_animation_inplace = false;

    float head_turn_direction_lr = 0.0f;   //Direction the head should be facing on top of the animation from -1 to 1
    float head_turn_direction_ud = 0.0f;


    //There also needs to be a list, or a tree linked list thing with all allowed actions from a current one.
    //eg. When haning of a ledge, you can't transistion to a walking animation. You first need climb up

    //And maybe a list of animation names that can be picked for certain actions, like idle and moving.

    void MoveForward();
    void MoveBackward();
    void TurnLeft();
    void TurnRight();
    void Jump();
    void ToIdle();

    void TurnLookLeft();
    void TurnLookRight();
    void TurnLookUp();
    void TurnLookDown();

    Object* head_tracker = NULL;    //The object the character is looking at.

    //For debugging visualisation:
    Object* foot_tracker_l = NULL; //The object to show as tracker
    Object* foot_tracker_r = NULL;
    Object* tracked_foot_l = NULL; //Reference to the foot.
    Object* tracked_foot_r = NULL;
    vec3 left_foot_prev_pos = vec3();
    vec3 right_foot_prev_pos = vec3();

};

#endif
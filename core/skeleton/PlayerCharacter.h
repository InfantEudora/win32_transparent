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

    The character is in a state, character state. These can be many things at once.
    Moving forward and turning at the same time.
    Or jumping moving and turning.
    Some combinations might have unique animations.
    Some animations require the animation to finish playing of be beyond some point.
*/

struct CharacterState{
    bool input_forward_down = false;
    bool input_backward_down = false;
    bool input_left_down = false;
    bool input_right_down = false;
    bool input_jump = false;
    bool any_input_was_active = false;
    bool moving_forward = false;
    bool moving_backward = false;
    bool moving_left = false;
    bool moving_right = false;
    bool input_action_active = false;
    bool input_action = false;
    bool input_interact = false;
};

class PlayerCharacter;

class PlayerCharacter : public virtual Skeleton{
    public:
    PlayerCharacter();
    ~PlayerCharacter();

    CharacterState character_state;


    void ApplyAnimation(float time_delta) override;

    float idle_time = 0.0f;
    float idle_time_max = 3.0f;

    bool f_switch_now = false;
    bool f_rotation_animation = false;
    bool f_move_by_feet_placement = false;      //For inplace animations.
    bool f_update_hip_position = false;         //Used for blending animations that move hip to a new orientation / position.

    float head_turn_direction_lr = 0.0f;        //Direction the head should be facing on top of the animation from -1 to 1
    float head_turn_direction_ud = 0.0f;
    float hips_turn_direction = 0.0f;            //Direction the hips should be facing on top of the animation from -1 to 1


    //There also needs to be a list, or a tree linked list thing with all allowed actions from a current one.
    //eg. When haning of a ledge, you can't transistion to a walking animation. You first need climb up

    //And maybe a list of animation names that can be picked for certain actions, like idle and moving.

    void MoveForward();
    void MoveBackward();
    void TurnLeft();
    void TurnRight();
    void Jump();
    void ToIdle();
    void ActionActive();
    void Action();
    void Interact();

    void TurnLookLeft();
    void TurnLookRight();
    void TurnLookUp();
    void TurnLookDown();

    //Computes the facing and target angles for the character to look at a target. This is used for head and hip turning.
    //Sets the angle the character is facing, and the diff angle the target is in -Pi - Pi
    void ComputeFacingAngles(ObjectStateAccessType state_access, const vec3& target, float& out_facing, float& out_diff);

    Object* head_tracker = NULL;    //The object the character is looking at.

    //For debugging visualisation:
    Object* foot_tracker_l = NULL; //The object to show as tracker
    Object* foot_tracker_r = NULL;
    Object* tracked_foot_l = NULL; //Reference to the foot.
    Object* tracked_foot_r = NULL;
    vec3 left_foot_prev_wpos = vec3();
    vec3 right_foot_prev_wpos = vec3();
private:
    void ProcessInputState();
};

#endif
#include "PlayerCharacter.h"
#include "type_helpers.h"
#include "Debug.h"
static Debugger* debug = new Debugger("PlayerCharacter",DEBUG_INFO);

PlayerCharacter::PlayerCharacter():Object(){
    animation_transition_time_max = 0.2;

}

PlayerCharacter::~PlayerCharacter(){

}

//Function checks input and applies correct animation and state.
void PlayerCharacter::ProcessInputState(){
    if (!(character_input_state.input_forward_down || character_input_state.input_backward_down ||
         /* character_input_state.input_left_down || character_input_state.input_right_down ||*/
        character_input_state.input_jump || character_input_state.input_action ||
        character_input_state.input_action_active)){
        //No input was down. We proceed to idle animation once.

        if (character_input_state.any_input_was_active){
            TransitionToAnimation("Idle");
            character_input_state.any_input_was_active = false;
            character_animation_state.Clear();
            character_animation_state.idle = true;
        }
    }else{
        character_input_state.any_input_was_active = true;
    }
    if (character_input_state.input_action){
        TransitionToAnimation("Boxing");
        character_input_state.input_action = false;
    }else if (character_input_state.input_action_active){
        if (f_handgun_drawn){
            TransitionToAnimation("PistolIdle");
        }else{
            if (FindAnimation("ActionIdle")){
                TransitionToAnimation("ActionIdle");
            }else{
                TransitionToAnimation("CrossJumps");
            }
        }
        character_input_state.input_action_active = false;
        //On top of this animation, we want to rotate the hips to face the target
    }
    if (character_input_state.input_backward_down){
        TransitionToAnimation("WalkingBackward");

        //MoveForwardBy(0.025f * animation_transition_factor);
        character_input_state.input_backward_down = false;
    }
    if (character_input_state.input_left_down){
        float delta = 0.025f;
        if (f_rotation_animation && !character_input_state.input_forward_down){
            TransitionToAnimation("TurnLeftInPlace");
            delta = 0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_input_state.input_left_down = false;
    }
    if (character_input_state.input_right_down){
        float delta = -0.025f;
        if (f_rotation_animation && !character_input_state.input_forward_down){
            TransitionToAnimation("TurnRightInPlace");
            delta = -0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_input_state.input_right_down = false;
    }
    if (character_input_state.input_forward_down){
        TransitionToAnimation("WalkingForward");
        //If the animation is transitioning to walking, we move by a factor.
        //If the animation is looping, that is the full speed.
        float factor = animation_transition_factor;
        if (animation_state == ANIMATION_STATE_LOOPING){
            factor = 1;
        }
        //MoveForwardBy(-0.025f * factor);
        character_input_state.input_forward_down = false;
        character_animation_state.Clear();
        character_animation_state.moving_forward = true;
    }
    if (character_input_state.input_jump){
        if (character_animation_state.moving_forward){
            TransitionToAnimation("JumpForward");
        }else{
            TransitionToAnimation("Jump");
        }
        character_input_state.input_jump = false;
    }
    if (character_input_state.input_interact){
        TransitionToAnimation("Pushing");
        character_input_state.input_interact = false;
    }
    if (character_input_state.input_toggle_handgun){
        f_handgun_drawn = !f_handgun_drawn;
        character_input_state.input_toggle_handgun = false;
    }
}

/*
    The character half of root motion.

    Object computes the delta and poses the root bone with it either way; this is the part that says
    a CHARACTER is the thing that actually goes where its feet went. Anything whose position is owned
    by something else - bomber's enemies, which a grid steps - inherits Object's version and stays
    put.
*/
void PlayerCharacter::ApplyRootMotion(const RootMotionDelta& delta){
    if (delta.yaw != 0.0f){
        RotateBy(quat(vec3(0,1,0),delta.yaw));
    }
    if (delta.position.length() > 0){
        MoveBy(GetRotation()*delta.position);
    }
}

void PlayerCharacter::LoadDefaultPose(){
    //The bones are Object's job. The character state that described the pose they were in is ours,
    //and it is no longer true once they have been put back.
    Object::LoadDefaultPose();
    character_animation_state.Clear();
    character_animation_state.t_pose = true;
}

void PlayerCharacter::ApplyAnimation(float time_delta){
    ProcessInputState();

    /*
        THE CLIP MACHINERY IS Object's NOW.

        Playing, looping, ending, crossfading and rewinding a blend are not character behaviour, and
        keeping a second copy of them here is exactly what let the two drift apart: Object could only
        loop, so any non-PlayerCharacter asking for a blend froze in a state nothing advanced.

        What remains below IS character behaviour - an idle blink layered over whatever is playing, a
        head and hips that turn to look where the character is looking, and the foot trackers. The
        two things Object hands back are ApplyRootMotion and LoadDefaultPose, both above.
    */
    Object::ApplyAnimation(time_delta);

    //Addition animation layering on top of the current animation.
    if (blink_animation){
        blink_interval = clamp(blink_interval - time_delta, 0.0f, 100.0f);
        if (blink_interval == 0.0f){
            blink_animation->Play(time_delta*2);
        }
        if (blink_animation->HasFinished()){
            blink_animation->time_index = 0;
            //Use RRand if you want a random interval.
            blink_interval = 4.0f;//RandFloat(2.0f,5.0f);
        }
    }

    //Apply additional rotation
    {
        Bone* neck = FindBone("mixamorig:Neck");
        if (neck){
            quat q = neck->GetRotation();
            quat r,r1,r2;
            vec3 left = neck->GetLeft();
            r1.set_rotation(left,head_turn_direction_ud*0.2f);
            r2.set_rotation(vec3(0,1,0),head_turn_direction_lr*0.2f);
            r = r2 * r1;
            if (animation_state == ANIMATION_STATE_INVALID || animation_state == ANIMATION_STATE_PAUSED){
                neck->SetRotation(r);
            }else{
                neck->RotateBy(r);
            }
        }
        Bone* head = FindBone("mixamorig:Head");
        if (head){
            quat q = head->GetRotation();
            quat r,r1,r2;
            vec3 left = neck->GetLeft();
            r1.set_rotation(left,head_turn_direction_ud*0.8f);
            r2.set_rotation(vec3(0,1,0),head_turn_direction_lr*0.8f);
            r = r2 * r1;
            if (animation_state == ANIMATION_STATE_INVALID || animation_state == ANIMATION_STATE_PAUSED){
                head->SetRotation(r);
            }else{
                head->RotateBy(r);
            }
        }

        Bone* hips = FindBone("mixamorig:Spine");
        if (hips){
            quat q = hips->GetRotation();
            quat r;
            r.set_rotation(vec3(0,1,0),hips_turn_direction);

            if (animation_state == ANIMATION_STATE_INVALID || animation_state == ANIMATION_STATE_PAUSED){
                hips->SetRotation(r);
            }else{
                hips->RotateBy(r);
            }
        }
    }

    //Update the foot trackers
    if (foot_tracker_l && tracked_foot_l){
        foot_tracker_l->SetPosition(tracked_foot_l->GetWorldPosition());
    }
    if (foot_tracker_r && tracked_foot_r){
        foot_tracker_r->SetPosition(tracked_foot_r->GetWorldPosition());
    }
}

void PlayerCharacter::MoveForward(){
    character_input_state.input_forward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::MoveBackward(){
    character_input_state.input_backward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::TurnRight(){
    character_input_state.input_right_down = true;
}

void PlayerCharacter::TurnLeft(){
    character_input_state.input_left_down = true;
}

void PlayerCharacter::ToIdle(){

}

void PlayerCharacter::ActionActive(){
    character_input_state.input_action_active = true;
}

void PlayerCharacter::Action(){
    character_input_state.input_action = true;
}

void PlayerCharacter::Jump(){
    character_input_state.input_jump = true;
}

void PlayerCharacter::Interact(){
    character_input_state.input_interact = true;
}

void PlayerCharacter::ToggleHandgun(){
    character_input_state.input_toggle_handgun = true;
}

void PlayerCharacter::TurnLookLeft(){
    head_turn_direction_lr = clamp(head_turn_direction_lr+0.05f,-1.0,1.0);
}

void PlayerCharacter::TurnLookRight(){
    head_turn_direction_lr = clamp(head_turn_direction_lr-0.05f,-1.0,1.0);
}

void PlayerCharacter::TurnLookUp(){
    head_turn_direction_ud = clamp(head_turn_direction_ud+0.05f,-1.0,1.0);
}

void PlayerCharacter::TurnLookDown(){
    head_turn_direction_ud = clamp(head_turn_direction_ud-0.05f,-1.0,1.0);
}

void PlayerCharacter::ComputeFacingAngles(const vec3& target, float& out_facing, float& out_diff){
    //We get the direction forward in the zx plane.
    vec3 forward = GetForward();
    forward.y = 0;
    forward.normalize();

    // Convert facing direction to the shader's atan2(uv.y, uv.x) space.
    float facing = atan2(forward.x, -forward.z) + TYPE_PI/2;
    //Get the direction to the target
    vec3 to_target = GetPosition() - target;
    to_target.y = 0;
    to_target.normalize();
    float target_angle = atan2(to_target.x,-to_target.z) + TYPE_PI/2;

    // Normalize target offset into [-PI, PI] relative to facing.
    // This avoids any wrap-around comparison by keeping both ends numerically close.
    float diff = fmod(target_angle - facing + 3 * TYPE_PI, 2 * TYPE_PI) - TYPE_PI;
    out_facing = facing;
    out_diff  = diff;
}
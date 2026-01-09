#include "PlayerCharacter.h"

#include "Debug.h"
static Debugger* debug = new Debugger("PlayerCharacter",DEBUG_INFO);

PlayerCharacter::PlayerCharacter():Object(){
    animation_transition_time_max = 0.2;

}

PlayerCharacter::~PlayerCharacter(){

}

//Function checks input and applies correct animation and state.
void PlayerCharacter::ProcessInputState(){
    if (!(character_state.input_forward_down || character_state.input_backward_down ||
          character_state.input_left_down || character_state.input_right_down
        || character_state.input_jump)){
        //No input was down. We proceed to idle animation once.
        if (character_state.any_input_was_active){
            SetNextAnimation("Idle");
            ProceedToNextAnimation();
            character_state.any_input_was_active = false;
        }
    }else{
        character_state.any_input_was_active = true;
    }
    if (character_state.input_forward_down){
        SetNextAnimation("CatWalkingInPlace");
        ProceedToNextAnimation();
        MoveForwardBy(-0.025f * animation_transition_factor);
        character_state.input_forward_down = false;
    }
    if (character_state.input_backward_down){
        SetNextAnimation("WalkBackwardInPlace");
        ProceedToNextAnimation();
        MoveForwardBy(0.025f * animation_transition_factor);
        character_state.input_backward_down = false;
    }
    if (character_state.input_left_down){
        float delta = 0.025f;
        if (f_rotation_animation){
            SetNextAnimation("LeftTurnInPlace");
            ProceedToNextAnimation();
            delta = 0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_state.input_left_down = false;
    }
    if (character_state.input_right_down){
        float delta = -0.025f;
        if (f_rotation_animation){
            SetNextAnimation("RightTurnInPlace");
            ProceedToNextAnimation();
            delta = -0.025f * animation_transition_factor;
        }
        quat q = quat(vec3(0,1,0),delta);
        RotateBy(q);
        character_state.input_right_down = false;
    }
    if (character_state.input_jump){
        SetNextAnimation("JoyfullJump");
        ProceedToNextAnimation();
        character_state.input_jump = false;
    }
}

void PlayerCharacter::ApplyAnimation(float time_delta){
    if (f_animation_override){
        if (animation_override_ticks > 0){
            debug->Info("Stepping through animation. ticks = %i, time_delta = %.3f\n",animation_override_ticks,time_delta);
            animation_override_ticks--;
        }else{
            return;
        }
    }

    Bone* hip_bone = FindBone(root_bone_name);
    if (!hip_bone){
        debug->Err("Could not find root/hip bone [%s]\n",root_bone_name.c_str());
        return;
    }

    ProcessInputState();

    //Animation state starts off as invalid.
    if (!current_animation){
        animation_state = ANIMATION_STATE_INVALID;
    }
    if (animation_state == ANIMATION_STATE_INVALID){
        if (next_animation){
            current_animation = next_animation;
            animation_state = ANIMATION_STATE_TRANSITION;
            animation_transition_time = animation_transition_time_max;
            animation_transition_factor = 1.0f;
        }
    }

    if (animation_state == ANIMATION_STATE_LOOPING){
        //Loop the same animation
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            //Animation has ended. We play a frame close to 0.
            current_animation->time_index -= current_animation->duration;
            //We need to correct the body postion and orientation from what is currently displayed,
            //to what will be displayed.
            if (!f_movement_animation_inplace){
                update_hip_position = true;

            }
            hip_posistion_start = hip_bone->GetPosition();
            hip_fwd_start = hip_bone->GetWorldForward();
            if (!current_animation->looped){
                SetNextAnimation("Idle");
                ProceedToNextAnimation();
            }

        }
        current_animation->ApplyInterval(current_animation->time_index);


        if (update_hip_position){
            vec3 hippos_end = hip_bone->GetPosition();
            //How much has the hip moved?
            vec3 d = hip_posistion_start - hippos_end;
            //debug->Info("Looping: Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);
            d.y = 0;
            //MoveForwardBy(-d.z);

            //Now, have we turned around maybe?
            vec3 hip_fwd_end = hip_bone->GetWorldForward();
            //debug->Info("Looping: Hip fwd start | end %.3f %.3f %.3f | %.3f %.3f %.3f\n",hip_fwd_start.x,hip_fwd_start.y,hip_fwd_start.z,hip_fwd_end.x,hip_fwd_end.y,hip_fwd_end.z);
            MoveBy(GetRotation()*d);

            //Compute the angle the hip has rotate in the XZ plane.
            vec3 xz_start = vec3(hip_fwd_start.xz()).normalize();
            vec3 xz_end = vec3(hip_fwd_end.xz()).normalize();

            float dot = xz_start.dot(xz_end);
            dot = clamp(dot,-1.0,1.0);
            //debug->Info("Dot product: %.3f. acos = %.3f\n",dot,acos(dot));
            quat r = quat(vec3(0,-1,0),acos(dot));
            //debug->Info("Q = %.3f %.3f %.3f %.3f\n",r.x,r.y,r.z,r.w);
            RotateBy(r);


            update_hip_position = false;
        }else if (f_movement_animation_inplace){
            //Each frame we update depending on foot placement.
        }
    }else if (animation_state == ANIMATION_STATE_TRANSITION_START){
        //In this state, we need to record the character position to where the hips currently are.
        //We are going to transition by playing this animation

        animation_state = ANIMATION_STATE_TRANSITION;
    }
    if (animation_state == ANIMATION_STATE_TRANSITION){
        //If there is no next animation, we can't proceed.
        if (!next_animation){
            debug->Warn("AnimationSampler: Transition to next = NULL\n");
            animation_state = ANIMATION_STATE_LOOPING;
            return;
        }
        if (next_animation == current_animation){
            debug->Warn("AnimationSampler: Next is identical to current\n");
            animation_state = ANIMATION_STATE_LOOPING;
            return;
        }
        debug->Info("Current animation %p\n");
        //We need to play the current and the next animation,
        //and loop both if we transistion longer than the animation is.
        current_animation->time_index += time_delta;
        if (current_animation->time_index > current_animation->duration){
            current_animation->time_index -= current_animation->duration;
            //update_hip_position = true;
            //hip_posistion_start = hip_bone->GetPosition();
            //hip_fwd_start = hip_bone->GetWorldForward();
            debug->Info("Transition: current_animation rewind.\n");
        }
        //Rewind next animation as well.
        next_animation->time_index += time_delta;
        if (next_animation->time_index > next_animation->duration){
            next_animation->time_index -= next_animation->duration;
            debug->Info("Transition: next_animation rewind.\n");
        }

        animation_transition_factor =  animation_transition_time / animation_transition_time_max;
        if (animation_transition_time_max == 0){
            animation_transition_factor = 0;
        }

        //We might need to undo any hip rotation by applying the reverse to the parent.
        hip_fwd_start = hip_bone->GetWorldForward();
        current_animation->Lerp(next_animation,current_animation->time_index,next_animation->time_index,animation_transition_factor, vec3());
        vec3 hip_fwd_end = hip_bone->GetWorldForward();

        //Compute the angle the hip has rotate in the XZ plane.
        vec3 xz_start = vec3(hip_fwd_start.xz()).normalize();
        vec3 xz_end = vec3(hip_fwd_end.xz()).normalize();
        float dot = xz_start.dot(xz_end);
        dot = clamp(dot,-1.0,1.0);
        //debug->Info("Dot product: %.3f. acos = %.3f\n",dot,acos(dot));
        quat r = quat(vec3(0,-1,0),acos(dot));
        debug->Info("Q = %.3f %.3f %.3f %.3f\n",r.x,r.y,r.z,r.w);
        RotateBy(r);


        if (update_hip_position){
            vec3 hippos_end = hip_bone->GetPosition();
            //How much has the hip moved?
            vec3 d = hip_posistion_start - hippos_end;
            //debug->Info("Transition: Hips delta = %.3f %.3f %.3f\n",d.x,d.y,d.z);
            d.y = 0;
            //MoveBy(GetRotation()*d);
            update_hip_position = false;
        }

        animation_transition_time += time_delta;
        if (animation_transition_time >= animation_transition_time_max){
            animation_transition_time = animation_transition_time_max;
            current_animation = next_animation;
            animation_state = ANIMATION_STATE_LOOPING;
            //update_hip_position = true;
            hip_posistion_start = hip_bone->GetPosition();
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
            if (animation_state == ANIMATION_STATE_INVALID){
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
            if (animation_state == ANIMATION_STATE_INVALID){
                head->SetRotation(r);
            }else{
                head->RotateBy(r);
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
    character_state.input_forward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::MoveBackward(){
    character_state.input_backward_down = true;
}

//Going to play a move forward animation based on whatever animation its in.
void PlayerCharacter::TurnRight(){
    character_state.input_right_down = true;
}

void PlayerCharacter::TurnLeft(){
    character_state.input_left_down = true;
}

void PlayerCharacter::ToIdle(){
    //If the current animation is idle, leave it.
    if (current_animation && current_animation->name.compare("Idle") == 0){
        return;
    }
    //Go to catwalk animation and enable foot tracking
    SetNextAnimation("Idle");
    ProceedToNextAnimation();
}

void PlayerCharacter::Jump(){
    character_state.input_jump = true;
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